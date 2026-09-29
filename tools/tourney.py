"""Local tournament: play two bots against each other across maps and seeds, both sides.

    python tools/tourney.py mybot bots/starter
    python tools/tourney.py mybot bots/v1 --maps devil,portals --seeds 4 -j 12
    python tools/tourney.py mybot bots/v1 --native            # faster, but not priced like the judge
    python tools/tourney.py mybot bots/v1 --replays replays/t  # keep replays of games bot 1 did not win

Each (map, seed) is played twice with the sides swapped, since team A moves first.
By default bots run in the judge's sandbox (--sandbox), so CPU points and crashes match the ladder.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures as futures
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
ANSI = re.compile(r"\x1b\[[0-9;]*m")
WIN = re.compile(r"team (A|B) wins after (\d+) rounds \((.*?)\)")
DRAW = re.compile(r"draw after (\d+) rounds \((.*?)\)")
DEATH = re.compile(r"round (\d+): bot (\d+) \(team (A|B)\) died: (.*)")
POINTS = re.compile(r"team (A|B) points per turn: p50 (\S+)\s+p99 (\S+)\s+mean (\S+)\s+max (\S+)")
BOT_LINE = re.compile(r"round (\S+): bot (\d+) \(team (A|B)\) (.*)")


def tool_python() -> str:
    tool_dir = subprocess.run(["uv", "tool", "dir"], capture_output=True, text=True, check=True).stdout.strip()
    for rel in ("unswbc/Scripts/python.exe", "unswbc/bin/python"):
        candidate = pathlib.Path(tool_dir) / rel
        if candidate.is_file():
            return str(candidate)
    sys.exit("cannot find the unswbc tool's python; install it with `uv tool install unswbc`")


def build(bot: str, sandbox: bool) -> str:
    """Build once up front so parallel games never race to compile."""
    if sandbox:
        code = ("import pathlib, sys\nfrom unswbc import clangtool\n"
                "print(clangtool.build(pathlib.Path(sys.argv[1])))")
    else:
        code = ("import pathlib, sys\nfrom unswbc.project import Project\n"
                "out = Project.from_dir(sys.argv[1]).compile()\n"
                "print(next(p for p in (out / 'bot.exe', out / 'bot') if p.is_file()))")
    done = subprocess.run([tool_python(), "-c", code, bot], capture_output=True, text=True, cwd=ROOT)
    if done.returncode != 0:
        sys.exit(f"build of {bot} failed:\n{done.stdout}\n{done.stderr}")
    return done.stdout.strip().splitlines()[-1]


def points(text: str) -> float:
    return float(text[:-1]) * 1e6 if text.endswith("M") else float(text.replace(",", ""))


PYTHON = None


def play(job: dict) -> dict:
    cmd = [PYTHON, str(ROOT / "tools" / "match.py"), job["map"], job["a"], job["b"], str(job["seed"])]
    if not job["sandbox"]:
        cmd.append("--native")
    if job["replay"]:
        cmd += ["--replay", job["replay"]]
    env = dict(os.environ, UNSWBC_NO_UPDATE="1", NO_COLOR="1")
    started = time.monotonic()
    done = subprocess.run(cmd, capture_output=True, text=True, env=env, cwd=ROOT, errors="replace")
    out = ANSI.sub("", done.stdout + done.stderr)
    result = {**job, "winner": None, "rounds": 0, "reason": "", "deaths": [], "points": {}, "errors": [],
              "seconds": time.monotonic() - started, "ok": False, "final": None}
    for line in out.splitlines():
        if line.startswith("RESULT "):
            result["final"] = json.loads(line[7:])
        elif m := WIN.search(line):
            result.update(winner=m[1], rounds=int(m[2]), reason=m[3], ok=True)
        elif m := DRAW.search(line):
            result.update(winner="draw", rounds=int(m[1]), reason=m[2], ok=True)
        elif m := DEATH.search(line):
            result["deaths"].append((m[3], m[4]))
        elif m := POINTS.search(line):
            result["points"][m[1]] = {"p99": points(m[3]), "max": points(m[5])}
        elif (m := BOT_LINE.search(line)) and "stdout" not in m[4] and "stderr" not in m[4]:
            result["errors"].append((m[3], m[4].strip()))
    if not result["ok"]:
        result["errors"].append(("?", out.strip()[-400:]))
    return result


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bot1")
    ap.add_argument("bot2")
    ap.add_argument("--maps", help="comma-separated map names (default: every map in the maps folder)")
    ap.add_argument("--maps-dir", default="maps", help="folder of .map files (e.g. maps_swapped: team spawns swapped)")
    ap.add_argument("--seeds", type=int, default=2, help="seeds per map (each played both ways)")
    ap.add_argument("--seed-base", type=int, default=1000)
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--native", action="store_true", help="run native binaries instead of the judge sandbox")
    ap.add_argument("--replays", help="keep replays of games bot1 did not win in this folder")
    ap.add_argument("--no-longest", action="store_true", help="skip reading each game's longest dragon from its replay")
    ap.add_argument("--early", action="store_true", help="also report rounds 50/150 (slow: decodes whole replays)")
    ap.add_argument("--keep-replays", action="store_true", help="keep every replay (see tools/replay_summary.py)")
    ap.add_argument("--json", help="write every game's result to this file")
    args = ap.parse_args()

    global PYTHON
    PYTHON = tool_python()
    sandbox = not args.native
    maps_dir = ROOT / args.maps_dir
    names = [m.strip() for m in args.maps.split(",")] if args.maps else sorted(p.stem for p in maps_dir.glob("*.map"))
    maps = [str(maps_dir / f"{n}.map") for n in names]
    for m in maps:
        if not pathlib.Path(m).is_file():
            sys.exit(f"no such map: {m}")

    print(f"building {args.bot1} and {args.bot2} ({'judge sandbox' if sandbox else 'native'})...", flush=True)
    bin1, bin2 = build(args.bot1, sandbox), build(args.bot2, sandbox)
    if bin1 == bin2 and args.bot1 != args.bot2:
        # Identical sources produce one cached binary; that is fine, the labels still differ.
        pass

    replay_tmp = None
    if args.replays:
        replay_tmp = ROOT / args.replays / "_tmp"
    elif not args.no_longest:
        # One folder per run, so runs side by side do not delete each other's replays.
        replay_tmp = ROOT / "results" / f"tourney_tmp_{os.getpid()}"
    if replay_tmp:
        shutil.rmtree(replay_tmp, ignore_errors=True)
        replay_tmp.mkdir(parents=True, exist_ok=True)

    jobs = []
    for path, name in zip(maps, names):
        for s in range(args.seeds):
            seed = args.seed_base + s
            for swap in (False, True):
                a, b = (bin2, bin1) if swap else (bin1, bin2)
                tag = f"{name}-s{seed}-{'bot2A' if swap else 'bot1A'}"
                jobs.append({"map": path, "name": name, "seed": seed, "a": a, "b": b, "bot1_team": "B" if swap else "A",
                             "sandbox": sandbox, "tag": tag,
                             "replay": str(replay_tmp / f"{tag}.replay") if replay_tmp else None})

    print(f"{len(jobs)} games on {len(maps)} maps with {args.jobs} workers", flush=True)
    started = time.monotonic()
    results = []
    live = sys.stdout.isatty()
    with futures.ThreadPoolExecutor(args.jobs) as pool:
        for i, r in enumerate(pool.map(play, jobs), 1):
            results.append(r)
            if live:
                mark = "?" if not r["ok"] else ("W" if r["winner"] == r["bot1_team"] else "D" if r["winner"] == "draw" else "L")
                print(f"\r[{i}/{len(jobs)}] {r['tag']}: {mark} ({r['rounds']} rounds, {r['reason']})".ljust(80),
                      end="" if i < len(jobs) else "\n", flush=True)

    # The engine reports total length only; the replay knows each team's longest dragon.
    if replay_tmp and not args.no_longest:
        files = [r["replay"] for r in results if r["ok"] and r["replay"] and pathlib.Path(r["replay"]).is_file()]
        longest = {}
        # Fast pass: each replay's result section only.
        done = subprocess.run(["node", str(ROOT / "tools" / "replay_results.cjs"), str(replay_tmp)],
                              capture_output=True, text=True, cwd=ROOT)
        if done.returncode == 0:
            for item in json.loads(done.stdout):
                if "error" not in item:
                    longest[item["file"]] = {"teamA": {"longestDragon": item["A"]["longest"]},
                                             "teamB": {"longestDragon": item["B"]["longest"]}}
        for i in range(0, len(files) if args.early else 0, 40):
            chunk = files[i:i + 40]
            done = subprocess.run(["node", str(ROOT / "tools" / "replay.cjs"), "--json", "--result-only",
                                   "--at", "50,150", *chunk],
                                  capture_output=True, text=True, cwd=ROOT)
            if done.returncode == 0:
                for item in json.loads(done.stdout):
                    longest[item["file"]] = dict(item["result"], at=item.get("at", {}))
            else:
                print(f"replay decoding failed: {done.stderr.strip()[:300]}")
                break
        for r in results:
            res = longest.get(pathlib.Path(r["replay"]).name) if r.get("replay") else None
            if res:
                r["longest"] = {"A": res["teamA"]["longestDragon"], "B": res["teamB"]["longestDragon"]}
                r["early"] = res.get("at", {})

    # Tally from bot1's point of view.
    per_map = collections.OrderedDict((n, collections.Counter()) for n in names)
    deaths = {1: collections.Counter(), 2: collections.Counter()}
    worst_points = {1: 0.0, 2: 0.0}
    errors = {1: collections.Counter(), 2: collections.Counter()}
    lengths = {1: [], 2: []}
    dragons = {1: [], 2: []}
    longs = {1: [], 2: []}
    early = {k: {r: {"alive": [], "total": []} for r in ("50", "150")} for k in (1, 2)}
    reasons = collections.Counter()
    for r in results:
        team1 = r["bot1_team"]
        team_of = {team1: 1, ("B" if team1 == "A" else "A"): 2}
        if r.get("longest"):
            for team, k in team_of.items():
                longs[k].append(r["longest"][team])
        for rnd, row in (r.get("early") or {}).items():
            if rnd in ("50", "150"):
                for team, k in team_of.items():
                    early[k][rnd]["alive"].append(row[team]["alive"])
                    early[k][rnd]["total"].append(row[team]["total"])
        if r["ok"]:
            won = "W" if r["winner"] == team1 else "D" if r["winner"] == "draw" else "L"
            reasons[(won, "elim" if "elimination" in r["reason"] else "length")] += 1
        if r.get("final"):
            f = r["final"]
            for team, k in team_of.items():
                lengths[k].append(f["a_length" if team == "A" else "b_length"])
                dragons[k].append(f["a_dragons" if team == "A" else "b_dragons"])
        if not r["ok"]:
            per_map[r["name"]]["?"] += 1
        elif r["winner"] == "draw":
            per_map[r["name"]]["D"] += 1
        else:
            per_map[r["name"]]["W" if r["winner"] == team1 else "L"] += 1
        for team, reason in r["deaths"]:
            deaths[team_of[team]][reason] += 1
        for team, p in r["points"].items():
            worst_points[team_of[team]] = max(worst_points[team_of[team]], p["max"])
        for team, msg in r["errors"]:
            key = 1 if team_of.get(team) == 1 else 2 if team in team_of else 0
            errors.setdefault(key, collections.Counter())[re.sub(r"\d+", "#", msg)[:90]] += 1

    total = collections.Counter()
    print(f"\n{'map':<18}{'W':>4}{'D':>4}{'L':>4}   score")
    for name, c in per_map.items():
        total.update(c)
        games = c["W"] + c["D"] + c["L"]
        score = (c["W"] + 0.5 * c["D"]) / games if games else 0
        flag = "" if not c["?"] else f"  ({c['?']} failed)"
        print(f"{name:<18}{c['W']:>4}{c['D']:>4}{c['L']:>4}   {score:5.0%}{flag}")
    games = total["W"] + total["D"] + total["L"]
    score = (total["W"] + 0.5 * total["D"]) / games if games else 0
    print(f"{'TOTAL':<18}{total['W']:>4}{total['D']:>4}{total['L']:>4}   {score:5.0%}   "
          f"({args.bot1} vs {args.bot2}, {time.monotonic() - started:.0f}s)")

    if reasons:
        print("bot1 results by how games ended: " + ", ".join(f"{w} {how} {n}" for (w, how), n in sorted(reasons.items())))
    for k, label in ((1, args.bot1), (2, args.bot2)):
        parts = []
        for rnd in ("50", "150"):
            e = early[k][rnd]
            if e["alive"]:
                parts.append(f"R{rnd}: {sum(e['alive']) / len(e['alive']):.1f} dragons, "
                             f"total {sum(e['total']) / len(e['total']):.1f}")
        if parts:
            print(f"early game {label}: " + "   ".join(parts))
    for k, label in ((1, args.bot1), (2, args.bot2)):
        if longs[k]:
            print(f"longest dragon at the end {label}: mean {sum(longs[k]) / len(longs[k]):.1f}")
    for k, label in ((1, args.bot1), (2, args.bot2)):
        if lengths[k]:
            print(f"final total length {label}: mean {sum(lengths[k]) / len(lengths[k]):.1f}   "
                  f"surviving dragons: mean {sum(dragons[k]) / len(dragons[k]):.1f}")
    for k, label in ((1, args.bot1), (2, args.bot2)):
        top = ", ".join(f"{reason} {n}" for reason, n in deaths[k].most_common())
        print(f"deaths {label}: {top or 'none'}")
        if sandbox:
            warn = "  <-- close to the 100M limit" if worst_points[k] > 70e6 else ""
            print(f"max points/turn {label}: {worst_points[k] / 1e6:.1f}M{warn}")
    for k, counter in errors.items():
        for msg, n in counter.most_common(5):
            print(f"errors ({'bot1' if k == 1 else 'bot2' if k == 2 else 'engine'}) x{n}: {msg}")

    if replay_tmp and args.keep_replays:
        print(f"replays kept in {replay_tmp} (python tools/replay_summary.py {replay_tmp} BOT1 BOT2)")
    elif replay_tmp and not args.replays:
        shutil.rmtree(replay_tmp, ignore_errors=True)
    elif replay_tmp:
        kept = 0
        for r in results:
            if r["ok"] and r["winner"] != r["bot1_team"] and r["replay"] and pathlib.Path(r["replay"]).is_file():
                shutil.move(r["replay"], replay_tmp.parent / pathlib.Path(r["replay"]).name)
                kept += 1
        shutil.rmtree(replay_tmp, ignore_errors=True)
        print(f"kept {kept} replays of non-wins in {replay_tmp.parent}")

    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(results, indent=1))


if __name__ == "__main__":
    main()
