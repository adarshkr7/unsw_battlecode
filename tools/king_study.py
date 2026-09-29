"""How do the teams that beat us grow and protect their king? Compare both sides' kings.

For every replay given (ladder games we lost at the round limit, by default), per side:
  growth      rounds at which the longest dragon first reaches 8 / 15 / 25
  food        where the pearls eaten by the side's longest dragon came from (spawned, dropped by an
              own-team death = feeding, dropped by an enemy death)
  feeding     own-turn deaths of the side's other dragons from round 200, and how many of them
              died within 3 steps of the king's head
  safety      from round 250: nearest enemy head to the king (median), share of checks with an enemy
              head within 3, own heads within 4 of the king (escort), the king's distance from its
              team's starting area vs the enemy's

    python tools/king_study.py [--rows results/ladder_fs1.json results/ladder_fs1_recent4.json]
"""

from __future__ import annotations

import argparse
import collections
import json
import multiprocessing
import os
import statistics

from replay_decode import load

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def wrapd(a, b, w, h):
    dx, dy = abs(a[0] - b[0]), abs(a[1] - b[1])
    return min(dx, w - dx) + min(dy, h - dy)


def study(args):
    path, us = args
    rep = load(path)
    lines = rep["map"].splitlines()
    w, h = map(int, lines[0].split()[1:3])
    dl = [l for l in lines if l.startswith("DRAGON ")]
    team = {i: "AB"[int(l.split()[1])] for i, l in enumerate(dl)}
    length = {i: int(l.split()[2]) for i, l in enumerate(dl)}
    head = {i: (int(l.split()[3]), int(l.split()[4])) for i, l in enumerate(dl)}
    home = {t: [head[i] for i in head if team[i] == t] for t in "AB"}
    alive = set(team)
    S = {t: {"reach": {}, "food": collections.Counter(), "feed_deaths": 0, "feed_near": 0,
             "enemy_near": [], "enemy_within3": 0, "checks": 0, "escort": [], "home_bias": []} for t in "AB"}
    origin = {}               # tile -> "spawn" | "own:A" | "own:B"
    turn_deaths = []
    rnd, cur = 0, None

    def king(t):
        cands = [(length[i], -i) for i in alive if team[i] == t]
        return -max(cands)[1] if cands else None

    def close_turn():
        turn_deaths.clear()

    for kind, f in rep["events"]:
        if kind == "roundStart":
            rnd = f["round"]
            for t in "AB":
                k = king(t)
                if k is None:
                    continue
                L = length[k]
                for thr in (8, 15, 25):
                    if L >= thr and thr not in S[t]["reach"]:
                        S[t]["reach"][thr] = rnd
                if rnd >= 250 and rnd % 5 == 0 and k in head:
                    o = "B" if t == "A" else "A"
                    kh = head[k]
                    ds = [wrapd(head[i], kh, w, h) for i in alive if team[i] == o and i in head]
                    if ds:
                        S[t]["enemy_near"].append(min(ds))
                        S[t]["enemy_within3"] += min(ds) <= 3
                    S[t]["checks"] += 1
                    S[t]["escort"].append(sum(1 for i in alive if team[i] == t and i != k and i in head and wrapd(head[i], kh, w, h) <= 4))
                    dh = min(wrapd(kh, p, w, h) for p in home[t]) if home[t] else 0
                    de = min(wrapd(kh, p, w, h) for p in home[o]) if home[o] else 0
                    S[t]["home_bias"].append(dh - de)   # negative: nearer our own start than theirs
        elif kind == "turnStart":
            close_turn()
            cur = f["id"]
        elif kind == "dragonUpdate" and f.get("head"):
            head[f["id"]] = f["head"]
        elif kind == "dragonAction" and f["id"] in alive and "move" in f and len(f["move"]) > 1:
            length[f["id"]] -= len(f["move"]) - 1
        elif kind == "tileChange":
            t = tuple(f["tile"])
            if f["hasPearl"]:
                origin[t] = ("own:" + turn_deaths[-1][1]) if turn_deaths else "spawn"
            else:
                if cur in alive:
                    length[cur] += 1
                    side = team[cur]
                    if cur == king(side) and rnd >= 200:
                        src = origin.get(t, "spawn")
                        label = "spawn" if src == "spawn" else ("own team death" if src[4:] == side else "enemy death")
                        S[side]["food"][label] += 1
                origin.pop(t, None)
        elif kind == "dragonSplit":
            team[f["child"]] = f["team"]
            length[f["parent"]] = len(f["parentBody"])
            length[f["child"]] = len(f["childBody"])
            head[f["child"]] = tuple(f["childBody"][0])
            alive.add(f["child"])
        elif kind == "dragonDeath":
            d = f["id"]
            if d in alive:
                t = team[d]
                turn_deaths.append((d, t))
                if rnd >= 200 and cur == d:
                    k = king(t)
                    if k is not None and k != d:
                        S[t]["feed_deaths"] += 1
                        if d in head and k in head and wrapd(head[d], head[k], w, h) <= 3:
                            S[t]["feed_near"] += 1
            alive.discard(d)
    them = "B" if us == "A" else "A"
    res = rep["result"]
    return {"us": S[us], "them": S[them], "final": (res[us]["longest"], res[them]["longest"])}


