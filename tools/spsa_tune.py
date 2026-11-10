"""SPSA tuning of a bot's parameters on the ladder maps (judge sandbox, no map data, no generated maps).

    python tools/spsa.py [--base results/tmp_bots/v19_live] [--out results/spsa] [--iters 200] [--jobs 16]
                         [--maps-dir maps_ladder3] [--maps a,b] [--seeds 1] [--a0 1.0]

Each iteration draws a random +1/-1 for every tuned parameter and builds two copies of the base bot:
theta+ (each parameter moved +c_k * step) and theta- (moved -c_k * step). They play each other on every
map, both sides, with fresh seeds. The score r = (wins - losses) / games of theta+ then moves every
parameter by lr_k * r * sign * step. This is the method chess engines use to tune evaluation weights
from noisy game results: one gradient estimate per iteration, whatever the number of parameters.

State is saved after every iteration in <out>/state.json; running the same command again resumes.
<out>/log.csv has one row per iteration; <out>/params_avg.hpp is the base params.hpp with the average
of theta over the second half of the iterations so far (the values to test and ship).
Only params.hpp changes between copies, so every copy keeps the base bot's code (and its no-map-data rule).

--check-at 100,200 pauses the tuning when those iterations are done and runs tools/spsa_check.py: the
averaged values play --check-ref (default v19 as submitted) on new seeds, on the tuning maps and the
held-out official maps. Results go to <out>/checks/; a check that already has its result is not rerun.
"""

from __future__ import annotations

import argparse
import csv
import ctypes
import json
import math
import os
import pathlib
import random
import re
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]

# name, type, low, high, step (one +-step perturbation should change play noticeably)
PARAMS = [
    ("THREAT_PER_LEN", float, 10.0, 200.0, 20.0),
    ("THREAT_EVEN", float, 0.0, 80.0, 10.0),
    ("THREAT_DENSITY_PENALTY", float, 0.0, 500.0, 40.0),
    ("TRADE_MIN_UNITS", int, 2, 14, 2.0),
    ("HUNT_ATTRACT", float, 0.0, 250.0, 25.0),
    ("AGGRO_RATIO", float, 1.2, 4.0, 0.4),
    ("EAT_NOW", float, 150.0, 800.0, 60.0),
    ("FOOD_BEST", float, 100.0, 700.0, 50.0),
    ("GAMMA_BEST", float, 0.65, 0.97, 0.04),
    ("ALLY_CLOSER", float, 0.02, 0.8, 0.08),
    ("ENEMY_CLOSER", float, 0.0, 0.6, 0.06),
    ("REVISIT", float, 0.0, 150.0, 15.0),
    ("DIVE_SEG_COST", float, 50.0, 900.0, 80.0),
    ("QUEEN_KEEPAWAY", float, 0.0, 500.0, 40.0),
    ("QUEEN_LONGER_P", float, 0.2, 0.95, 0.1),
    ("QUEEN_SPLIT_UNTIL", int, 0, 200, 20.0),
    ("FEED_ROUND", int, 300, 470, 20.0),
    ("CONSOLIDATE_ROUND", int, 360, 490, 15.0),
    ("FEED_MARGIN", int, 0, 10, 1.5),
    ("KING_APPROACH", float, 5.0, 150.0, 15.0),
    # Two features added on 3 Oct, each with "off" inside its range: a sprint cost of 800 never pays
    # (it costs more than a pearl is worth), and 0% feeders means no early queen feeding.
    ("EAT_SPRINT_COST", float, 100.0, 800.0, 80.0),
    ("EAT_SPRINT_ENEMY_DIST", int, 1, 6, 1.5),
    ("QFEED_ROUND", int, 150, 420, 30.0),
    ("QFEED_PULL_PCT", int, 0, 40, 6.0),
    ("QFEED_MIN_UNITS", int, 8, 40, 5.0),
]
LINE = r"^(inline constexpr [a-z: ]+ {name} = )([^;]+);"


def read_values(params_text: str) -> dict[str, float]:
    out = {}
    for name, typ, lo, hi, step in PARAMS:
        m = re.search(LINE.format(name=name), params_text, re.M)
        if not m:
            sys.exit(f"no parameter {name} in the base params.hpp")
        out[name] = float(m.group(2).strip().rstrip("f"))
    return out


def clamp(values: dict[str, float]) -> dict[str, float]:
    """Keep every parameter in its range and the round parameters in order (theta stays continuous)."""
    out = {name: min(hi, max(lo, values[name])) for name, typ, lo, hi, step in PARAMS}
    out["CONSOLIDATE_ROUND"] = max(out["CONSOLIDATE_ROUND"], out["FEED_ROUND"])
    return out


def legal(values: dict[str, float]) -> dict[str, float]:
    """What a bot is built with: clamped, the ints rounded, the round parameters in order."""
    out = clamp(values)
    for name, typ, lo, hi, step in PARAMS:
        if typ is int:
            out[name] = float(round(out[name]))
    out["CONSOLIDATE_ROUND"] = max(out["CONSOLIDATE_ROUND"], out["FEED_ROUND"])
    return out


