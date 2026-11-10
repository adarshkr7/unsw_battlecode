"""Final report of the tuning pipeline: results/tune/report.md (for people) and report.json (for tools).

    python -m tools.tune.report            (rebuild from the stored experiments at any time)
"""

from __future__ import annotations

import json
import pathlib
import time

from . import space


def _pct(x, digits=1):
    return "?" if x is None else f"{x * 100:+.{digits}f}"


def collect(p) -> dict:
    from . import pipeline as pl
    exps = {}
    for f in sorted(pl.EXP_DIR.glob("*.json")):
        exps[f.stem] = json.loads(f.read_text(encoding="utf-8"))
    base0 = p.db.pointer_history("current_baseline")
    cur = p.db.pointer("current_baseline")
    validations = {k: v["summary"] for k, v in exps.items() if v["kind"] == "validate" and v.get("summary")}
    best = None
    for k, v in validations.items():
        for m in v.get("finalists", []):
            if m.get("passes") and (best is None or m["fitness"] > best["fitness"]):
                best = dict(m, iteration=k)
    # importance and dead settings from every numeric screen
    importance, dead = [], []
    switches = []
    for k, v in exps.items():
        s = v.get("summary")
        if not s or v["kind"] not in ("bo", "switch_screen"):
            continue
        for sc in space.SCOPES:
            r = s.get(sc)
            if not r:
                continue
            if v["kind"] == "bo":
                for prm in r["params"]:
                    row = dict(experiment=k, scope=sc, **{x: prm[x] for x in ("name", "base", "value", "range_pct", "p_value")})
                    (importance if prm["p_value"] < 0.05 else dead).append(row)
            else:
                for e in r["effects"]:
                    switches.append(dict(experiment=k, scope=sc, **e))
    importance.sort(key=lambda r: r["p_value"])
    races = {k: v["summary"] for k, v in exps.items() if v["kind"] == "race" and v.get("summary")}
    return {"generated": time.strftime("%Y-%m-%d %H:%M:%S"), "current_baseline": cur,
            "baseline_history": base0, "best": best, "validations": validations, "importance": importance,
            "no_effect": dead, "switches": switches, "races": races,
            "pointers": {n: p.db.pointer(n) for n in ("current_baseline", "best_overall", "best_vs_v19", "best_vs_v20",
                                                       "most_robust")},
            "games": p.db.con.execute("SELECT COUNT(*), SUM(ok) FROM games").fetchone()}


