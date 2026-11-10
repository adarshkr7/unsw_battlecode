"""Automated tuning pipeline: find the strongest robust configuration of the bot's existing strategies and
settings against v19 and v20, with as few games as possible.

    python -m tools.tune.pipeline [--workers 16] [--scale 1.0] [--iterations 3]

Rerunning the same command resumes: every design is stored in results/tune/experiments/<name>.json and
every game in results/tune/tune.db (a game already played is never played again).

One iteration (all relative to the current baseline, at first bots/v22 = the live bot):
  0 calibrate   baseline vs v19 and v20 on the 17 ladder maps, 3 seeds, both sides
  1 screens     each module on its own, fresh seeds, cheap (8 games per configuration):
                  switches  random factorial design of the strategy switches (low density, adaptive:
                            switches that clearly hurt are sampled less, promising ones more)
                  combat / economy / queen   Bayesian optimisation of the numeric settings: Latin
                            hypercube start, then Thompson sampling from a quadratic logistic surrogate
                  interactions  the most important numeric settings together, with pairwise terms
                Each map size (big >= 1,000 tiles, small) has its own values and its own model.
  2 race        strategy combinations and module optima, successive halving on a shared seed set,
                paired with the baseline's games on the same slots
  3 validate    the top 2 on fresh seeds: 17 ladder maps x 5 seeds + 5 held-out maps + 2 extra
                ladder layouts (vs v19 and v20), and the secondary opponents v15/v18
  4 promote     only if it passes every rule in `promotion_rules`; the old baseline stays available
"""

from __future__ import annotations

import argparse
import copy
import ctypes
import hashlib
import json
import math
import os
import pathlib
import random
import time

import numpy as np

from . import config as cfgmod
from . import db as dbmod
from . import runner as runmod
from . import space, stats

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "results" / "tune"
EXP_DIR = OUT / "experiments"
OPPS = list(runmod.PRIMARY)


def stable_hash(s: str) -> int:
    return int(hashlib.sha1(s.encode()).hexdigest()[:8], 16)


class Experiment:
    """A stored design: batches of configurations with their jobs, plus free-form state."""

    def __init__(self, name: str, kind: str):
        self.path = EXP_DIR / f"{name}.json"
        self.name = name
        if self.path.is_file():
            self.d = json.loads(self.path.read_text(encoding="utf-8"))
        else:
            self.d = {"name": name, "kind": kind, "batches": [], "state": {}, "done": False, "summary": None,
                      "created": time.time()}
            self.save()

    def save(self):
        EXP_DIR.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.d, indent=1, default=float), encoding="utf-8")
        os.replace(tmp, self.path)

    @property
    def batches(self):
        return self.d["batches"]

    @property
    def state(self):
        return self.d["state"]


