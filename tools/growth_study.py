"""How do fast swarms grow? Rounds 0-150 of our ladder replays, per side.

Splits (how many, at what parent length, child size), eating per dragon-round, deaths of
small dragons, and how long dragons sit at a splittable length without splitting. Grouped
by opponent type: "fast" when their dragon count at r150 is 1.3x ours or more.

    python tools/growth_study.py [replays/ladder] [--until 150]
"""

from __future__ import annotations

import argparse
import collections
import glob
import json
import multiprocessing
import os
import pathlib
import statistics

from replay_decode import load

ROOT = pathlib.Path(__file__).resolve().parent.parent
UNTIL = 150


def our_ids():
    return {str(s["id"]) for s in json.loads((ROOT / "results" / "submissions.json").read_text())}


def study(path):
    try:
        rep = load(path)
    except Exception:
        return None
    ours = our_ids()
    us = "A" if rep["botA"] in ours else "B" if rep["botB"] in ours else None
    if us is None:
        return None
    lines = [l for l in rep["map"].splitlines() if l.startswith("DRAGON ")]
    team = {i: "AB"[int(l.split()[1])] for i, l in enumerate(lines)}
    length = {i: int(l.split()[2]) for i, l in enumerate(lines)}
    alive = set(length)
    s = {t: {"splits": 0, "parent_len": [], "child_len": [], "eaten": 0, "dragon_rounds": 0,
             "deaths_small": 0, "deaths": 0, "wait": [], "n150": 0, "sprints": 0} for t in "AB"}
    ready_since = {}  # dragon -> round it first reached length >= 4 without splitting since
    rnd, cur = 0, None
    for kind, f in rep["events"]:
        if kind == "roundStart":
            rnd = f["round"]
            if rnd > UNTIL:
                break
            for i in alive:
                s[team[i]]["dragon_rounds"] += 1
                if length[i] >= 4:
                    ready_since.setdefault(i, rnd)
        elif kind == "turnStart":
            cur = f["id"]
        elif kind == "dragonAction" and f["id"] in alive and "move" in f:
            if len(f["move"]) > 1:
                s[team[f["id"]]]["sprints"] += 1
                length[f["id"]] -= len(f["move"]) - 1
        elif kind == "tileChange" and not f["hasPearl"] and cur in alive:
            length[cur] += 1
            s[team[cur]]["eaten"] += 1
        elif kind == "dragonSplit":
            p, c = f["parent"], f["child"]
            team[c] = f["team"]
            pl, cl = len(f["parentBody"]), len(f["childBody"])
            t = team[p]
            s[t]["splits"] += 1
            s[t]["parent_len"].append(pl + cl)
            s[t]["child_len"].append(cl)
            if p in ready_since:
                s[t]["wait"].append(rnd - ready_since.pop(p))
            length[p], length[c] = pl, cl
            alive.add(c)
        elif kind == "dragonDeath":
            d = f["id"]
            if d in alive:
                s[team[d]]["deaths"] += 1
                if length[d] <= 3:
                    s[team[d]]["deaths_small"] += 1
                alive.discard(d)
                ready_since.pop(d, None)
    for i in alive:
        s[team[i]]["n150"] += 1
    them = "B" if us == "A" else "A"
    won = (rep["result"] or {}).get("winner") == us
    return {"us": s[us], "them": s[them], "won": won, "map": rep["map"].splitlines()[2][9:]}


def main():
    global UNTIL
    ap = argparse.ArgumentParser()
    ap.add_argument("folder", nargs="?", default=str(ROOT / "replays" / "ladder"))
    ap.add_argument("--until", type=int, default=150)
    args = ap.parse_args()
    UNTIL = args.until
    files = sorted(glob.glob(os.path.join(args.folder, "*.replay")))
    with multiprocessing.Pool(16) as pool:
        rows = [r for r in pool.map(study, files) if r]
    groups = {"vs fast swarms": [r for r in rows if r["them"]["n150"] >= 1.3 * max(1, r["us"]["n150"])],
              "vs others": [r for r in rows if r["them"]["n150"] < 1.3 * max(1, r["us"]["n150"])]}
    for g, rs in groups.items():
        if not rs:
            continue
        print(f"=== {g}: {len(rs)} games, we won {sum(r['won'] for r in rs)}")
        for side in ("us", "them"):
            def tot(k):
                return sum(r[side][k] for r in rs)
            pl = [x for r in rs for x in r[side]["parent_len"]]
            cl = [x for r in rs for x in r[side]["child_len"]]
            wt = [x for r in rs for x in r[side]["wait"]]
            dr = tot("dragon_rounds")
            pc = collections.Counter(pl)
            print(f"  {side:4}: dragons at r{UNTIL} {tot('n150') / len(rs):5.1f} | splits {tot('splits') / len(rs):5.1f} a game | "
                  f"eaten {tot('eaten') / len(rs):5.0f} ({1000 * tot('eaten') / max(1, dr):.0f} per 1000 dragon-rounds) | "
                  f"deaths {tot('deaths') / len(rs):4.1f} ({tot('deaths_small') / len(rs):4.1f} of length <=3) | "
                  f"sprints {tot('sprints') / len(rs):5.1f}")
            if pl:
                print(f"        split at length: {', '.join(f'{k}:{100 * v / len(pl):.0f}%' for k, v in sorted(pc.items())[:6])} | "
                      f"child median {statistics.median(cl)} | rounds at 4+ before splitting: median {statistics.median(wt) if wt else '-'}, "
                      f"75% {statistics.quantiles(wt, n=4)[2] if len(wt) > 3 else '-'}")
        print()


if __name__ == "__main__":
    main()