def render(params_text: str, values: dict[str, float]) -> str:
    v = legal(values)
    derived = dict(v)
    derived["KING_PULL_ROUND"] = v["FEED_ROUND"]                 # head for the king when feeding starts
    derived["FINAL_MERGE_ROUND"] = v["CONSOLIDATE_ROUND"] + 10   # v15: 440 -> 450
    types = {name: typ for name, typ, *_ in PARAMS}
    types.update(KING_PULL_ROUND=int, FINAL_MERGE_ROUND=int)
    text = params_text
    for name, val in derived.items():
        lit = str(int(val)) if types[name] is int else repr(float(round(val, 6)))
        text, n = re.subn(LINE.format(name=name), lambda m: m.group(1) + lit + ";", text, count=1, flags=re.M)
        if n != 1:
            sys.exit(f"could not set {name}")
    return text


def make_bot(base: pathlib.Path, dest: pathlib.Path, values: dict[str, float]) -> None:
    shutil.rmtree(dest, ignore_errors=True)
    dest.mkdir(parents=True)
    for f in base.iterdir():
        if f.is_file() and f.suffix in (".cpp", ".hpp", ".toml"):
            shutil.copy(f, dest / f.name)
    text = (base / "params.hpp").read_text(encoding="utf-8")
    (dest / "params.hpp").write_text(render(text, values), encoding="utf-8")
    if "inline constexpr bool DEBUG = false;" not in (dest / "params.hpp").read_text(encoding="utf-8"):
        sys.exit("DEBUG is on in the base bot")


def drop_cached(folder: pathlib.Path) -> None:
    cache = pathlib.Path(os.environ.get("LOCALAPPDATA", "")) / "unswbc" / "wasmbots"
    for w in cache.glob(folder.name + "-*.wasm"):
        try:
            w.unlink()
        except OSError:
            pass


def play(plus: pathlib.Path, minus: pathlib.Path, args, seed_base: int, out_json: pathlib.Path) -> list[dict]:
    cmd = [sys.executable, str(ROOT / "tools" / "run_tournament.py"), str(plus.relative_to(ROOT)), str(minus.relative_to(ROOT)),
           "--maps-dir", args.maps_dir, "--seeds", str(args.seeds), "--seed-base", str(seed_base),
           "-j", str(args.jobs), "--no-longest", "--json", str(out_json)]
    if args.maps:
        cmd += ["--maps", args.maps]
    for attempt in range(2):
        done = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
        if out_json.is_file():
            data = json.loads(out_json.read_text(encoding="utf-8"))
            return data if isinstance(data, list) else data.get("games", [])
        print(f"  tourney failed (attempt {attempt + 1}): {done.stdout[-300:]} {done.stderr[-300:]}", flush=True)
        time.sleep(30)
    return []