class Pipeline:
    def __init__(self, args):
        global OUT, EXP_DIR
        self.args = args
        if args.out != "results/tune":
            OUT = ROOT / args.out
            EXP_DIR = OUT / "experiments"
        OUT.mkdir(parents=True, exist_ok=True)
        self.log_path = OUT / "driver.log"
        self.db = dbmod.DB(OUT.relative_to(ROOT) / "tune.db")
        self.base = cfgmod.Base(args.base)
        if args.dry:
            from .dryrun import FakeRunner
            self.runner = FakeRunner(self.db, self.base)
        else:
            self.runner = runmod.Runner(self.db, self.base, workers=args.workers, log=self.log)
        self.overrides: dict[str, dict] = {}
        self.maps_big = runmod.maps_of("tune", "big")
        self.maps_small = runmod.maps_of("tune", "small")
        self.scale = args.scale

    # -- utilities -------------------------------------------------------------------------------
    def log(self, msg: str):
        line = f"{time.strftime('%Y-%m-%d %H:%M:%S')} {msg}"
        print(line, flush=True)
        with self.log_path.open("a", encoding="utf-8") as f:
            f.write(line + "\n")

    def register(self, overrides: dict, experiment: str, optimizer: str, module=None, parent=None, note=None,
                 design=None) -> str:
        canon = self.base.canonical(overrides)
        cid = self.base.config_id(canon)
        self.db.add_config(cid, canon, parent=parent, optimizer=optimizer, experiment=experiment, module=module,
                           note=note, base=self.base.fingerprint, design=design)
        self.overrides[cid] = canon
        return cid

    def ov(self, cid: str) -> dict:
        if cid not in self.overrides:
            self.overrides[cid] = self.db.config(cid)["overrides"]
        return self.overrides[cid]

    def play(self, jobs: list[dict], progress: str = "") -> list[dict]:
        need = {j["config"] for j in jobs}
        return self.runner.run(jobs, {c: self.ov(c) for c in need}, progress)

    def games_of(self, cid: str, seeds=None, opponents=OPPS, maps=None) -> list[dict]:
        rows = self.db.games("config=?", (cid,))
        return [r for r in rows if r["opponent"] in opponents and (seeds is None or r["seed"] in seeds)
                and (maps is None or r["map"] in maps)]

    @property
    def baseline(self) -> str:
        b = self.db.pointer("current_baseline")
        if b is None:
            b = self.register({}, "init", "none", note="base bot (bots/v22 = live v21, switches off)")
            self.db.set_pointer("current_baseline", b, "initial: the live bot")
        return b

    # -- design helpers -------------------------------------------------------------------------
    def scope_jobs(self, cid: str, scope: str, n: int, rng: random.Random, seed_iter, cycle: dict, exp: str, stage: str):
        """n games of config cid on maps of one size: opponents alternate, sides random, maps cycle."""
        maps = self.maps_big if scope == "big" else self.maps_small
        jobs = []
        for i in range(n):
            if not cycle.get(scope):
                cycle[scope] = maps[:]
                rng.shuffle(cycle[scope])
            m = cycle[scope].pop()
            jobs.append(dict(config=cid, opponent=OPPS[i % 2], map=m, seed=next(seed_iter), side=rng.choice("AB"),
                             experiment=exp, stage=stage))
        return jobs

    def flip(self, ov: dict, sw: space.Switch, scope: str) -> dict:
        """Turn switch sw over in one scope, relative to the current overrides."""
        alt = sw.alt.get(scope)
        if not alt:
            return ov
        ov = copy.deepcopy(ov)
        cur = self.base.effective(ov, scope)
        base = self.base.values(scope)
        on = all(cur[k] == self.base.coerce(k, v) for k, v in alt.items())
        sc = ov.setdefault(scope, {})
        for k, v in alt.items():
            if on:
                b = base[k]
                sc[k] = b if b != self.base.coerce(k, v) else (not v if isinstance(v, bool) else b)
            else:
                sc[k] = v
        return ov

    def num_value(self, ov: dict, num: space.Num, scope: str, x: float):
        center = self.base.effective(ov, scope)[num.name]
        v = min(num.hi, max(num.lo, center + x * num.span))
        return int(round(v)) if num.typ is int else round(v, 6)

    def apply_nums(self, ov: dict, xs: dict) -> dict:
        """xs: {scope: {name: x}} with x in [-1, 1] around the baseline overrides `ov`."""
        out = copy.deepcopy(ov)
        for scope, d in xs.items():
            for name, x in d.items():
                out.setdefault(scope, {})[name] = self.num_value(ov, space.NUM_BY_NAME[name], scope, x)
        return out

    # =============================================================================================
    # Stage 0: calibration
    def calibrate(self, it: int):
        name = f"it{it}_calibrate"
        exp = Experiment(name, "calibrate")
        if exp.d["done"]:
            return exp.d["summary"]
        b = self.baseline
        seeds = [1000 + 7 * it + s for s in range(3)]
        jobs = [dict(config=b, opponent=o, map=m, seed=s, side=sd, experiment=name, stage="calibrate")
                for o in OPPS for m in runmod.maps_of("tune") for s in seeds for sd in "AB"]
        self.log(f"[{name}] baseline {b} vs v19/v20: {len(jobs)} games")
        t0 = time.monotonic()
        rows = self.play(jobs, f"{name} ")
        mins = (time.monotonic() - t0) / 60
        summ = stats.summarize(rows)
        summ["games_per_hour"] = len(rows) / max(mins, 1e-3) * 60
        exp.d["summary"] = summ
        exp.d["done"] = True
        exp.save()
        self.db.event("calibrated", name, b, summary=summ)
        self.log(f"[{name}] done: " + ", ".join(f"{k} {v['score']:.3f} (n={v['n']})" for k, v in summ.items()
                                                if isinstance(v, dict)) + f"; {summ['games_per_hour']:.0f} games/h")
        return summ

    # =============================================================================================
    # Stage 1a: strategy switch screen (random factorial design, adaptive densities)
    def screen_switches(self, it: int, n_configs: int):
        name = f"it{it}_switches"
        exp = Experiment(name, "switch_screen")
        if exp.d["done"]:
            return exp.d["summary"]
        base_cid = self.baseline
        base_ov = self.ov(base_cid)
        st = exp.state
        st.setdefault("density", {sc: {s.name: 0.15 for s in space.SWITCHES if sc in s.alt} for sc in space.SCOPES})
        st.setdefault("seed", 1_000_000 + 200_000 * it)
        st.setdefault("baseline", base_cid)
        batch_size = 24
        n_batches = max(4, math.ceil(n_configs / batch_size))
        rng = random.Random(stable_hash(name))
        while len(exp.batches) < n_batches or not all(bt["complete"] for bt in exp.batches):
            if exp.batches and not exp.batches[-1]["complete"]:
                bt = exp.batches[-1]
            else:
                k = len(exp.batches)
                if k >= n_batches // 2:
                    self.adapt_switch_density(exp)
                rng = random.Random(stable_hash(f"{name}/{k}"))
                seed_iter = iter(range(st["seed"] + k * 1000, st["seed"] + (k + 1) * 1000))
                cycle: dict = {}
                items = []
                for i in range(batch_size):
                    design = {}
                    ov = base_ov
                    for sc in space.SCOPES:
                        on = []
                        if rng.random() >= 0.05:  # 5% of configurations are the baseline itself
                            for sw in space.SWITCHES:
                                if sc in sw.alt and rng.random() < st["density"][sc][sw.name]:
                                    on.append(sw.name)
                            for group in space.EXCLUSIVE:
                                hit = [s for s in on if s in group]
                                for s in hit[1:]:
                                    on.remove(s)
                        for s in on:
                            ov = self.flip(ov, space.SWITCH_BY_NAME[s], sc)
                        design[sc] = on
                    cid = self.register(ov, name, "factorial", module="switches", parent=base_cid, design=design)
                    jobs = (self.scope_jobs(cid, "big", 4, rng, seed_iter, cycle, name, "screen")
                            + self.scope_jobs(cid, "small", 4, rng, seed_iter, cycle, name, "screen"))
                    items.append({"config": cid, "design": design, "jobs": jobs})
                bt = {"items": items, "complete": False, "density": copy.deepcopy(st["density"])}
                exp.batches.append(bt)
                exp.save()
            self.log(f"[{name}] batch {len(exp.batches)}/{n_batches}: {len(bt['items'])} configurations")
            self.play([j for it_ in bt["items"] for j in it_["jobs"]], f"{name} b{len(exp.batches)} ")
            bt["complete"] = True
            exp.save()
        summ = self.analyze_switches(exp)
        exp.d["summary"] = summ
        exp.d["done"] = True
        exp.save()
        self.db.event("screen_done", name, base_cid, summary=summ)
        return summ

    def switch_data(self, exp: Experiment, scope: str):
        names = [s.name for s in space.SWITCHES if scope in s.alt]
        design = {}
        for bt in exp.batches:
            for item in bt["items"]:
                design[item["config"]] = set(item["design"][scope])
        rows = [r for r in self.db.games(f"experiment=? AND size=?", (exp.name, scope)) if r["config"] in design]
        X = np.array([[1.0 if n in design[r["config"]] else 0.0 for n in names] for r in rows])
        y = np.array([r["score"] for r in rows])
        cells = [(r["opponent"], r["map"], r["side"]) for r in rows]
        return names, rows, X, y, cells

    def fit_switches(self, exp: Experiment, scope: str):
        names, rows, X, y, cells = self.switch_data(exp, scope)
        if len(rows) < 40:
            return None
        model = stats.Logistic().fit(cells, X, y, np.full(len(names), 0.5))
        out = []
        se = model.se()
        p0 = float(np.mean(y))
        for j, n in enumerate(names):
            b, s = float(model.beta[j]), float(se[j])
            out.append({"name": n, "module": space.SWITCH_BY_NAME[n].module, "beta": b, "se": s,
                        "p_pos": stats.norm_cdf(b / s), "pct": stats.logit_to_pct(b, min(max(p0, 0.05), 0.95)),
                        "n_on": int(X[:, j].sum()), "n": len(rows)})
        return {"effects": out, "n": len(rows), "mean_score": p0}

    def adapt_switch_density(self, exp: Experiment):
        st = exp.state
        for sc in space.SCOPES:
            f = self.fit_switches(exp, sc)
            if not f:
                continue
            for e in f["effects"]:
                if e["p_pos"] < 0.1:
                    st["density"][sc][e["name"]] = 0.04   # clearly hurts: stop spending games on it
                elif e["p_pos"] > 0.6:
                    st["density"][sc][e["name"]] = 0.30   # promising: more games
                else:
                    st["density"][sc][e["name"]] = 0.15
        self.log(f"[{exp.name}] densities: " + "; ".join(
            f"{sc}: " + ", ".join(f"{k}={v:.2f}" for k, v in st["density"][sc].items() if v != 0.15) for sc in space.SCOPES))
        exp.save()

    def analyze_switches(self, exp: Experiment) -> dict:
        summ = {}
        for sc in space.SCOPES:
            f = self.fit_switches(exp, sc)
            summ[sc] = f
            if f:
                eff = sorted(f["effects"], key=lambda e: -e["beta"])
                self.log(f"[{exp.name}] {sc} ({f['n']} games): " + ", ".join(
                    f"{e['name']} {e['pct'] * 100:+.1f}% (P+ {e['p_pos']:.2f})" for e in eff))
        return summ

    # =============================================================================================
    # Stage 1b-d (and interactions): Bayesian optimisation of numeric settings
    def num_dims(self, module, scope: str) -> list[str]:
        if isinstance(module, dict):
            return [n for n in module.get(scope, []) if scope in space.NUM_BY_NAME[n].scopes]
        return [n.name for n in space.NUMS if n.module == module and scope in n.scopes]

    @staticmethod
    def features(xs: np.ndarray, pairs: list[tuple[int, int]]) -> np.ndarray:
        cols = [xs, xs ** 2 - 1 / 3]
        if pairs:
            cols.append(np.stack([xs[:, i] * xs[:, j] for i, j in pairs], axis=1))
        return np.concatenate(cols, axis=1)

    def screen_numeric(self, it: int, module, n_configs: int, seed_base: int, label: str | None = None, pairs_for=None):
        """module: a module name, or (with label) {scope: [setting names]} searched together with pairwise terms."""
        name = f"it{it}_{label or module}"
        exp = Experiment(name, "bo")
        if exp.d["done"]:
            return exp.d["summary"]
        base_cid = self.baseline
        base_ov = self.ov(base_cid)
        st = exp.state
        st.setdefault("seed", seed_base)
        st.setdefault("baseline", base_cid)
        dims = st.setdefault("dims", {sc: self.num_dims(module, sc) for sc in space.SCOPES})
        pairs = {sc: ([(i, j) for i in range(len(dims[sc])) for j in range(i + 1, len(dims[sc]))] if pairs_for else [])
                 for sc in space.SCOPES}
        batch_size = 24
        n_batches = max(4, math.ceil(n_configs / batch_size))
        n_lhs = max(2, int(round(n_batches * 0.4)))
        while len(exp.batches) < n_batches or not all(bt["complete"] for bt in exp.batches):
            if exp.batches and not exp.batches[-1]["complete"]:
                bt = exp.batches[-1]
            else:
                k = len(exp.batches)
                rng = random.Random(stable_hash(f"{name}/{k}"))
                nrng = np.random.default_rng(stable_hash(f"{name}/{k}/np"))
                seed_iter = iter(range(st["seed"] + k * 1000, st["seed"] + (k + 1) * 1000))
                cycle: dict = {}
                points = {}
                for sc in space.SCOPES:
                    d = len(dims[sc])
                    if d == 0:
                        points[sc] = [np.zeros(0)] * batch_size
                        continue
                    if k < n_lhs:
                        # Latin hypercube in [-1, 1]^d
                        P = np.stack([(nrng.permutation(batch_size) + nrng.random(batch_size)) / batch_size * 2 - 1
                                      for _ in range(d)], axis=1)
                        points[sc] = list(P)
                    else:
                        model = self.fit_numeric(exp, sc, dims[sc], pairs[sc])
                        pts = []
                        for i in range(batch_size):
                            if model is None or i % 4 == 3:          # 25% random exploration
                                pts.append(nrng.uniform(-1, 1, d))
                            else:                                     # Thompson sample
                                beta = model.sample(nrng)[0]
                                pts.append(self.maximize(beta, d, pairs[sc], nrng))
                        points[sc] = pts
                items = []
                for i in range(batch_size):
                    xs = {sc: {n: float(points[sc][i][j]) for j, n in enumerate(dims[sc])} for sc in space.SCOPES}
                    ov = self.apply_nums(base_ov, xs)
                    cid = self.register(ov, name, "bo-thompson" if k >= n_lhs else "bo-lhs", module=str(label or module),
                                        parent=base_cid, design=xs)
                    jobs = (self.scope_jobs(cid, "big", 4, rng, seed_iter, cycle, name, "screen")
                            + self.scope_jobs(cid, "small", 4, rng, seed_iter, cycle, name, "screen"))
                    items.append({"config": cid, "design": xs, "jobs": jobs})
                bt = {"items": items, "complete": False}
                exp.batches.append(bt)
                exp.save()
            self.log(f"[{name}] batch {len(exp.batches)}/{n_batches}")
            self.play([j for it_ in bt["items"] for j in it_["jobs"]], f"{name} b{len(exp.batches)} ")
            bt["complete"] = True
            exp.save()
        summ = self.analyze_numeric(exp, dims, pairs)
        exp.d["summary"] = summ
        exp.d["done"] = True
        exp.save()
        self.db.event("screen_done", name, base_cid, summary=summ)
        return summ

    def numeric_data(self, exp: Experiment, scope: str, dims: list[str]):
        design = {}
        for bt in exp.batches:
            for item in bt["items"]:
                design[item["config"]] = [item["design"][scope].get(n, 0.0) for n in dims]
        rows = [r for r in self.db.games("experiment=? AND size=?", (exp.name, scope)) if r["config"] in design]
        xs = np.array([design[r["config"]] for r in rows]).reshape(len(rows), len(dims))
        y = np.array([r["score"] for r in rows])
        cells = [(r["opponent"], r["map"], r["side"]) for r in rows]
        return rows, xs, y, cells

    def fit_numeric(self, exp: Experiment, scope: str, dims: list[str], pairs):
        rows, xs, y, cells = self.numeric_data(exp, scope, dims)
        if len(rows) < 40 or not dims:
            return None
        X = self.features(xs, pairs)
        d = len(dims)
        prior = np.concatenate([np.full(d, 0.3), np.full(d, 0.3), np.full(len(pairs), 0.2)])
        return stats.Logistic().fit(cells, X, y, prior)

    def predict(self, beta: np.ndarray, xs: np.ndarray, pairs) -> np.ndarray:
        return self.features(xs, pairs) @ beta

    def maximize(self, beta: np.ndarray, d: int, pairs, nrng, cov: np.ndarray | None = None, lcb: float = 0.0):
        """Point in [-1,1]^d with the highest predicted logit (minus lcb * sd when cov is given)."""
        def val(P):
            F = self.features(P, pairs)
            v = F @ beta
            if cov is not None and lcb:
                v = v - lcb * np.sqrt(np.einsum("ij,jk,ik->i", F, cov, F))
            return v
        P = np.concatenate([nrng.uniform(-1, 1, (3000, d)), np.zeros((1, d))])
        best = P[int(np.argmax(val(P)))].copy()
        grid = np.linspace(-1, 1, 41)
        for _ in range(3):  # coordinate ascent on a grid
            for j in range(d):
                Q = np.repeat(best[None, :], len(grid), axis=0)
                Q[:, j] = grid
                best = Q[int(np.argmax(val(Q)))].copy()
        return best

    def analyze_numeric(self, exp: Experiment, dims: dict, pairs: dict) -> dict:
        """Per setting: a joint Wald test of its linear and quadratic terms. Only settings with a detectable
        effect (p < 0.05) move from the baseline; the others stay put (no chasing noise in a 10-d box)."""
        summ = {}
        for sc in space.SCOPES:
            ds = dims[sc]
            model = self.fit_numeric(exp, sc, ds, pairs[sc])
            if model is None:
                summ[sc] = None
                continue
            nrng = np.random.default_rng(stable_hash(exp.name + sc))
            d = len(ds)
            se = model.se()
            base_ov = self.ov(exp.state["baseline"])
            params, chosen = [], []
            grid = np.linspace(-1, 1, 21)
            for j, n in enumerate(ds):
                idx = [j, d + j]
                b = model.beta[idx]
                S = model.cov[np.ix_(idx, idx)]
                chi2 = float(b @ np.linalg.solve(S, b))
                pval = math.exp(-chi2 / 2)  # chi-square, 2 degrees of freedom
                Q = np.zeros((21, d))
                Q[:, j] = grid
                curve = self.features(Q, pairs[sc]) @ model.beta
                curve = curve - curve[10]
                if pval < 0.05:
                    chosen.append(j)
                params.append({"name": n, "base": self.base.effective(base_ov, sc)[n],
                               "lin": float(b[0]), "lin_se": float(se[j]), "quad": float(b[1]), "quad_se": float(se[d + j]),
                               "p_value": pval, "range_pct": float(curve.max() - curve.min()) / 4 * 100,
                               "best_alone_x": float(grid[int(np.argmax(curve))])})
            xbest = np.zeros(d)
            if chosen:
                sub_pairs = [(chosen.index(i), chosen.index(k)) for i, k in pairs[sc] if i in chosen and k in chosen]
                cols = chosen + [d + j for j in chosen] + [2 * d + q for q, (i, k) in enumerate(pairs[sc]) if i in chosen and k in chosen]
                beta = model.beta[cols]
                cov = model.cov[np.ix_(cols, cols)]
                xb = self.maximize(beta, len(chosen), sub_pairs, nrng, cov, lcb=0.5)
                xbest[chosen] = xb
            F = self.features(np.stack([xbest, np.zeros(d)]), pairs[sc])
            diff = F[0] - F[1]
            gain = float(diff @ model.beta)
            gain_sd = float(np.sqrt(max(diff @ model.cov @ diff, 1e-12)))
            p0 = model.p_mean
            for j, prm in enumerate(params):
                prm["x"] = float(xbest[j])
                prm["value"] = self.num_value(base_ov, space.NUM_BY_NAME[prm["name"]], sc, float(xbest[j]))
                prm["moved"] = j in chosen
            inter = [{"a": ds[i], "b": ds[k], "beta": float(bb), "se": float(ss)}
                     for (i, k), bb, ss in zip(pairs[sc], model.beta[2 * d:], se[2 * d:])]
            summ[sc] = {"n": model.n, "x": {n: float(v) for n, v in zip(ds, xbest) if v != 0},
                        "gain_logit": gain, "gain_sd": gain_sd,
                        "gain_pct": stats.logit_to_pct(gain, min(max(p0, .05), .95)) * 100,
                        "p_gain": stats.norm_cdf(gain / gain_sd) if gain else 0.0, "params": params, "interactions": inter}
            self.log(f"[{exp.name}] {sc} ({model.n} games): predicted gain {summ[sc]['gain_pct']:+.1f}% "
                     f"(P {summ[sc]['p_gain']:.2f}); " + ", ".join(
                         f"{p['name']} {p['base']}->{p['value']} (p {p['p_value']:.3f}, range {p['range_pct']:.1f}%)"
                         + ("*" if p["moved"] else "") for p in params))
        return summ

    # =============================================================================================
    # Stage 2: candidates and the race
    def candidates(self, it: int, screens: dict) -> list[tuple[str, str]]:
        base_cid = self.baseline
        base_ov = self.ov(base_cid)
        cands = []
        sw = screens.get("switches") or {}
        good = {sc: [] for sc in space.SCOPES}
        for sc in space.SCOPES:
            for e in ((sw.get(sc) or {}).get("effects") or []):
                if e["p_pos"] > 0.9 and e["pct"] > 0.01:  # ~3 of 72 null switches pass by chance; the race filters
                    good[sc].append(e)
        for sc in space.SCOPES:
            # never two switches of one exclusive group
            names = [e["name"] for e in sorted(good[sc], key=lambda e: -e["beta"])]
            for group in space.EXCLUSIVE:
                hit = [n for n in names if n in group]
                for n in hit[1:]:
                    good[sc] = [e for e in good[sc] if e["name"] != n]

        def pack(groups):
            ov = base_ov
            for sc in space.SCOPES:
                for e in good[sc]:
                    if space.SWITCH_BY_NAME[e["name"]].module in groups:
                        ov = self.flip(ov, space.SWITCH_BY_NAME[e["name"]], sc)
            return ov

        named = {
            "aggressive": ["aggressive"], "defensive": ["defensive"], "economic": ["economic"],
            "adaptive": ["adaptive"], "structure": ["structure"],
            "aggressive+economic": ["aggressive", "economic"], "aggressive+defensive": ["aggressive", "defensive"],
            "economic+defensive": ["economic", "defensive"], "adaptive+aggressive": ["adaptive", "aggressive"],
            "adaptive+economic": ["adaptive", "economic"], "adaptive+defensive": ["adaptive", "defensive"],
            "all_switches": space.STRATEGY_GROUPS,
        }
        for label, groups in named.items():
            cands.append((label, pack(groups)))
        # the strongest single switches on their own (in the scopes where they looked good)
        singles = {}
        for sc in space.SCOPES:
            for e in ((sw.get(sc) or {}).get("effects") or []):
                if e["beta"] / e["se"] > 1.28:
                    singles.setdefault(e["name"], []).append((sc, e["beta"] / e["se"]))
        for name_, scs in sorted(singles.items(), key=lambda kv: -max(z for _, z in kv[1]))[:4]:
            ov = base_ov
            for sc, _ in scs:
                ov = self.flip(ov, space.SWITCH_BY_NAME[name_], sc)
            cands.append((f"single:{name_}", ov))
        # numeric module optima (each scope's best values, if the model expects a gain)
        nums = {}
        for mod in ("combat", "economy", "queen", "interactions"):
            s = screens.get(mod)
            if not s:
                continue
            xs = {}
            for sc in space.SCOPES:
                r = s.get(sc)
                if r and r["gain_logit"] > 0 and r["p_gain"] > 0.6:
                    xs[sc] = r["x"]
            if xs:
                nums[mod] = xs
                cands.append((f"num:{mod}", self.apply_nums(base_ov, xs)))
        modules = [m for m in ("combat", "economy", "queen") if m in nums]
        if len(modules) >= 2:
            merged = {sc: {} for sc in space.SCOPES}
            for m in modules:
                for sc, d in nums[m].items():
                    merged[sc].update(d)
            cands.append(("num:all", self.apply_nums(base_ov, merged)))
            all_sw = pack(space.STRATEGY_GROUPS)
            cands.append(("num:all+switches", self.apply_nums(all_sw, merged)))
        if "interactions" in nums:
            cands.append(("num:interactions+switches", self.apply_nums(pack(space.STRATEGY_GROUPS), nums["interactions"])))
        out, seen = [], {base_cid}
        for label, ov in cands:
            cid = self.register(ov, f"it{it}_race", "combination", module="race", parent=base_cid, note=label)
            if cid in seen:
                continue
            seen.add(cid)
            out.append((label, cid))
        return out

    def race_jobs(self, cid: str, rnd: int, it: int, name: str) -> list[dict]:
        seed = 500000 + 1000 * it + rnd
        return [dict(config=cid, opponent=o, map=m, seed=seed, side=sd, experiment=name, stage="race")
                for o in OPPS for m in runmod.maps_of("tune") for sd in "AB"]

    def race(self, it: int, screens: dict, max_rounds: int = 6) -> list[dict]:
        name = f"it{it}_race"
        exp = Experiment(name, "race")
        if exp.d["done"]:
            return exp.d["summary"]
        st = exp.state
        if "candidates" not in st:
            st["candidates"] = [{"label": l, "config": c, "alive": True, "rounds": 0} for l, c in self.candidates(it, screens)]
            st["baseline"] = self.baseline
            exp.save()
            self.log(f"[{name}] {len(st['candidates'])} candidates: " + ", ".join(c["label"] for c in st["candidates"]))
        base = st["baseline"]
        # successive halving: after round 1 keep the better half, after round 3 the best 3, end after max_rounds
        schedule = {1: lambda n: max(3, math.ceil(n / 2)), 3: lambda n: min(n, 3)}
        rnd = st.get("round", 0)
        while rnd < max_rounds:
            rnd += 1
            alive = [c for c in st["candidates"] if c["alive"]]
            if not alive:
                break
            jobs = self.race_jobs(base, rnd, it, name)
            for c in alive:
                jobs += self.race_jobs(c["config"], rnd, it, name)
            self.log(f"[{name}] round {rnd}: {len(alive)} candidates, {len(jobs)} games (some cached)")
            self.play(jobs, f"{name} r{rnd} ")
            seeds = {500000 + 1000 * it + r for r in range(1, rnd + 1)}
            brows = self.games_of(base, seeds)
            for c in alive:
                c["rounds"] = rnd
                p = stats.paired(self.games_of(c["config"], seeds), brows)
                c["paired"] = p
                a = p.get("all", {"delta": 0, "se": 1})
                c["delta"], c["se"] = a["delta"], a["se"]
                # clearly worse: stop spending games on it
                if a["delta"] + 1.5 * a["se"] < 0:
                    c["alive"] = False
                    c["dropped"] = f"round {rnd}: clearly worse"
            alive = sorted([c for c in st["candidates"] if c["alive"]], key=lambda c: -(c["delta"] - 0.5 * c["se"]))
            if rnd in schedule:
                keep = schedule[rnd](len(alive))
                for c in alive[keep:]:
                    c["alive"] = False
                    c["dropped"] = f"round {rnd}: not in the top {keep}"
            st["round"] = rnd
            exp.save()
            self.log(f"[{name}] after round {rnd}: " + "; ".join(
                f"{c['label']} {c.get('delta', 0) * 100:+.1f}+-{c.get('se', 0) * 100:.1f}{'' if c['alive'] else ' (out)'}"
                for c in sorted(st["candidates"], key=lambda c: -c.get("delta", -9))))
        res = sorted(st["candidates"], key=lambda c: -(c.get("delta", -9) - 0.5 * c.get("se", 0)))
        exp.d["summary"] = res
        exp.d["done"] = True
        exp.save()
        self.db.event("race_done", name, base, ranking=[(c["label"], c["config"], c.get("delta"), c.get("se")) for c in res])
        return res

    # =============================================================================================
    # Stage 3: heavy validation on fresh seeds; Stage 4: promotion
    def validation_jobs(self, cid: str, it: int, name: str, ext: int = 0) -> list[dict]:
        """Block `ext` of validation games (block 0, then extensions on new seeds for close calls)."""
        jobs = []
        s0 = 800000 + 1000 * it + 300 * ext
        for o in OPPS:
            for m in runmod.maps_of("tune"):
                for s in range(5):
                    for sd in "AB":
                        jobs.append(dict(config=cid, opponent=o, map=m, seed=s0 + s, side=sd, experiment=name, stage="validate"))
            for m in runmod.maps_of("heldout") + runmod.EXTRA_MAPS:
                for s in range(5):
                    for sd in "AB":
                        jobs.append(dict(config=cid, opponent=o, map=m, seed=s0 + 100 + s, side=sd, experiment=name,
                                         stage="validate-heldout"))
        for o in runmod.SECONDARY:
            for m in runmod.maps_of("tune"):
                for s in range(2):
                    for sd in "AB":
                        jobs.append(dict(config=cid, opponent=o, map=m, seed=s0 + 200 + s, side=sd, experiment=name,
                                         stage="validate-secondary"))
        return jobs

    def metrics(self, cid: str, base: str, it: int, n_ext: int = 1) -> dict:
        seeds, sseeds = set(), set()
        for ext in range(n_ext):
            s0 = 800000 + 1000 * it + 300 * ext
            seeds |= set(range(s0, s0 + 5)) | set(range(s0 + 100, s0 + 105))
            sseeds |= set(range(s0 + 200, s0 + 202))
        prim = self.games_of(cid, seeds)
        bprim = self.games_of(base, seeds)
        sec = self.games_of(cid, sseeds, opponents=runmod.SECONDARY)
        bsec = self.games_of(base, sseeds, opponents=runmod.SECONDARY)
        held = set(runmod.maps_of("heldout") + runmod.EXTRA_MAPS)
        summ = stats.summarize(prim)
        pd = stats.paired(prim, bprim)
        pd_held = stats.paired([r for r in prim if r["map"] in held], [r for r in bprim if r["map"] in held])
        pd_sec = stats.paired(sec, bsec)
        cells = [summ.get(f"{o}/{z}", {}).get("score") for o in OPPS for z in ("big", "small")]
        robust = min(c for c in cells if c is not None) if any(c is not None for c in cells) else 0.0
        maps = {k[4:]: v["delta"] for k, v in pd.items() if k.startswith("map:")}
        return {
            "config": cid, "n": len(prim),
            "score_v19": summ.get("v19", {}).get("score"), "score_v20": summ.get("v20", {}).get("score"),
            "ci_v19": [summ.get("v19", {}).get("lo"), summ.get("v19", {}).get("hi")],
            "ci_v20": [summ.get("v20", {}).get("lo"), summ.get("v20", {}).get("hi")],
            "robustness": robust,
            "fitness": stats.fitness(summ.get("v19", {}).get("score", 0), summ.get("v20", {}).get("score", 0), robust),
            "paired": {k: v for k, v in pd.items() if not k.startswith("map:")},
            "paired_heldout": pd_held.get("all"),
            "paired_secondary": pd_sec,
            "worst_map": min(maps.items(), key=lambda kv: kv[1]) if maps else None,
            "per_map_delta": maps,
            "queen_alive": float(np.mean([r["our_queen"] > 0 for r in prim])) if prim else None,
            "mean_total": float(np.mean([r["our_total"] or 0 for r in prim])) if prim else None,
            "max_points": max((r["our_maxpts"] or 0) for r in prim) if prim else None,
            "scores": summ,
        }

    @staticmethod
    def promotion_rules(m: dict) -> list[tuple[str, bool, str]]:
        p = m["paired"]
        a = p.get("all", {"delta": 0, "se": 1})
        rules = [("aggregate better (paired, one-sided 95%)", a["delta"] - 1.645 * a["se"] > 0,
                  f"{a['delta'] * 100:+.1f} +- {a['se'] * 100:.1f} pts")]
        for o in OPPS:
            q = p.get(o, {"delta": 0, "se": 1})
            rules.append((f"no regression vs {o}", q["delta"] > -0.02 and q["delta"] + 1.645 * q["se"] > 0,
                          f"{q['delta'] * 100:+.1f} +- {q['se'] * 100:.1f}"))
        for z in ("big", "small"):
            q = p.get(z, {"delta": 0, "se": 1})
            rules.append((f"consistent on {z} maps", q["delta"] + 1.645 * q["se"] > 0, f"{q['delta'] * 100:+.1f} +- {q['se'] * 100:.1f}"))
        h = m.get("paired_heldout") or {"delta": 0, "se": 1}
        rules.append(("held-out maps not worse", h["delta"] > -0.04, f"{h['delta'] * 100:+.1f} +- {h['se'] * 100:.1f}"))
        s = (m.get("paired_secondary") or {}).get("all", {"delta": 0, "se": 1})
        rules.append(("secondary opponents (v15/v18) not significantly worse", s["delta"] + 1.645 * s["se"] > 0,
                      f"{s['delta'] * 100:+.1f} +- {s['se'] * 100:.1f}"))
        rules.append(("CPU under 70M points/turn", (m.get("max_points") or 0) < 70e6, f"{(m.get('max_points') or 0) / 1e6:.1f}M"))
        return rules

    def validate(self, it: int, race: list[dict], n_final: int = 2) -> dict:
        name = f"it{it}_validate"
        exp = Experiment(name, "validate")
        if exp.d["done"]:
            return exp.d["summary"]
        st = exp.state
        if "finalists" not in st:
            fin = [c for c in race if c["alive"] and c.get("delta", -1) > 0][:n_final]
            st["finalists"] = [{"label": c["label"], "config": c["config"]} for c in fin]
            st["baseline"] = self.baseline
            exp.save()
        base = st["baseline"]
        if not st["finalists"]:
            self.log(f"[{name}] no candidate beat the baseline in the race: nothing to validate")
            exp.d["summary"] = {"finalists": [], "promoted": None}
            exp.d["done"] = True
            exp.save()
            return exp.d["summary"]
        out = {"baseline": None, "finalists": []}
        for f in st["finalists"]:
            f.setdefault("blocks", 1)
        ext = 0
        while True:
            # Block `ext`: the baseline and every finalist that still needs it.
            todo = [f for f in st["finalists"] if f["blocks"] > ext]
            if not todo:
                break
            jobs = self.validation_jobs(base, it, name, ext)
            for f in todo:
                jobs += self.validation_jobs(f["config"], it, name, ext)
            self.log(f"[{name}] block {ext}: {len(todo)} finalists + baseline, {len(jobs)} games (some cached)")
            self.play(jobs, f"{name} block{ext} ")
            for f in todo:
                m = self.metrics(f["config"], base, it, f["blocks"])
                a = m["paired"].get("all", {"delta": 0, "se": 1})
                rules = self.promotion_rules(m)
                only_aggregate_fails = [r for r, ok, _ in rules if not ok] == [rules[0][0]]
                # A close call (z > 1 with every other rule passing) earns another block of fresh seeds, up to 3 blocks.
                if only_aggregate_fails and a["delta"] / a["se"] > 1.0 and f["blocks"] < 3:
                    f["blocks"] += 1
                    self.log(f"[{name}] {f['label']}: close call ({a['delta'] * 100:+.1f} +- {a['se'] * 100:.1f}), "
                             f"adding validation block {f['blocks']}")
            exp.save()
            ext += 1
        nb = max(f["blocks"] for f in st["finalists"])
        out["baseline"] = self.metrics(base, base, it, nb)
        for f in st["finalists"]:
            m = self.metrics(f["config"], base, it, f["blocks"])
            m["label"] = f["label"]
            m["blocks"] = f["blocks"]
            rules = self.promotion_rules(m)
            m["rules"] = rules
            m["passes"] = all(ok for _, ok, _ in rules)
            out["finalists"].append(m)
            self.log(f"[{name}] {f['label']} ({f['config']}, {m['n']} games): fitness {m['fitness']:.3f} vs baseline "
                     f"{out['baseline']['fitness']:.3f}; " + "; ".join(f"{r} {'OK' if ok else 'FAIL'} ({t})" for r, ok, t in rules))
        promoted = None
        passing = sorted([m for m in out["finalists"] if m["passes"]], key=lambda m: -m["fitness"])
        if passing:
            promoted = passing[0]
            self.db.set_pointer("current_baseline", promoted["config"], f"it{it}: promoted {promoted['label']}")
            folder = OUT / "promoted" / f"it{it}_{promoted['config']}"
            self.base.make_bot(self.ov(promoted["config"]), folder)
            self.db.event("promoted", name, promoted["config"], label=promoted["label"], previous=base,
                          folder=str(folder.relative_to(ROOT)), metrics={k: v for k, v in promoted.items() if k != "scores"})
            self.log(f"[{name}] PROMOTED {promoted['label']} ({promoted['config']}); previous baseline {base} kept; "
                     f"bot written to {folder.relative_to(ROOT)}")
        else:
            self.db.event("not_promoted", name, base, finalists=[(m["label"], m["config"]) for m in out["finalists"]])
            self.log(f"[{name}] nothing promoted; the baseline stays {base}")
        out["promoted"] = promoted["config"] if promoted else None
        self.update_pointers(out)
        exp.d["summary"] = out
        exp.d["done"] = True
        exp.save()
        return out

    def update_pointers(self, out: dict):
        allm = [out["baseline"]] + out["finalists"]
        hist = OUT / "validated.json"
        rec = json.loads(hist.read_text(encoding="utf-8")) if hist.is_file() else {}
        for m in allm:
            rec[m["config"]] = {k: m.get(k) for k in ("label", "fitness", "score_v19", "score_v20", "robustness", "n")}
        hist.write_text(json.dumps(rec, indent=1), encoding="utf-8")
        # Only configurations measured on the same validation seeds are comparable: use this round's.
        for key, ptr in (("fitness", "best_overall"), ("score_v19", "best_vs_v19"), ("score_v20", "best_vs_v20"),
                         ("robustness", "most_robust")):
            best = max(allm, key=lambda m: m.get(key) or 0)
            self.db.set_pointer(ptr, best["config"], f"{key} {best.get(key):.3f} on validation seeds")

    # =============================================================================================
    def iteration(self, it: int) -> bool:
        s = self.scale * (1.0 if it == 1 else 0.6)
        self.log(f"=== iteration {it} (baseline {self.baseline}, budget scale {s:.2f}) ===")
        cal = self.calibrate(it) if it == 1 else None
        screens = {"switches": self.screen_switches(it, int(240 * s))}
        for i, mod in enumerate(("combat", "economy", "queen")):
            screens[mod] = self.screen_numeric(it, mod, int(168 * s), seed_base=1_000_000 + 200_000 * it + 40_000 * (i + 1))
        # interactions among the settings that mattered most
        top = {}
        for mod in ("combat", "economy", "queen"):
            for sc in space.SCOPES:
                r = (screens[mod] or {}).get(sc)
                for p in (r or {}).get("params", []):
                    top.setdefault(sc, []).append((-p["p_value"], p["range_pct"], p["name"]))
        chosen = {sc: [n for *_, n in sorted(top.get(sc, []), reverse=True)[:5]] for sc in space.SCOPES}
        self.log(f"[it{it}] interaction search over {chosen} (unless already stored)")
        screens["interactions"] = self.screen_numeric(it, chosen, int(144 * s), seed_base=1_000_000 + 200_000 * it + 160_000,
                                                      label="interactions", pairs_for=True)
        race = self.race(it, screens)
        val = self.validate(it, race)
        from . import report
        report.write(self)
        return bool(val.get("promoted"))

    def main(self):
        if os.name == "nt":  # keep the machine awake while tuning
            ctypes.windll.kernel32.SetThreadExecutionState(0x80000000 | 0x00000001)
        self.log(f"pipeline start: base {self.base.folder.name} ({self.base.fingerprint}), workers {self.args.workers}, "
                 f"git {self.db.git}")
        for it in range(1, self.args.iterations + 1):
            promoted = self.iteration(it)
            if not promoted:
                self.log(f"iteration {it} promoted nothing: stopping")
                break
        from . import report
        report.write(self, final=True)
        self.log("pipeline finished")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", default="bots/v22")
    ap.add_argument("--workers", type=int, default=16)
    ap.add_argument("--scale", type=float, default=1.0, help="multiplies every screening budget")
    ap.add_argument("--iterations", type=int, default=3)
    ap.add_argument("--out", default="results/tune")
    ap.add_argument("--dry", action="store_true", help="simulated games (tests the pipeline in seconds)")
    args = ap.parse_args()
    Pipeline(args).main()


if __name__ == "__main__":
    main()
