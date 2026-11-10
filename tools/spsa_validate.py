"""Test the tuner's averaged values against a frozen bot (default: v19 as submitted) on new seeds.

    python tools/spsa_check.py --step 100 [--out results/spsa] [--base results/tmp_bots/v19_tune_base]
                               [--ref results/tmp_bots/v19_live] [--maps-dir maps_official] [--maps a,b]
                               [--heldout-dir maps_heldout] [--heldout-maps a,b] [--seeds 5] [--jobs 16]

The candidate is the base bot with <out>/params_avg.hpp (the average of theta over the second half of the
steps so far). It plays the reference on the tuning maps and on the held-out official maps (not on the
ladder, never used for tuning), both sides of every seed, in the judge sandbox. Seeds start at
90000 + 50 * step, far from the tuner's (20000 + 13 * step). Never generated maps.

tools/spsa.py --check-at 100,200 runs this by itself at those steps while the tuner waits.
Writes <out>/checks/step<K>.json and .txt (the summary), step<K>_params.hpp (the values tested),
step<K>_<set>.txt and _games.json (tourney output), step<K>_<set>/ (replays of games the candidate did
not win), and <out>/checks/status.json while it runs.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]


def copy_bot(src: pathlib.Path, dest: pathlib.Path, params: pathlib.Path | None) -> None:
    shutil.rmtree(dest, ignore_errors=True)
    dest.mkdir(parents=True)
    for f in src.iterdir():
        if f.is_file() and f.suffix in (".cpp", ".hpp", ".toml"):
            shutil.copy(f, dest / f.name)  # fresh mtimes, so tourney.py rebuilds instead of reusing a cached wasm
    if params:
        shutil.copy(params, dest / "params.hpp")
    text = (dest / "params.hpp").read_text(encoding="utf-8")
    text = text.replace("inline constexpr bool USE_MAP_DATA = true;", "inline constexpr bool USE_MAP_DATA = false;")
    (dest / "params.hpp").write_text(text, encoding="utf-8")
    if "inline constexpr bool DEBUG = false;" not in text:
        sys.exit(f"DEBUG is on in {dest}")


def play(cand: pathlib.Path, ref: pathlib.Path, maps_dir: str, maps: str | None, seeds: int, seed_base: int,
         jobs: int, checks: pathlib.Path, tag: str) -> list[dict]:
    games_file = checks / f"{tag}_games.json"
    cmd = [sys.executable, str(ROOT / "tools" / "run_tournament.py"), str(cand.relative_to(ROOT)), str(ref.relative_to(ROOT)),
           "--maps-dir", maps_dir, "--seeds", str(seeds), "--seed-base", str(seed_base), "-j", str(jobs),
           "--replays", str((checks / tag).relative_to(ROOT)), "--json", str(games_file)]
    if maps:
        cmd += ["--maps", maps]
    for attempt in range(2):
        games_file.unlink(missing_ok=True)
        with (checks / f"{tag}.txt").open("w", encoding="utf-8") as f:
            subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT, text=True, errors="replace")
        if games_file.is_file():
            return json.loads(games_file.read_text(encoding="utf-8"))
        time.sleep(30)
    return []


def tally(games: list[dict]) -> dict:
    """The candidate's results (it is bot 1 in every game)."""
    ok = [g for g in games if g.get("ok")]
    pts = [1.0 if g["winner"] == g["bot1_team"] else 0.5 if g["winner"] == "draw" else 0.0 for g in ok]
    n = len(pts)
    w, d = pts.count(1.0), pts.count(0.5)
    out = {"games": n, "won": w, "drawn": d, "lost": n - w - d, "failed": len(games) - n,
           "score": None, "low": None, "high": None, "verdict": "no games finished"}
    if n:
        # 95% range of the score (Wilson interval: stays inside 0-100% and is honest for few games;
        # it treats a draw like half a win and half a loss, which slightly widens the range)
        p, z2 = sum(pts) / n, 1.96 ** 2
        mid = (p + z2 / (2 * n)) / (1 + z2 / n)
        half = math.sqrt(z2 * p * (1 - p) / n + z2 * z2 / (4 * n * n)) / (1 + z2 / n)
        low, high = max(0.0, mid - half), min(1.0, mid + half)
        out.update(score=p, low=low, high=high,
                   verdict="tuned bot better" if low > 0.5 else "tuned bot worse" if high < 0.5
                   else "no clear difference")
    per_map: dict[str, list[int]] = {}
    for g, p in zip(ok, pts):
        per_map.setdefault(g["name"], [0, 0, 0])[0 if p == 1.0 else 1 if p == 0.5 else 2] += 1
    out["perMap"] = per_map
    side = {"cand": [], "ref": []}
    longest = {"cand": [], "ref": []}
    for g in ok:
        other = "B" if g["bot1_team"] == "A" else "A"
        if g.get("final"):
            for who, team in (("cand", g["bot1_team"]), ("ref", other)):
                side[who].append(g["final"]["a_length" if team == "A" else "b_length"])
        if g.get("longest"):
            longest["cand"].append(g["longest"][g["bot1_team"]])
            longest["ref"].append(g["longest"][other])
    out["finalLength"] = {k: (sum(v) / len(v) if v else None) for k, v in side.items()}
    out["longest"] = {k: (sum(v) / len(v) if v else None) for k, v in longest.items()}
    msgs = [m for g in games for _, m in (g.get("errors") or [])]
    out["gamesWithErrors"] = sum(1 for g in games if g.get("errors"))
    out["ranOutOfTime"] = sum(1 for m in msgs if "ran out" in m)
    return out


