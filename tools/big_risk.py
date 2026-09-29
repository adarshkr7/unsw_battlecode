"""What gets our big dragons killed in the midgame? A hazard table from replays.

At every turn a dragon of --min-len+ takes between --from and --to, record its situation
(enemy heads near it and whether any can reach it, allies near it, its room, its head's
exits) and whether it was killed head-on before its next turn. Kill rate by situation shows
which situations are deadly and how often we are in them, before any bot change is tried.

    python tools/big_risk.py replays/eval-...-x-mf200-big [more folders] [--min-len 10]
"""

from __future__ import annotations

import argparse
import collections
import glob
import os

from replay_analyze import Geo
from replay_decode import load


def wrapd(a, b, n):
    d = abs(a - b)
    return min(d, n - d)


def bfs_room(geo, start, blocked, cap):
    seen = {start}
    q = [start]
    for t in q:
        if len(seen) >= cap:
            break
        for d in "NESW":
            n = geo.step(t, d)
            if n is None or n in seen or n in blocked:
                continue
            seen.add(n)
            q.append(n)
    return len(seen)


def reach(geo, start, steps, blocked):
    """Tiles a head can land on within `steps` moves through free tiles."""
    seen = {start: 0}
    front = [start]
    out = set()
    while front:
        nxt = []
        for p in front:
            if seen[p] >= steps:
                continue
            for d in "NESW":
                q = geo.step(p, d)
                if q is None or q in seen:
                    continue
                seen[q] = seen[p] + 1
                out.add(q)
                if q not in blocked:
                    nxt.append(q)
        front = nxt
    return out


def study(path, min_len, r0, r1):
    rep = load(path)
    geo = Geo(rep["map"])
    bodies = {i: list(b) for i, (t, b) in enumerate(geo.spawns)}
    team = {i: t for i, (t, b) in enumerate(geo.spawns)}
    alive = set(bodies)
    pearls = set()
    rnd, cur = -1, None
    pending = {}  # dragon id -> features at its last turn start
    rows = []
    for kind, f in rep["events"]:
        if kind == "roundStart":
            rnd = f["round"]
        elif kind == "turnStart":
            cur = f["id"]
            if cur in pending:  # survived until its next turn
                rows.append((pending.pop(cur), False))
            b = bodies.get(cur)
            if cur in alive and b and len(b) >= min_len and r0 <= rnd <= r1:
                pending[cur] = features(geo, bodies, team, alive, cur)
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
            if d in pending:
                killed = f["reason"] == "head-to-head" and cur != d
                feat = pending.pop(d)
                feat["own_death"] = cur == d
                rows.append((feat, killed))
            alive.discard(d)
    return rows


def features(geo, bodies, team, alive, me):
    body = bodies[me]
    head = body[0]
    L = len(body)
    occupied = {t for i in alive for t in bodies[i]}
    enemies, allies = [], []
    for i in alive:
        if i == me:
            continue
        h = bodies[i][0]
        dist = wrapd(h[0], head[0], geo.w) + wrapd(h[1], head[1], geo.h)
        (allies if team[i] == team[me] else enemies).append((dist, len(bodies[i]), h, i))
    # can any enemy head land on a tile next to ours next turn (i.e. threaten every move)?
    around = [n for d in "NESW" if (n := geo.step(head, d)) is not None and n not in occupied]
    threatened = set()
    for dist, el, eh, i in enemies:
        if dist > 8:
            continue
        threatened |= reach(geo, eh, min(6, max(1, el - 1)), occupied - {eh}) & set(around)
    safe_moves = len([n for n in around if n not in threatened])
    # Could a small ally intercept? For each shorter enemy head within 4 of our head: can a
    # small ally (2-5 long) put its head on it next turn, or is one within 6 steps of it?
    hunters = [e for e in enemies if e[0] <= 4 and e[1] < L]
    smalls = [a for a in allies if a[1] <= 5]
    icpt_now = icpt_near = False
    for dist, el, eh, i in hunters:
        for adist, al, ah, j in smalls:
            dd = wrapd(ah[0], eh[0], geo.w) + wrapd(ah[1], eh[1], geo.h)
            if dd <= 6:
                icpt_near = True
            if dd <= max(1, al - 1) and eh in reach(geo, ah, max(1, al - 1), occupied - {eh, ah}):
                icpt_now = True
    near_e = [e for e in enemies if e[0] <= 6]
    return {
        "len": L,
        "exits": len(around),
        "safe_moves": safe_moves,
        "enemies6": len(near_e),
        "small_enemies6": sum(1 for e in near_e if e[1] < L),
        "enemies3": sum(1 for e in enemies if e[0] <= 3),
        "allies6": sum(1 for a in allies if a[0] <= 6),
        "room": bfs_room(geo, head, occupied, 3 * L),
        "hunters4": len(hunters),
        "icpt_now": icpt_now,
        "icpt_near": icpt_near,
    }


def bucket(v, edges):
    for e in edges:
        if v <= e:
            return f"<={e}"
    return f">{edges[-1]}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--min-len", type=int, default=10)
    ap.add_argument("--from", dest="r0", type=int, default=150)
    ap.add_argument("--to", dest="r1", type=int, default=379)
    args = ap.parse_args()
    files = []
    for p in args.paths:
        files += sorted(glob.glob(os.path.join(p, "*.replay"))) if os.path.isdir(p) else [p]
    rows = []
    for f in files:
        rows += study(f, args.min_len, args.r0, args.r1)
    turns = len(rows)
    kills = sum(k for _, k in rows)
    print(f"{len(files)} games, {turns} turns of dragons {args.min_len}+ in rounds {args.r0}-{args.r1}, "
          f"{kills} head-on kills ({100 * kills / max(1, turns):.2f}% per turn)\n")
    tables = [
        ("safe moves (no enemy can reach the tile next turn)", "safe_moves", [0, 1, 2, 3]),
        ("head exits (free neighbours)", "exits", [0, 1, 2, 3]),
        ("enemy heads within 6", "enemies6", [0, 1, 2, 4, 8]),
        ("shorter enemy heads within 6", "small_enemies6", [0, 1, 2, 4]),
        ("enemy heads within 3", "enemies3", [0, 1, 2]),
        ("ally heads within 6", "allies6", [0, 1, 3, 6]),
        ("room (tiles, capped at 3x length) / length", None, None),
        ("a small ally could strike a hunter (within 4 of us) next turn", "icpt_now", [0]),
        ("a small ally within 6 of a hunter", "icpt_near", [0]),
    ]
    for title, key, edges in tables:
        c = collections.defaultdict(lambda: [0, 0])
        for feat, k in rows:
            if key is None:
                b = bucket(feat["room"] / feat["len"], [1, 2, 2.9])
            else:
                b = bucket(feat[key], edges)
            c[b][0] += 1
            c[b][1] += k
        print(title)
        for b, (n, k) in sorted(c.items(), key=lambda kv: (len(kv[0]), kv[0])):
            print(f"   {b:>6}: {n:>6} turns ({100 * n / turns:4.1f}% of time)  {k:>4} kills  "
                  f"kill rate {100 * k / max(1, n):5.2f}%  share of kills {100 * k / max(1, kills):4.1f}%")
        print()


if __name__ == "__main__":
    main()
