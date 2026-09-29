"""Where does our length go in the endgame? From round 380 (consolidation) to 500, per team:
how fragmented it is at 380, how far the small dragons are from the longest one, how fast
they feed it, and how much length ends up in the final king vs fed, killed or left unfed.

    python tools/endgame_study.py <replay folders> [--min-area 900]
"""

from __future__ import annotations

import argparse
import collections
import glob
import multiprocessing
import os
import re
import statistics

from replay_analyze import Geo
from replay_decode import load

START = int(os.environ.get("EG_START", "380"))  # env var so the worker processes see it too


def wrapd(a, b, g):
    dx, dy = abs(a[0] - b[0]), abs(a[1] - b[1])
    return min(dx, g.w - dx) + min(dy, g.h - dy)


def study(path):
    rep = load(path)
    geo = Geo(rep["map"])
    bodies = {i: list(b) for i, (t, b) in enumerate(geo.spawns)}
    team = {i: t for i, (t, b) in enumerate(geo.spawns)}
    alive = set(bodies)
    pearls = set()
    rnd, cur = -1, None
    snap = None
    out = {t: {"feeds": [], "killed_len": 0, "feed_len": 0} for t in "AB"}
    for kind, f in rep["events"]:
        if kind == "roundStart":
            rnd = f["round"]
            if rnd == START and snap is None:
                snap = {i: list(bodies[i]) for i in alive}
        elif kind == "turnStart":
            cur = f["id"]
        elif kind == "dragonAction":
            d = f["id"]
            if "move" in f and d in alive:
                body = bodies[d]
                for k, ch in enumerate(f["move"]):
                    nxt = geo.step(body[0], ch)
                    if nxt is None or nxt in body or any(nxt in bodies[o] for o in alive if o != d):
                        break
                    body.insert(0, nxt)
                    if nxt in pearls:
                        pearls.discard(nxt)
                    else:
                        body.pop()
                    if k > 0 and body:
                        body.pop()
        elif kind == "tileChange":
            (pearls.add if f["hasPearl"] else pearls.discard)(f["tile"])
        elif kind == "dragonSplit":
            bodies[f["parent"]] = list(f["parentBody"])
            bodies[f["child"]] = list(f["childBody"])
            team[f["child"]] = f["team"]
            alive.add(f["child"])
        elif kind == "dragonDeath":
            d = f["id"]
            if rnd >= START and d in alive:
                t = team[d]
                L = len(bodies[d])
                if f["reason"] == "no valid action" and cur == d:
                    out[t]["feeds"].append(rnd - START)
                    out[t]["feed_len"] += L
                elif cur != d:
                    out[t]["killed_len"] += L
            alive.discard(d)
    if snap is None:
        return None
    res = {}
    for t in "AB":
        mine = sorted(((len(b), i) for i, b in snap.items() if team[i] == t), reverse=True)
        if not mine:
            res[t] = None
            continue
        kl, kid = mine[0]
        khead = snap[kid][0]
        smalls = [snap[i][0] for L, i in mine[1:] if L <= 5]
        dists = [wrapd(h, khead, geo) for h in smalls]
        end = sorted((len(bodies[i]) for i in alive if team[i] == t), reverse=True)
        res[t] = {
            "n380": len(mine), "total380": sum(L for L, _ in mine), "long380": kl,
            "second380": mine[1][0] if len(mine) > 1 else 0,
            "big8_380": sum(1 for L, _ in mine if L >= 8),
            "small_dist": statistics.median(dists) if dists else 0,
            "small_near10": sum(1 for x in dists if x <= 10) / max(1, len(dists)),
            "long_end": end[0] if end else 0, "second_end": end[1] if len(end) > 1 else 0,
            "n_end": len(end), "unfed_end": sum(end[1:]),
            **out[t],
        }
    return geo, res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--min-area", type=int, default=900)
    ap.add_argument("--max-area", type=int, default=10 ** 9)
    args = ap.parse_args()
    files = []
    for p in args.paths:
        files += sorted(glob.glob(os.path.join(p, "*.replay"))) if os.path.isdir(p) else [p]
    rows = {"cand": [], "base": []}
    files = [f for f in files if re.search(r"cand([AB])", os.path.basename(f))]
    # one replay per process: the replay walk is pure Python and slow
    with multiprocessing.Pool(min(16, os.cpu_count() or 2)) as pool:
        results = pool.map(study, files)
    for f, r in zip(files, results):
        m = re.search(r"cand([AB])", os.path.basename(f))
        if r is None:
            continue
        geo, res = r
        if not (args.min_area <= geo.w * geo.h <= args.max_area):
            continue
        ct = m[1]
        for t in "AB":
            if res[t]:
                rows["cand" if t == ct else "base"].append(res[t])
    for side, rs in rows.items():
        if not rs:
            continue
        def avg(k):
            return statistics.mean(r[k] for r in rs)
        feeds = [x for r in rs for x in r["feeds"]]
        print(f"{side}: {len(rs)} team-games (maps of area {args.min_area}+) that reached round {START}")
        print(f"   at {START}: {avg('n380'):.1f} dragons, total length {avg('total380'):.0f}, longest {avg('long380'):.1f}, "
              f"second {avg('second380'):.1f}, dragons of 8+: {avg('big8_380'):.1f}")
        print(f"   small dragons (<=5) to the longest: median distance {avg('small_dist'):.1f} steps, "
              f"{100 * avg('small_near10'):.0f}% within 10")
        if feeds:
            q = statistics.quantiles(feeds, n=4)
            print(f"   feeds: {len(feeds) / len(rs):.1f} a game, length fed {avg('feed_len'):.0f}; rounds after {START}: "
                  f"25% by {q[0]:.0f}, median {q[1]:.0f}, 75% by {q[2]:.0f}")
        print(f"   killed after {START}: length {avg('killed_len'):.0f}; left unfed at the end: {avg('unfed_end'):.0f} "
              f"in {avg('n_end') - 1:.1f} other dragons (second longest {avg('second_end'):.1f})")
        print(f"   final longest {avg('long_end'):.1f} = {100 * avg('long_end') / max(1, avg('total380')):.0f}% of the length at {START}")
        print()


if __name__ == "__main__":
    main()