def line(name: str, t: dict) -> str:
    if not t["games"]:
        return f"{name}: no games finished ({t['failed']} failed)"
    return (f"{name}: {t['won']}-{t['drawn']}-{t['lost']} of {t['games']}, score {t['score']:.1%} "
            f"(95% range {t['low']:.1%} to {t['high']:.1%}): {t['verdict']}"
            + (f"; {t['failed']} failed" if t["failed"] else "")
            + (f"; {t['ranOutOfTime']} ran-out-of-time messages" if t["ranOutOfTime"] else ""))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--step", type=int, required=True, help="the tuning step reached (names the outputs)")
    ap.add_argument("--out", default="results/spsa")
    ap.add_argument("--base", default="results/tmp_bots/v19_tune_base")
    ap.add_argument("--ref", default="results/tmp_bots/v19_live")
    ap.add_argument("--maps-dir", default="maps_official")
    ap.add_argument("--maps", default=None)
    ap.add_argument("--heldout-dir", default="maps_heldout")
    ap.add_argument("--heldout-maps", default=None)
    ap.add_argument("--seeds", type=int, default=5)
    ap.add_argument("--jobs", type=int, default=16)
    args = ap.parse_args()
    for d in (args.maps_dir, args.heldout_dir):
        if "worldgen" in d or "maps_gen" in d:
            sys.exit("generated maps are not allowed")

    out = (ROOT / args.out).resolve()
    checks = out / "checks"
    checks.mkdir(parents=True, exist_ok=True)
    k = args.step
    avg_file = out / "params_avg.hpp"
    if not avg_file.is_file():
        sys.exit(f"no {avg_file}: nothing to test yet")
    shutil.copy(avg_file, checks / f"step{k}_params.hpp")
    values = json.loads((out / "avg.json").read_text(encoding="utf-8")) if (out / "avg.json").is_file() else {}
    cand = ROOT / "results" / "tmp_bots" / f"{out.name}_check{k}_cand"
    ref = ROOT / "results" / "tmp_bots" / f"{out.name}_check{k}_ref"
    copy_bot((ROOT / args.base).resolve(), cand, checks / f"step{k}_params.hpp")
    copy_bot((ROOT / args.ref).resolve(), ref, None)

    seed_base = 90000 + 50 * k
    started = time.time()
    sets = [("tuning maps", "tuning", args.maps_dir, args.maps), ("held-out maps", "heldout", args.heldout_dir, args.heldout_maps)]
    results = []
    all_games: list[dict] = []
    status = checks / "status.json"  # what the tuning page shows while this runs (it reads the replays for live results)
    plan = []
    for label, tag, maps_dir, maps in sets:
        n_maps = len(maps.split(",")) if maps else len(list((ROOT / maps_dir).glob("*.map")))
        plan.append({"name": label, "tag": tag, "maps": n_maps, "games": n_maps * args.seeds * 2})
    try:
        for (label, tag, maps_dir, maps), p in zip(sets, plan):
            n_maps = p["maps"]
            status.write_text(json.dumps({"step": k, "phase": label, "tag": tag, "maps": n_maps, "games": p["games"],
                                          "plan": plan, "reference": args.ref, "started": started, "phaseStarted": time.time(),
                                          "pid": os.getpid()}),
                              encoding="utf-8")
            games = play(cand, ref, maps_dir, maps, args.seeds, seed_base, args.jobs, checks, f"step{k}_{tag}")
            t = tally(games)
            t.update(name=label, mapsDir=maps_dir, maps=n_maps)
            results.append(t)
            all_games += games
            print(line(label, t), flush=True)
    finally:
        status.unlink(missing_ok=True)
    total = tally(all_games)
    total.update(name="all maps")
    summary = {"step": k, "finished": time.time(), "minutes": round((time.time() - started) / 60, 1),
               "candidate": cand.relative_to(ROOT).as_posix(), "reference": args.ref, "seedBase": seed_base,
               "seeds": args.seeds, "sets": results, "all": total, "values": values}
    (checks / f"step{k}.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    text = [f"Step {k}: averaged values ({cand.relative_to(ROOT).as_posix()}) vs {args.ref}, seeds {seed_base}+{args.seeds}, "
            f"both sides, {summary['minutes']} min"]
    for t in results + [total]:
        text.append(line(t["name"], t))
        fl, lg = t["finalLength"], t["longest"]
        if fl["cand"] is not None:
            text.append(f"  final total length {fl['cand']:.1f} v {fl['ref']:.1f}"
                        + (f", longest dragon {lg['cand']:.1f} v {lg['ref']:.1f}" if lg["cand"] is not None else ""))
    for t in results:
        text.append(f"{t['name']} by map (won-drawn-lost):")
        text += [f"  {m:<20}{c[0]}-{c[1]}-{c[2]}" for m, c in sorted(t["perMap"].items())]
    (checks / f"step{k}.txt").write_text("\n".join(text) + "\n", encoding="utf-8")
    print(line("all maps", total), flush=True)


if __name__ == "__main__":
    main()
