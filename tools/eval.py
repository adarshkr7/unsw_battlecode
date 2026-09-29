"""Win rate of a candidate bot against a frozen oracle.

Plays every map x seed twice (candidate as team A, then as team B, same seed) in the
judge's sandbox, so CPU points are priced like the ladder and timeouts show up as deaths.
Score: win = 1, draw = 0.5, loss = 0. Prints win % with a 95% confidence interval, a
per-map breakdown, and appends the run to results/history.csv.

    python tools/eval.py --quick                         # fast check: 12 fast maps x 1 seed x 2 sides
    python tools/eval.py                                 # full: mybot vs bots/oracle, all maps, 3 seeds
    python tools/eval.py --seeds 5 --jobs 16
    python tools/eval.py --maps arena trauma --replays   # keep replays in replays/eval-<stamp>/
    python tools/eval.py bots/v2d bots/oracle            # any two bot folders
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import math
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

ROOT = pathlib.Path(__file__).resolve().parents[1]
# Every map whose game takes under ~20s (timed with seed 1, sandboxed). Left out: big_empty,
# schooltime and slithery_fight, the three big maps at 40s+ a game. Trauma, portals and
# dilemma are in: those are the maps v2c is weakest on.
QUICK_MAPS = ["arena", "Colosseum", "default_small", "default", "dilemma", "devil", "trophy",
              "queen_of_spades", "portals", "trauma", "stronghold", "autarky"]
ANSI = re.compile(r"\x1b\[[0-9;]*m")
WIN = re.compile(r"team ([AB]) wins after (\d+) rounds \(([^)]*)\)")
DRAW = re.compile(r"draw after (\d+) rounds \(([^)]*)\)")
DEATH = re.compile(r"died: (.+)$")
# params::CONSOLIDATE_ROUND: "no valid action" deaths from here on are mostly deliberate feeding,
# before it they are crashes, timeouts or bad sprints.
FEED_ROUND = 380
# Named map sets. The ladder now plays official maps with secret, re-rolled pearl spawns, and
# the contest will use unseen maps: "blind" is the suite to judge changes on.
SUITES = {"blind": ["maps_variants", "maps_world"], "official": ["maps"],
          # 21 blind maps spread over small/mid/big, both kinds: a 42-game first look (about 2.5 min)
          "quick": ["maps_quick"]}
POINTS = re.compile(r"team ([AB]) points per turn: .*max ([\d.]+)M")


def unswbc_python() -> pathlib.Path | None:
    """The interpreter uv installed unswbc into, so we can call its build step directly."""
    try:
        tool_dir = subprocess.run(["uv", "tool", "dir"], capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None
    for rel in ("unswbc/Scripts/python.exe", "unswbc/bin/python"):
        exe = pathlib.Path(tool_dir) / rel
        if exe.is_file():
            return exe
    return None


BUILD_WASM = "import pathlib,sys; from unswbc import clangtool; print(clangtool.build(pathlib.Path(sys.argv[1])))"
BUILD_NATIVE = ("import pathlib,sys; from unswbc.project import Project; d = Project.from_dir(sys.argv[1]).compile();"
                " print(next(p for p in (d / 'bot.exe', d / 'bot') if p.is_file()))")


def prebuild(bot: pathlib.Path, sandbox: bool) -> str:
    """Build a C/C++ bot once (sandbox: wasm, native: bot.exe), so parallel games don't race to
    build it. Falls back to the folder (unswbc builds it, cached by source hash) if that fails."""
    py = unswbc_python()
    if py is not None:
        proc = subprocess.run([str(py), "-c", BUILD_WASM if sandbox else BUILD_NATIVE, str(bot)],
                              capture_output=True, text=True)
        built = proc.stdout.strip().splitlines()[-1] if proc.stdout.strip() else ""
        if proc.returncode == 0 and built and pathlib.Path(built).is_file():
            return built
        sys.stderr.write(f"prebuild of {bot} failed, letting unswbc build it:\n{proc.stderr[-2000:]}\n")
    return str(bot)


def play(unswbc: str, map_path: pathlib.Path, bot_a: str, bot_b: str, seed: int, replay: pathlib.Path | None,
         sandbox: bool, timeout: float | None, retries: int = 2) -> dict:
    """One game. A sandbox that fails to start (it happens under load) is retried. The replay is
    always written (to a temp file unless kept) and read back for pearls eaten and final lengths."""
    keep = replay is not None
    if not keep:
        fd, tmp = tempfile.mkstemp(suffix=".replay")
        os.close(fd)
        replay = pathlib.Path(tmp)
    try:
        for _ in range(retries + 1):
            r = _play_once(unswbc, map_path, bot_a, bot_b, seed, replay, sandbox, timeout)
            if not (r["error"] and "failed to start" in r["error"]):
                break
            time.sleep(2)
        r["stats"] = None
        if r["winner"] not in (None, "timeout") and replay.is_file() and replay.stat().st_size > 0:
            proc = subprocess.run([sys.executable, str(ROOT / "tools" / "replay_decode.py"), "--stats", str(replay)],
                                  capture_output=True, text=True)
            if proc.returncode == 0:
                r["stats"] = json.loads(proc.stdout)
            proc = subprocess.run([sys.executable, str(ROOT / "tools" / "replay_analyze.py"), "--growth", str(replay)],
                                  capture_output=True, text=True)
            if proc.returncode == 0 and r["stats"] is not None:
                r["stats"]["growth"] = json.loads(proc.stdout)
        return r
    finally:
        if not keep:
            replay.unlink(missing_ok=True)


def _play_once(unswbc: str, map_path: pathlib.Path, bot_a: str, bot_b: str, seed: int, replay: pathlib.Path | None,
               sandbox: bool, timeout: float | None) -> dict:
    cmd = [unswbc, "run", str(map_path), bot_a, bot_b, "--seed", str(seed), "--no-debug"]
    cmd += ["--sandbox"] if sandbox else []
    cmd += ["-o", str(replay)] if replay else ["--no-replay"]
    started = time.monotonic()
    result = {"winner": None, "rounds": None, "reason": None, "error": None,
              "no_action": {"A": 0, "B": 0}, "no_action_late": {"A": 0, "B": 0}, "max_points": {}, "secs": None}
    try:
        proc = subprocess.run(cmd, capture_output=True, cwd=ROOT, timeout=timeout)
    except subprocess.TimeoutExpired:
        result.update(winner="timeout", secs=round(time.monotonic() - started, 1))
        return result
    result["secs"] = round(time.monotonic() - started, 1)
    out = ANSI.sub("", proc.stdout.decode(errors="replace") + proc.stderr.decode(errors="replace"))
    for line in out.splitlines():
        if m := WIN.search(line):
            result.update(winner=m[1], rounds=int(m[2]), reason=m[3])
        elif m := DRAW.search(line):
            result.update(winner="draw", rounds=int(m[1]), reason=m[2])
        elif (m := DEATH.search(line)) and m[1].strip() == "no valid action":
            who = re.search(r"round (\d+): .*\(team ([AB])\)", line)
            if who:
                # From FEED_ROUND on, v2c dies on purpose (an illegal SPLIT 1) to feed its king.
                key = "no_action_late" if int(who[1]) >= FEED_ROUND else "no_action"
                result[key][who[2]] += 1
        elif m := POINTS.search(line):
            result["max_points"][m[1]] = float(m[2])
    if result["winner"] is None:
        result["error"] = out.strip()[-1500:] or f"exit code {proc.returncode}"
    return result


def wilson(score: float, n: int, z: float = 1.96) -> tuple[float, float]:
    if n == 0:
        return 0.0, 0.0
    p = score / n
    denom = 1 + z * z / n
    centre = (p + z * z / (2 * n)) / denom
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / denom
    return centre - half, centre + half


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("candidate", nargs="?", default="mybot")
    ap.add_argument("oracle", nargs="?", default="bots/oracle")
    ap.add_argument("--quick", action="store_true",
                    help=f"fast check: {len(QUICK_MAPS)} fast maps x 1 seed x 2 sides, all at once, 120s cap per game")
    ap.add_argument("--maps", nargs="*", help="map names (default: every maps/*.map)")
    ap.add_argument("--map-dir", nargs="+", default=["maps"],
                    help="folder(s) of .map files (maps_swapped: the same maps with the teams' starting sides swapped, as the ladder often plays them)")
    ap.add_argument("--suite", choices=sorted(SUITES),
                    help="named map set: 'blind' = ladder-like variants (official walls, re-rolled pearls) + generated worlds, "
                         "1 seed; 'official' = maps/ (our bots know their pearls there, the ladder hides them)")
    ap.add_argument("--min-area", type=int, default=0, help="only maps of at least this many tiles (big maps: 900)")
    ap.add_argument("--seeds", type=int, default=None, help="seeds per map, each played from both sides (default 3)")
    ap.add_argument("--seed-base", type=int, default=1, help="first seed (seeds are base..base+n-1)")
    ap.add_argument("--jobs", type=int, default=None, help="games at once (default: half the cores)")
    ap.add_argument("--timeout", type=float, default=None,
                    help="kill a game after this many seconds; it is reported and left out of the score")
    ap.add_argument("--no-sandbox", action="store_true",
                    help="run native binaries: no CPU-point pricing, so timeouts go unnoticed")
    ap.add_argument("--replays", action="store_true", help="keep every replay under replays/eval-<stamp>/")
    ap.add_argument("--early-stop", type=int, default=0, metavar="N",
                    help="after N scored games, stop starting new ones once the 95%% interval excludes 50%%")
    ap.add_argument("--label", default="", help="note stored with this run in results/history.csv")
    args = ap.parse_args()
    if args.suite:
        args.map_dir = SUITES[args.suite]
        args.seeds = args.seeds or 1
    if args.quick:
        args.maps = args.maps or QUICK_MAPS
        args.seeds = args.seeds or 1
        args.timeout = args.timeout or 120
    args.seeds = args.seeds or 3
    sandbox = not args.no_sandbox

    unswbc = shutil.which("unswbc")
    if not unswbc:
        sys.exit("unswbc not found: `uv tool install unswbc`")
    cand_dir, orac_dir = ROOT / args.candidate, ROOT / args.oracle
    for d in (cand_dir, orac_dir):
        if not d.is_dir():
            sys.exit(f"not a bot folder: {d}")
    dirs = [ROOT / d for d in args.map_dir]
    if args.maps:
        maps = [next((d / (m if m.endswith(".map") else m + ".map") for d in dirs
                      if (d / (m if m.endswith(".map") else m + ".map")).is_file()),
                     dirs[0] / (m if m.endswith(".map") else m + ".map")) for m in args.maps]
    else:
        maps = [m for d in dirs for m in sorted(d.glob("*.map"))]
    if args.min_area:
        def area(m):
            w, h = map(int, m.read_text().splitlines()[0].split()[1:3])
            return w * h
        maps = [m for m in maps if area(m) >= args.min_area]
    for m in maps:
        if not m.is_file():
            sys.exit(f"no such map: {m}")

    print(f"building {args.candidate} and {args.oracle} ({'sandbox wasm' if sandbox else 'native'}) ...", flush=True)
    cand, orac = prebuild(cand_dir, sandbox), prebuild(orac_dir, sandbox)

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    replay_dir = ROOT / "replays" / f"eval-{stamp}{'-' + args.label if args.label else ''}" if args.replays else None
    if replay_dir:
        replay_dir.mkdir(parents=True, exist_ok=True)

    games = []
    for m in maps:
        for s in range(args.seed_base, args.seed_base + args.seeds):
            for cand_team in ("A", "B"):
                games.append((m, s, cand_team))
    # Slowest first: a big map started last keeps every worker waiting (a 100-game run spent
    # about a quarter of its time on that tail). Cost = mean seconds in past runs, else map area.
    past: dict[str, list[float]] = {}
    for f in sorted((ROOT / "results").glob("eval-*.json"))[-60:]:
        try:
            for g in json.loads(f.read_text()).get("games", []):
                if g.get("secs"):
                    past.setdefault(g["map"], []).append(g["secs"])
        except (OSError, ValueError):
            pass

    def cost(m: pathlib.Path) -> float:
        if m.stem in past:
            return sum(past[m.stem]) / len(past[m.stem])
        try:
            w, h = map(int, m.read_text().splitlines()[0].split()[1:3])
            return w * h / 40.0
        except (OSError, ValueError):
            return 0.0
    games.sort(key=lambda g: -cost(g[0]))
    cores = os.cpu_count() or 2
    jobs = args.jobs or (min(len(games), cores) if args.quick else max(1, cores // 2))
    print(f"{len(games)} games: {len(maps)} maps x {args.seeds} seeds x 2 sides, {jobs} at a time", flush=True)

    rows = []
    started = time.monotonic()
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {}
        for m, s, cand_team in games:
            a, b = (cand, orac) if cand_team == "A" else (orac, cand)
            replay = replay_dir / f"{m.stem}-s{s}-cand{cand_team}.replay" if replay_dir else None
            futures[pool.submit(play, unswbc, m, a, b, s, replay, sandbox, args.timeout)] = (m.stem, s, cand_team)
        stopped = False
        for i, fut in enumerate(as_completed(futures), 1):
            if fut.cancelled():
                continue
            map_name, s, cand_team = futures[fut]
            r = fut.result()
            orac_team = "B" if cand_team == "A" else "A"
            if r["winner"] == "timeout":
                outcome, score = "timeout", None
            elif r["winner"] is None:
                outcome, score = "error", None
            elif r["winner"] == "draw":
                outcome, score = "draw", 0.5
            else:
                outcome, score = ("win", 1.0) if r["winner"] == cand_team else ("loss", 0.0)
            row = {"map": map_name, "seed": s, "cand_team": cand_team, "outcome": outcome, "score": score,
                   "rounds": r["rounds"], "reason": r["reason"],
                   "cand_no_action": r["no_action"][cand_team], "orac_no_action": r["no_action"][orac_team],
                   "cand_no_action_late": r["no_action_late"][cand_team],
                   "cand_max_points_M": r["max_points"].get(cand_team), "secs": r["secs"], "error": r["error"]}
            st = r.get("stats")
            if st and st["result"]:
                row.update(cand_eaten=st["eaten"][cand_team], orac_eaten=st["eaten"][orac_team],
                           cand_longest=st["result"][cand_team]["longest"],
                           orac_longest=st["result"][orac_team]["longest"])
            if st and st.get("growth"):
                row.update(cand_growth=st["growth"][cand_team], orac_growth=st["growth"][orac_team])
            rows.append(row)
            tag = {"win": "W", "loss": "L", "draw": "D", "error": "!", "timeout": "T"}[outcome]
            print(f"[{i:>3}/{len(games)}] {tag} {map_name:<16} seed {s} cand={cand_team} "
                  f"{outcome:<5} r{r['rounds']} ({r['reason']}) {r['secs']}s", flush=True)
            if outcome == "error":
                print("      " + (r["error"] or "").replace("\n", "\n      "), flush=True)
            if args.early_stop and not stopped:
                done = [x["score"] for x in rows if x["score"] is not None]
                if len(done) >= args.early_stop:
                    lo, hi = wilson(sum(done), len(done))
                    if lo > 0.5 or hi < 0.5:
                        stopped = True
                        cancelled = sum(f.cancel() for f in futures)
                        print(f"early stop after {len(done)} games: {100 * sum(done) / len(done):.1f}% "
                              f"(95% CI {100 * lo:.1f}-{100 * hi:.1f}%), {cancelled} games not started", flush=True)

    # ---- summary
    scored = [r for r in rows if r["score"] is not None]
    n, total = len(scored), sum(r["score"] for r in scored)
    wins = sum(r["outcome"] == "win" for r in rows)
    losses = sum(r["outcome"] == "loss" for r in rows)
    draws = sum(r["outcome"] == "draw" for r in rows)
    errors = sum(r["outcome"] == "error" for r in rows)
    timeouts = sum(r["outcome"] == "timeout" for r in rows)
    lo, hi = wilson(total, n)
    pct = 100 * total / n if n else 0.0

    # Continuous measures, which move before wins do: pearls eaten per game, and the final
    # longest dragon in games that went the distance (eliminations end with 0 for the loser).
    def avg(xs):
        xs = [x for x in xs if x is not None]
        return sum(xs) / len(xs) if xs else float("nan")

    print("\nper map: score | pearls eaten per game (cand vs oracle) | final longest at round 500 (cand vs oracle)")
    for m in maps:
        mr = [r for r in scored if r["map"] == m.stem]
        if mr:
            ms = sum(r["score"] for r in mr)
            full = [r for r in mr if r["reason"] == "on length"]
            longest = (f"{avg(r.get('cand_longest') for r in full):5.1f} vs {avg(r.get('orac_longest') for r in full):5.1f}"
                       if full else "   (eliminations)")
            print(f"  {m.stem:<16} {100 * ms / len(mr):5.1f}% ({ms:g}/{len(mr)}) | "
                  f"{avg(r.get('cand_eaten') for r in mr):6.0f} vs {avg(r.get('orac_eaten') for r in mr):6.0f} | {longest}")
    full = [r for r in scored if r["reason"] == "on length"]
    eat_c, eat_o = avg(r.get("cand_eaten") for r in scored), avg(r.get("orac_eaten") for r in scored)
    long_c, long_o = avg(r.get("cand_longest") for r in full), avg(r.get("orac_longest") for r in full)
    # Early growth (the ~2000-rated teams field 2-5x our dragons by round 50-150 and grow a king early).
    grown = [r for r in scored if r.get("cand_growth")]
    if grown:
        def g(side, key):
            return avg(r[f"{side}_growth"][key] for r in grown)
        parts = []
        for rr in (50, 100, 150):
            parts.append(f"r{rr}: dragons {g('cand', f'n{rr}'):.1f} vs {g('orac', f'n{rr}'):.1f}, "
                         f"eaten {g('cand', f'eat{rr}'):.0f} vs {g('orac', f'eat{rr}'):.0f}, "
                         f"splits {g('cand', f'split{rr}'):.0f} vs {g('orac', f'split{rr}'):.0f}")
        pad = "\n" + " " * 31
        growth_line = ("early growth (cand vs oracle)  " + pad.join(parts)
                       + f"{pad}longest r150 {g('cand', 'long150'):.1f} vs {g('orac', 'long150'):.1f}, "
                         f"r300 {g('cand', 'long300'):.1f} vs {g('orac', 'long300'):.1f}"
                       + (f"{pad}length lost by 10+ dragons (not feeding) {g('cand', 'big_lost'):.0f} vs {g('orac', 'big_lost'):.0f} per game"
                          if all("big_lost" in r["cand_growth"] for r in grown) else ""))
    else:
        growth_line = None
    no_action = sum(r["cand_no_action"] for r in rows)
    no_action_late = sum(r["cand_no_action_late"] for r in rows)
    worst_pts = max((r["cand_max_points_M"] or 0) for r in rows) if rows else 0
    print(f"\n{args.candidate} vs {args.oracle}: {wins}W {losses}L {draws}D"
          + (f" {errors} errors" if errors else "")
          + (f" {timeouts} over the {args.timeout:g}s cap (not scored)" if timeouts else ""))
    print(f"WIN % = {pct:.1f}%   (95% CI {100 * lo:.1f}-{100 * hi:.1f}%, n={n})")
    print(f"pearls eaten per game: {eat_c:.0f} vs {eat_o:.0f} ({100 * (eat_c / eat_o - 1):+.0f}%)   "
          f"final longest (games to round 500, n={len(full)}): {long_c:.1f} vs {long_o:.1f} ({long_c - long_o:+.1f})")
    if growth_line:
        print(growth_line)
    print(f"candidate 'no valid action' deaths before round {FEED_ROUND} (crashes/timeouts): {no_action}"
          + ("   <-- investigate" if no_action else "")
          + f"   from round {FEED_ROUND} (mostly feeding): {no_action_late}")
    if sandbox:
        print(f"candidate worst turn: {worst_pts}M of 100M points")
    else:
        print("native run: CPU points not checked")
    print(f"took {time.monotonic() - started:.0f}s")

    # ---- persist
    results = ROOT / "results"
    results.mkdir(exist_ok=True)
    (results / f"eval-{stamp}{'-' + args.label if args.label else ''}.json").write_text(json.dumps({
        "candidate": args.candidate, "oracle": args.oracle, "label": args.label, "seeds": args.seeds,
        "mode": "quick" if args.quick else "full", "sandbox": sandbox,
        "eaten": [eat_c, eat_o], "longest_round500": [long_c, long_o],
        "seed_base": args.seed_base, "win_pct": pct, "ci95": [100 * lo, 100 * hi], "games": rows}, indent=1))
    history = results / "history.csv"
    new = not history.exists()
    header = ["time", "mode", "candidate", "oracle", "label", "maps", "seeds", "games", "wins", "losses",
              "draws", "errors", "win_pct", "ci_lo", "ci_hi", "eaten_pct", "longest_diff"]
    if not new and history.read_text().splitlines()[0].split(",") != header:
        history.rename(results / f"history-old-{stamp}.csv")  # column layout changed: start afresh
        new = True
    with history.open("a", newline="") as f:
        w = csv.writer(f)
        if new:
            w.writerow(header)
        w.writerow([stamp, "quick" if args.quick else "full", args.candidate, args.oracle, args.label, len(maps),
                    args.seeds, n, wins, losses, draws, errors + timeouts,
                    f"{pct:.1f}", f"{100 * lo:.1f}", f"{100 * hi:.1f}",
                    f"{100 * (eat_c / eat_o - 1):+.0f}", f"{long_c - long_o:+.1f}"])
    print(f"saved results/eval-{stamp}{'-' + args.label if args.label else ''}.json and appended results/history.csv")
    return 0 if errors == 0 and timeouts == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
