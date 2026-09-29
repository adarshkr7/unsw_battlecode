"""What do fast swarms do that we don't? And what do head-on trades buy?

From our ladder replays (tools/ladder_history.py --replays), per side:
  trades       head-to-head trades by who started them, even/favourable/unfavourable for the
               starter, and who ate the pearls the two dead dragons dropped (within 20 rounds)
  growth       first split, dragons and pearls eaten at r25/50/100/150
  spread       median distance from each head to the nearest ally head at r50 and r100
  portals      first portal jump, jumps before r100
  deaths       early deaths (before r150) on own turn by cause, and killed on others' turns
Grouped by opponent (the top fast swarms vs the rest).

    PYTHONIOENCODING=utf-8 python tools/fastswarm_study.py [replays/ladder]
"""

from __future__ import annotations

import collections
import glob
import json
import multiprocessing
import os
import pathlib
import statistics
import sys

from replay_decode import load

ROOT = pathlib.Path(__file__).resolve().parent.parent
FAST = {"cheji bt", "Team SKKU", "food", "RustyHamsters", "Bhole Chature", "Weneedmoretokens"}
CHECK = (25, 50, 100, 150)


def wrapd(a, b, w, h):
    dx, dy = abs(a[0] - b[0]), abs(a[1] - b[1])
    return min(dx, w - dx) + min(dy, h - dy)