def write(p, final: bool = False) -> pathlib.Path:
    from . import pipeline as pl
    d = collect(p)
    cur = d["current_baseline"]
    best = d["best"]
    L = [f"# Tuning report ({'final' if final else 'interim'}, {d['generated']})", ""]
    L.append(f"Games played: {d['games'][0]} ({d['games'][1]} valid). Base bot: `{p.base.folder.name}` "
             f"(fingerprint {p.base.fingerprint}). All results: `results/tune/tune.db`, designs in `results/tune/experiments/`.")
    L.append("")
    L.append("## BEST CONFIGURATION")
    L.append("")
    if best:
        ov = p.ov(best["config"])
        L.append(f"Config `{best['config']}` ({best['label']}, validated in {best['iteration']}); current baseline `{cur}`.")
        L.append("")
        L.append("Strategy and parameters (changes from the live bot, per map size):")
        L.append("")
        for sc in space.SCOPES:
            ch = ov.get(sc, {})
            base = p.base.values(sc)
            L.append(f"- **{sc} maps**: " + (", ".join(f"`{k}` {base[k]} -> {v}" for k, v in sorted(ch.items())) or "no change"))
        L.append("")
        pd = best["paired"]
        L.append(f"Fitness: {best['fitness']:.3f} (0.45 x score vs v19 + 0.45 x score vs v20 + 0.10 x worst opponent/size cell)")
        L.append("")
        for o in ("v19", "v20"):
            q = pd.get(o, {})
            ci = best[f"ci_{o}"]
            L.append(f"vs {o}: score {best[f'score_{o}']:.3f} (95% {ci[0]:.3f}-{ci[1]:.3f}); "
                     f"vs baseline {_pct(q.get('delta'))} +- {_pct(q.get('se')).lstrip('+')} points (paired, n={q.get('n')})")
            L.append("")
        L.append(f"Robustness (worst opponent x map-size score): {best['robustness']:.3f}; worst map vs baseline: "
                 f"{best['worst_map']}")
        L.append("")
        L.append("Baseline comparison (paired on identical slots, fresh validation seeds): " + ", ".join(
            f"{k} {_pct(v['delta'])} +- {v['se'] * 100:.1f} (n={v['n']})" for k, v in pd.items()))
        L.append("")
        L.append("Promotion rules: " + "; ".join(f"{r}: {'pass' if ok else 'FAIL'} ({t})" for r, ok, t in best["rules"]))
    else:
        L.append(f"No configuration has passed validation yet: the baseline `{cur}` (the live bot) stays.")
    L.append("")
    L.append("## Validations")
    for k, v in d["validations"].items():
        b = v.get("baseline") or {}
        L.append(f"### {k}")
        if b:
            L.append(f"- baseline `{b['config']}`: v19 {b['score_v19']:.3f}, v20 {b['score_v20']:.3f}, fitness {b['fitness']:.3f}")
        for m in v.get("finalists", []):
            a = m["paired"].get("all", {})
            L.append(f"- {m['label']} `{m['config']}`: v19 {m['score_v19']:.3f}, v20 {m['score_v20']:.3f}, fitness "
                     f"{m['fitness']:.3f}, vs baseline {_pct(a.get('delta'))} +- {a.get('se', 0) * 100:.1f}: "
                     f"{'PROMOTED' if v.get('promoted') == m['config'] else ('passes' if m['passes'] else 'rejected')}")
        L.append("")
    L.append("## Most important parameters (numeric screens)")
    L.append("")
    L.append("Settings with a detectable effect (p < 0.05, joint test of linear + quadratic terms). Range = how much")
    L.append("the model's win rate moves across the searched box with the others at baseline.")
    L.append("")
    L.append("| screen | scope | setting | baseline | best | range | p |")
    L.append("|---|---|---|---|---|---|---|")
    for r in d["importance"][:30]:
        L.append(f"| {r['experiment']} | {r['scope']} | `{r['name']}` | {r['base']} | {r['value']} | "
                 f"{r['range_pct']:.1f}% | {r['p_value']:.3f} |")
    L.append("")
    L.append("## Parameters that did not matter")
    L.append("")
    L.append(", ".join(sorted({f"`{r['name']}` ({r['scope']})" for r in d["no_effect"]})) or "none")
    L.append("")
    L.append("## Strategy switches (factorial screen; effect on win rate, P(helps))")
    L.append("")
    sw = sorted(d["switches"], key=lambda e: -e["beta"])
    L.append("| screen | scope | switch | group | effect | P(helps) | games with it on |")
    L.append("|---|---|---|---|---|---|---|")
    for e in sw:
        L.append(f"| {e['experiment']} | {e['scope']} | `{e['name']}` | {e['module']} | {e['pct'] * 100:+.1f}% | "
                 f"{e['p_pos']:.2f} | {e['n_on']} |")
    L.append("")
    rejected = sorted({f"`{e['name']}` ({e['scope']})" for e in sw if e["p_pos"] < 0.1})
    L.append("Rejected strategies (P(helps) < 0.1): " + (", ".join(rejected) or "none"))
    L.append("")
    L.append("## Best strategy combinations (race, paired vs baseline on shared seeds)")
    for k, v in d["races"].items():
        L.append(f"### {k}")
        for c in v:
            L.append(f"- {c['label']} `{c['config']}`: {_pct(c.get('delta'))} +- {c.get('se', 0) * 100:.1f} after "
                     f"{c.get('rounds')} rounds" + (f" ({c['dropped']})" if c.get("dropped") else ""))
        L.append("")
    L.append("## Recommended next experiment")
    L.append("")
    nxt = []
    if d["importance"]:
        top = d["importance"][0]
        nxt.append(f"refine `{top['name']}` ({top['scope']} maps), the setting with the largest effect, in a narrower box "
                   f"around {top['value']}")
    maybe = [e for e in sw if 0.6 < e["p_pos"] <= 0.75]
    if maybe:
        nxt.append("give the undecided switches more games: " + ", ".join(f"`{e['name']}` ({e['scope']})" for e in maybe[:5]))
    nxt.append("confirm the promoted bot on the ladder for 40+ games before trusting it (local self-play has misled before)")
    for x in nxt:
        L.append(f"- {x}")
    md = pl.OUT / "report.md"
    md.write_text("\n".join(L) + "\n", encoding="utf-8")
    (pl.OUT / "report.json").write_text(json.dumps(d, indent=1, default=str), encoding="utf-8")
    return md


if __name__ == "__main__":
    import argparse
    from . import pipeline as pl
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="results/tune")
    a = ap.parse_args()
    args = argparse.Namespace(base="bots/v22", workers=1, scale=1.0, iterations=0, out=a.out, dry=True)
    print(write(pl.Pipeline(args)))