def run_check(k: int, args, out: pathlib.Path) -> None:
    """Test the averaged values against the reference once, at iteration k (tools/spsa_check.py)."""
    if (out / "checks" / f"step{k}.json").is_file() or not (out / "params_avg.hpp").is_file():
        return
    cmd = [sys.executable, str(ROOT / "tools" / "spsa_validate.py"), "--step", str(k), "--out", args.out,
           "--base", args.base, "--ref", args.check_ref, "--maps-dir", args.maps_dir,
           "--heldout-dir", args.check_heldout, "--seeds", str(args.check_seeds), "--jobs", str(args.jobs)]
    if args.maps:
        cmd += ["--maps", args.maps]
    if args.check_heldout_maps:
        cmd += ["--heldout-maps", args.check_heldout_maps]
    print(f"iteration {k}: testing the averaged values against {args.check_ref}", flush=True)
    try:
        done = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
        (out / "checks").mkdir(parents=True, exist_ok=True)
        (out / "checks" / f"step{k}.log").write_text(done.stdout + done.stderr, encoding="utf-8")
        print(done.stdout.strip() or f"check failed: {done.stderr.strip()[-500:]}", flush=True)
    except Exception as e:  # a failed check must never stop the tuning
        print(f"check at iteration {k} failed: {e!r}", flush=True)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", default="results/tmp_bots/v19_tune_base")
    ap.add_argument("--out", default="results/spsa")
    ap.add_argument("--iters", type=int, default=200)
    ap.add_argument("--jobs", type=int, default=16)
    ap.add_argument("--maps-dir", default="maps_ladder3")
    ap.add_argument("--maps", default=None)
    ap.add_argument("--seeds", type=int, default=1)
    ap.add_argument("--a0", type=float, default=1.0, help="learning rate at the start (in steps per unit of r)")
    ap.add_argument("--A", type=float, default=20.0, help="learning-rate decay offset")
    ap.add_argument("--check-at", default="", help="iterations after which the averaged values play --check-ref, e.g. 100,200")
    ap.add_argument("--check-ref", default="results/tmp_bots/v19_live")
    ap.add_argument("--check-seeds", type=int, default=5, help="seeds per map in a check (both sides)")
    ap.add_argument("--check-heldout", default="maps_heldout", help="official maps never used for tuning")
    ap.add_argument("--check-heldout-maps", default=None)
    args = ap.parse_args()
    if any(bad in d for d in (args.maps_dir, args.check_heldout) for bad in ("worldgen", "maps_gen")):
        sys.exit("generated maps are not allowed for tuning")
    checks = {int(x) for x in args.check_at.split(",") if x.strip()}

    # Keep the machine awake while tuning (ES_CONTINUOUS | ES_SYSTEM_REQUIRED); released on exit.
    if os.name == "nt":
        ctypes.windll.kernel32.SetThreadExecutionState(0x80000000 | 0x00000001)

    base = (ROOT / args.base).resolve()
    out = (ROOT / args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    state_file = out / "state.json"
    base_text = (base / "params.hpp").read_text(encoding="utf-8")
    if state_file.is_file():
        state = json.loads(state_file.read_text(encoding="utf-8"))
        print(f"resuming at iteration {state['k']}", flush=True)
    else:
        start = read_values(base_text)
        state = {"k": 0, "theta": dict(start), "start": dict(start), "history": [], "rng": random.randrange(1 << 30)}
    rng = random.Random(state["rng"] + state["k"])
    log_file = out / "log.csv"
    new_log = not log_file.is_file()
    names = [p[0] for p in PARAMS]
    steps = {p[0]: p[4] for p in PARAMS}
    # For the tuning page: how this run was started.
    (out / "run.json").write_text(json.dumps({"pid": os.getpid(), "started": time.time(), "args": vars(args),
                                              "checkAt": sorted(checks)}, indent=1), encoding="utf-8")

    while state["k"] < args.iters:
        k = state["k"]
        if k in checks:
            run_check(k, args, out)
        ck = 1.0 / (k + 1) ** 0.101
        lr = args.a0 * ((args.A + 1) / (args.A + k + 1)) ** 0.602
        delta = {n: rng.choice((-1, 1)) for n in names}
        theta = state["theta"]
        plus = {n: theta[n] + ck * delta[n] * steps[n] for n in names}
        minus = {n: theta[n] - ck * delta[n] * steps[n] for n in names}
        pdir = ROOT / "results" / "tmp_bots" / f"spsa_p{k}"
        mdir = ROOT / "results" / "tmp_bots" / f"spsa_m{k}"
        make_bot(base, pdir, plus)
        make_bot(base, mdir, minus)
        seed_base = 20000 + 13 * k
        started = time.monotonic()
        games = play(pdir, mdir, args, seed_base, out / "last.json")
        ok = [g for g in games if g.get("ok")]
        w = sum(1 for g in ok if g["winner"] == g["bot1_team"])
        d = sum(1 for g in ok if g["winner"] == "draw")
        l = len(ok) - w - d
        errors = sum(1 for g in games if g.get("errors"))
        if errors or len(ok) < len(games):
            with (out / "errors.log").open("a", encoding="utf-8") as f:
                for g in games:
                    if g.get("errors") or not g.get("ok"):
                        f.write(json.dumps({"iter": k, "map": g.get("name"), "seed": g.get("seed"), "ok": g.get("ok"),
                                            "errors": g.get("errors")[:4] if g.get("errors") else None}) + "\n")
        if not ok:
            print(f"iteration {k}: no games finished; stopping", flush=True)
            break
        r = (w - l) / len(ok)
        for n in names:
            theta[n] += lr * r * delta[n] * steps[n]
        theta.update(clamp(theta))  # continuous: only the built bots are rounded
        state["history"].append({n: theta[n] for n in names})
        state["k"] = k + 1
        state_file.write_text(json.dumps(state, indent=1), encoding="utf-8")
        # Average of theta over the second half of the iterations so far: the values to ship.
        half = state["history"][len(state["history"]) // 2:]
        avg = {n: sum(h[n] for h in half) / len(half) for n in names}
        (out / "params_avg.hpp").write_text(render(base_text, avg), encoding="utf-8")
        (out / "avg.json").write_text(json.dumps(legal(avg), indent=1), encoding="utf-8")
        with log_file.open("a", newline="", encoding="utf-8") as f:
            wr = csv.writer(f)
            if new_log:
                wr.writerow(["iter", "time", "minutes", "games", "w", "d", "l", "errors", "r", "lr", "ck"] + names)
                new_log = False
            wr.writerow([k, time.strftime("%H:%M"), round((time.monotonic() - started) / 60, 1), len(ok), w, d, l, errors,
                         round(r, 3), round(lr, 3), round(ck, 3)] + [round(theta[n], 4) for n in names])
        print(f"iteration {k}: {w}-{d}-{l} of {len(ok)} (errors {errors}), r {r:+.3f}, "
              f"{(time.monotonic() - started) / 60:.1f} min", flush=True)
        for folder in (pdir, mdir):  # keep the disk and the wasm cache small
            drop_cached(folder)
            shutil.rmtree(folder, ignore_errors=True)
    if state["k"] in checks:
        run_check(state["k"], args, out)


if __name__ == "__main__":
    main()