def summarize(name, rs):
    n = len(rs)
    print(f"  {name}")
    for thr in (8, 15, 25):
        vals = [r["reach"][thr] for r in rs if thr in r["reach"]]
        print(f"     longest reaches {thr:>2}: in {len(vals)}/{n} games, median round {statistics.median(vals) if vals else '-'}")
    food = collections.Counter()
    for r in rs:
        food.update(r["food"])
    tot = sum(food.values()) or 1
    print(f"     king's food from r200: " + ", ".join(f"{k} {100 * v / tot:.0f}%" for k, v in food.most_common()) + f" ({tot / n:.0f} pearls a game)")
    fd = sum(r["feed_deaths"] for r in rs) / n
    fn = sum(r["feed_near"] for r in rs) / n
    print(f"     own-turn deaths of other dragons from r200: {fd:.0f} a game, {fn:.0f} of them within 3 of the king")
    en = [x for r in rs for x in r["enemy_near"]]
    ch = sum(r["checks"] for r in rs) or 1
    es = [x for r in rs for x in r["escort"]]
    hb = [x for r in rs for x in r["home_bias"]]
    print(f"     from r250: nearest enemy head median {statistics.median(en) if en else '-'} steps, enemy within 3 in "
          f"{100 * sum(r['enemy_within3'] for r in rs) / ch:.0f}% of checks, own heads within 4 (escort) median "
          f"{statistics.median(es) if es else '-'}, king nearer own start than enemy's in "
          f"{100 * sum(1 for x in hb if x < 0) / max(1, len(hb)):.0f}% of checks")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rows", nargs="+", default=[os.path.join(ROOT, "results", "ladder_fs1.json"),
                                                   os.path.join(ROOT, "results", "ladder_fs1_recent4.json")])
    ap.add_argument("--all", action="store_true", help="all games, not only round-limit losses")
    args = ap.parse_args()
    jobs, seen = [], set()
    for f in args.rows:
        for r in json.load(open(f)):
            p = os.path.join(ROOT, "replays", "ladder_fs1", f"{r['battle']}-{r['game']}.replay")
            if p in seen or not os.path.exists(p) or r["won"] is None:
                continue
            seen.add(p)
            jobs.append((p, r["we_are"], r["won"], r["opponent"], r["map"]))
    with multiprocessing.Pool(12) as pool:
        out = pool.map(study, [(p, us) for p, us, *_ in jobs])
    rows = []
    for (p, us, won, opp, m), o in zip(jobs, out):
        went500 = o["final"][0] > 0 and o["final"][1] > 0
        if args.all or (not won and went500):
            rows.append((opp, m, o))
    print(f"{len(rows)} games (" + ("all" if args.all else "our round-limit losses") + ")")
    for opp, m, o in rows:
        print(f"   {opp[:22]:22} {m:18} final longest us {o['final'][0]:>2} them {o['final'][1]:>2} | "
              f"their 15 at r{o['them']['reach'].get(15, '-')}, ours r{o['us']['reach'].get(15, '-')}")
    summarize("THEIR king", [o["them"] for _, _, o in rows])
    summarize("OUR king", [o["us"] for _, _, o in rows])


if __name__ == "__main__":
    main()
