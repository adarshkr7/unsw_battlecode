"""For every head-on kill of one of our big dragons, was it avoidable?

Rebuilds the board at the victim's last turn and tests each of its four moves (or standing
still after a split):
  - legal: not into kelp or any body (tails included: collisions are checked before moving);
  - safe in truth: no enemy head can reach the tile next turn (a length-L dragon sprints L-1
    steps through free tiles, portals included; capped at 6, the longest killing sprint seen);
  - safe as the bot saw it: only enemies inside the victim's 7x7 view count, with the length
    it could see (whole body if the tail is in view, else the visible segments), sprint capped at 6.

    python tools/kill_audit.py <replay files or folders> [--team A|B|cand|bot-name] [--min-len 10] [--from-round 380]
"""

from __future__ import annotations

import argparse
import collections
import glob
import os
import re

from replay_analyze import DV, Geo, analyze
from replay_decode import load


def in_view(head, t, geo):
    return ((t[0] - head[0] + 3) % geo.w) < 7 and ((t[1] - head[1] + 3) % geo.h) < 7


def reach_tiles(geo, start, steps, blocked):
    """Tiles a head at `start` can end on within `steps` moves through tiles not in `blocked`."""
    seen = {start: 0}
    frontier = [start]
    out = set()
    while frontier:
        nxt = []
        for p in frontier:
            k = seen[p]
            if k >= steps:
                continue
            for d in "NESW":
                q = geo.step(p, d)
                if q is None or q in seen:
                    continue
                seen[q] = k + 1
                out.add(q)
                if q not in blocked:
                    nxt.append(q)  # can end on a body tile (a head) but not pass through it
        frontier = nxt
    return out


def audit(path, our_team, min_len, from_round):
    a = analyze(path, us_name="__none__")
    kills = {}
    for d in a["deaths"]:
        if (d["team"] == our_team and d["round"] >= from_round and d["len"] >= min_len
                and d["on_turn_of"] != d["id"] and d["reason"] == "head-to-head"):
            kills[d["id"]] = d
    if not kills:
        return []
    rep = load(path)
    geo = Geo(rep["map"])
    bodies = {i: list(b) for i, (t, b) in enumerate(geo.spawns)}
    team = {i: t for i, (t, b) in enumerate(geo.spawns)}
    alive = set(bodies)
    pearls = set()
    snap = {}
    rnd, cur = -1, None
    out = []
    for kind, f in rep["events"]:
        if kind == "roundStart":
            rnd = f["round"]
        elif kind == "turnStart":
            cur = f["id"]
            if cur in kills and cur in alive:
                snap[cur] = {"round": rnd, "bodies": {i: list(bodies[i]) for i in alive}, "action": None}
        elif kind == "dragonAction":
            d = f["id"]
            if d in snap and snap[d]["action"] is None and cur == d:
                snap[d]["action"] = f
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
            if d in kills and d in snap:
                out.append(classify(geo, team, our_team, d, kills[d], snap[d]))
            alive.discard(d)
    return out


def classify(geo, team, our_team, vid, death, s):
    bodies = s["bodies"]
    me = bodies[vid]
    head = me[0]
    occupied = {t for b in bodies.values() for t in b}
    enemies = [(i, b) for i, b in bodies.items() if team.get(i) != our_team]
    act = s["action"] or {}
    # candidate destinations: the four steps, or staying put if it split
    options = {}
    for d in "NESW":
        q = geo.step(head, d)
        options[d] = q if (q is not None and q not in occupied) else None
    stayed = "split" in act
    # true threat: any enemy head reaching the tile within len-1 steps
    true_reach = set()
    bot_reach = set()
    for i, b in enemies:
        eh = b[0]
        L = len(b)
        blocked = occupied - {eh}
        true_reach |= reach_tiles(geo, eh, min(6, max(1, L - 1)), blocked)  # no killer sprinted more than 6
        if in_view(head, eh, geo):
            tail_seen = in_view(head, b[-1], geo)
            vis = L if tail_seen else sum(1 for t in b if in_view(head, t, geo))
            bot_reach |= reach_tiles(geo, eh, min(6, max(1, vis - 1)), blocked)
    legal = {d: q for d, q in options.items() if q is not None}
    safe_true = {d for d, q in legal.items() if q not in true_reach}
    safe_bot = {d for d, q in legal.items() if q not in bot_reach}
    chosen = act.get("move", "")[:1] if "move" in act else ("stay" if stayed else "?")
    stay_safe_true = head not in true_reach
    if not legal and not (stayed and stay_safe_true):
        cat = "no legal move (trapped)"
    elif not safe_true and not stay_safe_true:
        cat = "no safe move in truth (unavoidable)"
    elif safe_true & safe_bot:
        if chosen in safe_bot:
            cat = "bot thought its move safe, it wasn't (misjudged)"
        else:
            cat = "a safe move the bot could see existed, it chose another (scoring)"
    else:
        cat = "safe move existed only with full information (threat model)"
    return {"round": death["round"], "len": death["len"], "cat": cat, "chosen": chosen,
            "legal": sorted(legal), "safe_true": sorted(safe_true), "safe_bot": sorted(safe_bot)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--team", default="cand", help="A, B, 'cand' (eval replays: the candidate), or a bot name")
    ap.add_argument("--min-len", type=int, default=10)
    ap.add_argument("--from-round", type=int, default=380)
    args = ap.parse_args()
    files = []
    for p in args.paths:
        files += sorted(glob.glob(os.path.join(p, "*.replay"))) if os.path.isdir(p) else [p]
    cats = collections.Counter()
    examples = collections.defaultdict(list)
    for f in files:
        name = os.path.basename(f)
        if args.team in ("A", "B"):
            t = args.team
        elif args.team == "cand":
            m = re.search(r"cand([AB])", name)
            if not m:
                continue
            t = m[1]
        else:
            r = load(f)
            t = "A" if r["botA"] == args.team else "B" if r["botB"] == args.team else None
            if t is None:
                continue
        for row in audit(f, t, args.min_len, args.from_round):
            cats[row["cat"]] += 1
            if len(examples[row["cat"]]) < 3:
                examples[row["cat"]].append((name, row))
    total = sum(cats.values())
    print(f"{total} head-on kills of our dragons of {args.min_len}+ from round {args.from_round}:")
    for c, n in cats.most_common():
        print(f"  {n:>4}  ({100 * n / max(1, total):.0f}%)  {c}")
        for name, r in examples[c]:
            print(f"          e.g. {name} r{r['round']} len {r['len']} chose {r['chosen']} legal {r['legal']} "
                  f"safe(true) {r['safe_true']} safe(bot view) {r['safe_bot']}")


if __name__ == "__main__":
    main()