def study(path):
    try:
        rep = load(path)
    except Exception:
        return None
    ours = {str(s["id"]) for s in json.loads((ROOT / "results" / "submissions.json").read_text())}
    us = "A" if rep["botA"] in ours else "B" if rep["botB"] in ours else None
    if us is None:
        return None
    them = "B" if us == "A" else "A"
    lines = rep["map"].splitlines()
    w, h = map(int, lines[0].split()[1:3])
    dl = [l for l in lines if l.startswith("DRAGON ")]
    team = {i: "AB"[int(l.split()[1])] for i, l in enumerate(dl)}
    length = {i: int(l.split()[2]) for i, l in enumerate(dl)}
    head = {i: (int(l.split()[3]), int(l.split()[4])) for i, l in enumerate(dl)}
    alive = set(length)
    S = {t: {"trades_started": collections.Counter(), "trades_suffered": 0, "trade_pearls_dropped": 0,
             "trade_pearls_eaten": 0, "first_split": None, "n": {}, "eaten_at": {}, "eaten": 0,
             "spread": {}, "first_portal": None, "portal100": 0, "early_own": collections.Counter(),
             "early_killed": 0, "enemy_trade_pearls_eaten": 0, "trade_len_lost": 0} for t in "AB"}
    rnd, cur = 0, None
    turn_deaths = []            # deaths in the current turn: (id, team, len, reason)
    drop_owner = {}             # tile -> (round, trade starter team) for pearls dropped by a trade
    turn_drops = []             # pearls that appeared during the current turn (a death drops them at once)

    def close_turn():
        h2h = [d for d in turn_deaths if d[3] == "head-to-head"]
        if len(h2h) == 2 and h2h[0][1] != h2h[1][1]:
            mover = next((d for d in h2h if d[0] == cur), None)
            if mover is not None:
                victim = h2h[0] if h2h[1] is mover else h2h[1]
                diff = victim[2] - mover[2]
                cls = "even" if diff == 0 else ("favourable" if diff > 0 else "unfavourable")
                phase = "early" if rnd < 150 else "mid" if rnd < 380 else "late"
                S[mover[1]]["trades_started"][(phase, cls)] += 1
                S[victim[1]]["trades_suffered"] += 1
                S[mover[1]]["trade_len_lost"] += mover[2]
                S[victim[1]]["trade_len_lost"] += victim[2]
                for tile in turn_drops:
                    drop_owner[tile] = (rnd, mover[1])
                S[mover[1]]["trade_pearls_dropped"] += len(turn_drops)
        turn_deaths.clear()
        turn_drops.clear()

    for kind, f in rep["events"]:
        if kind == "roundStart":
            rnd = f["round"]
            for t in "AB":
                for c in CHECK:
                    if rnd == c:
                        S[t]["n"][c] = sum(1 for i in alive if team[i] == t)
                        S[t]["eaten_at"][c] = S[t]["eaten"]
                if rnd in (50, 100):
                    hs = [head[i] for i in alive if team[i] == t and i in head]
                    nn = []
                    for a in hs:
                        ds = [wrapd(a, b, w, h) for b in hs if b is not a]
                        if ds:
                            nn.append(min(ds))
                    S[t]["spread"][rnd] = statistics.median(nn) if nn else None
        elif kind == "turnStart":
            close_turn()
            cur = f["id"]
        elif kind == "dragonUpdate" and f.get("head"):
            d = f["id"]
            if d in head and cur == d and d in alive:
                if wrapd(head[d], f["head"], w, h) > 7:  # longer than any move: a portal
                    t = team[d]
                    S[t]["first_portal"] = S[t]["first_portal"] if S[t]["first_portal"] is not None else rnd
                    if rnd < 100:
                        S[t]["portal100"] += 1
            head[d] = f["head"]
        elif kind == "dragonAction" and f["id"] in alive and "move" in f and len(f["move"]) > 1:
            length[f["id"]] -= len(f["move"]) - 1
        elif kind == "tileChange":
            tile = f["tile"]
            if f["hasPearl"]:
                turn_drops.append(tile)
            elif cur in alive:
                t = team[cur]
                length[cur] += 1
                S[t]["eaten"] += 1
                if tile in drop_owner:
                    r0, starter = drop_owner.pop(tile)
                    if rnd - r0 <= 20:
                        S[t]["trade_pearls_eaten"] += 1
                        if starter != t:
                            S[t]["enemy_trade_pearls_eaten"] += 1
        elif kind == "dragonSplit":
            p, c = f["parent"], f["child"]
            team[c] = f["team"]
            length[p], length[c] = len(f["parentBody"]), len(f["childBody"])
            head[c] = tuple(f["childBody"][0]) if f["childBody"] else None
            alive.add(c)
            t = team[p]
            if S[t]["first_split"] is None:
                S[t]["first_split"] = rnd
        elif kind == "dragonDeath":
            d = f["id"]
            if d in alive:
                t = team[d]
                turn_deaths.append((d, t, length[d], f["reason"]))
                if rnd < 150:
                    if cur == d:
                        S[t]["early_own"][f["reason"]] += 1
                    else:
                        S[t]["early_killed"] += 1
                alive.discard(d)
    close_turn()
    # pearls dropped by trades that were never eaten within 20 rounds are simply not counted
    won = (rep["result"] or {}).get("winner") == us
    return {"file": os.path.basename(path), "won": won, "map": lines[2][9:] if len(lines) > 2 else "?",
            "us": S[us], "them": S[them]}


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "replays" / "ladder")
    files = sorted(glob.glob(os.path.join(folder, "*.replay")))
    with multiprocessing.Pool(16) as pool:
        rows = [r for r in pool.map(study, files) if r]
    hist = json.loads((ROOT / "results" / "ladder_history.json").read_text())
    opp = {f"{r['battle']}-{r['game']}.replay": r["opponent"] for r in hist}
    for r in rows:
        r["opponent"] = opp.get(r["file"], "?")
    groups = [("TOP FAST SWARMS (" + ", ".join(sorted(FAST)) + ")", [r for r in rows if r["opponent"] in FAST]),
              ("ALL OTHER OPPONENTS", [r for r in rows if r["opponent"] not in FAST])]
    for title, rs in groups:
        if not rs:
            continue
        print(f"===== {title}: {len(rs)} games, we won {sum(r['won'] for r in rs)}")
        for side in ("us", "them"):
            X = [r[side] for r in rs]
            def m(vals):
                vals = [v for v in vals if v is not None]
                return statistics.mean(vals) if vals else float("nan")
            print(f"  {side.upper()}")
            print("    dragons   " + "  ".join(f"r{c}: {m([x['n'].get(c) for x in X]):5.1f}" for c in CHECK))
            print("    eaten     " + "  ".join(f"r{c}: {m([x['eaten_at'].get(c) for x in X]):5.0f}" for c in CHECK))
            print(f"    first split round {m([x['first_split'] for x in X]):.1f} | nearest-ally distance r50 {m([x['spread'].get(50) for x in X]):.1f}, "
                  f"r100 {m([x['spread'].get(100) for x in X]):.1f}")
            fp = [x["first_portal"] for x in X if x["first_portal"] is not None]
            print(f"    portals: used in {len(fp)}/{len(X)} games, first at round {statistics.median(fp) if fp else '-'} (median), "
                  f"jumps before r100 {m([x['portal100'] for x in X]):.1f}")
            own = collections.Counter()
            for x in X:
                own.update(x["early_own"])
            print(f"    deaths before r150: own turn {', '.join(f'{k} {v / len(X):.1f}' for k, v in own.most_common())}; "
                  f"killed on others' turns {m([x['early_killed'] for x in X]):.1f}")
            ts = collections.Counter()
            for x in X:
                ts.update(x["trades_started"])
            started = sum(ts.values()) / len(X)
            print(f"    trades started {started:.1f}/game: " + ", ".join(
                f"{ph} {cl} {ts[(ph, cl)] / len(X):.1f}" for ph in ("early", "mid", "late") for cl in ("even", "favourable", "unfavourable") if ts[(ph, cl)]))
            print(f"    length lost in trades {m([x['trade_len_lost'] for x in X]):.1f}/game | pearls dropped by trades this side started {m([x['trade_pearls_dropped'] for x in X]):.1f}/game")
            print(f"    trades suffered {m([x['trades_suffered'] for x in X]):.1f}/game | pearls from trades eaten {m([x['trade_pearls_eaten'] for x in X]):.1f}/game "
                  f"(of which from trades the other side started {m([x['enemy_trade_pearls_eaten'] for x in X]):.1f})")
        print()
    # per opponent, compact
    print("per opponent: games, our wins, their dragons r50/r150 vs ours, trades started us/them, trade pearls eaten us/them")
    by = collections.defaultdict(list)
    for r in rows:
        by[r["opponent"]].append(r)
    for name, rs in sorted(by.items(), key=lambda kv: -len(kv[1])):
        def a(side, f):
            return statistics.mean(f(r[side]) for r in rs)
        print(f"  {name[:24]:24} {len(rs):>3} {sum(r['won'] for r in rs):>3}  "
              f"{a('them', lambda x: x['n'].get(50, 0)):5.1f}/{a('them', lambda x: x['n'].get(150, 0)):5.1f} vs "
              f"{a('us', lambda x: x['n'].get(50, 0)):5.1f}/{a('us', lambda x: x['n'].get(150, 0)):5.1f}  "
              f"trades {a('us', lambda x: sum(x['trades_started'].values())):5.1f}/{a('them', lambda x: sum(x['trades_started'].values())):5.1f}  "
              f"pearls {a('us', lambda x: x['trade_pearls_eaten']):5.1f}/{a('them', lambda x: x['trade_pearls_eaten']):5.1f}")


if __name__ == "__main__":
    main()
