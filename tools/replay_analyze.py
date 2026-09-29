"""Replay a .replay move by move and explain the game: lengths over time, every death with
its cause (and who caused it), head trades, splits, pearls eaten, CPU use.

    python tools/replay_analyze.py game.replay [--us 9008] [--every 50] [--deaths]

Our team is the side whose bot name matches --us (ladder replays only name your own bot),
falling back to the side that printed our indicator strings.
"""

from __future__ import annotations

import argparse
import collections
import re

from replay_decode import load

DV = {"N": (0, -1), "E": (1, 0), "S": (0, 1), "W": (-1, 0)}
OUR_INDICATORS = ("swarm split", "escape split", "strike", "feed", "doomed", "trade", "insurance split")


class Geo:
    """Kelp and portals from the map text (same edge layout as mybot/board.hpp)."""

    def __init__(self, text: str):
        self.w = self.h = 0
        self.h_kind, self.v_kind, self.h_pid, self.v_pid = {}, {}, {}, {}
        self.spawns = []
        self.name = ""
        for line in text.split("\n"):
            p = line.split()
            if not p:
                continue
            if p[0] == "MAP":
                self.w, self.h = int(p[1]), int(p[2])
            elif p[0] == "MAP_NAME":
                self.name = line[9:].strip()
            elif p[0] == "EDGE":
                index, kind, pid = int(p[1]), int(p[2]), int(p[3])
                row, col = divmod(index, self.w + 1)
                if row % 2 == 0:
                    y = row // 2
                    if y >= self.h:
                        y -= self.h
                    if col < self.w:
                        self.h_kind[(col, y)], self.h_pid[(col, y)] = kind, pid
                else:
                    y, x = (row - 1) // 2, col
                    if x >= self.w:
                        x -= self.w
                    self.v_kind[(x, y)], self.v_pid[(x, y)] = kind, pid
            elif p[0] == "DRAGON":
                team, n = int(p[1]), int(p[2])
                body = [(int(p[3 + 2 * i]), int(p[4 + 2 * i])) for i in range(n)]
                self.spawns.append(("AB"[team], body))
        self.portal_ends = collections.defaultdict(list)
        for (t, kind) in self.h_kind.items():
            if kind == 2:
                self.portal_ends[self.h_pid[t]].append((True, t))
        for (t, kind) in self.v_kind.items():
            if kind == 2:
                self.portal_ends[self.v_pid[t]].append((False, t))

    def step(self, pos, d):
        """Tile reached leaving pos in direction d, or None for kelp."""
        x, y = pos
        W, H = self.w, self.h
        if d == "N":
            hor, e = True, (x, y)
        elif d == "S":
            hor, e = True, (x, (y + 1) % H)
        elif d == "W":
            hor, e = False, (x, y)
        else:
            hor, e = False, ((x + 1) % W, y)
        kind = (self.h_kind if hor else self.v_kind).get(e, 0)
        if kind == 1:
            return None
        if kind == 2:
            pid = (self.h_pid if hor else self.v_pid)[e]
            for ph, pt in self.portal_ends[pid]:
                if ph == hor and pt == e:
                    continue
                px, py = pt
                if ph:
                    return pt if d == "S" else (px, (py - 1) % H)
                return pt if d == "E" else ((px - 1) % W, py)
        dx, dy = DV[d]
        return ((x + dx) % W, (y + dy) % H)


def analyze(path: str, us_name: str = "9008"):
    rep = load(path)
    geo = Geo(rep["map"])
    bodies, team, alive = {}, {}, set()
    for i, (t, body) in enumerate(geo.spawns):
        bodies[i], team[i] = list(body), t
        alive.add(i)
    pearls = set()
    rnd, cur = -1, None
    cur_ind = {}
    indicators = collections.Counter()
    ind_team = collections.Counter()
    deaths, trades, splits = [], [], collections.Counter()
    eaten = collections.Counter()
    split_log = []      # (round, team)
    timeline = []
    points = collections.defaultdict(list)
    tle = collections.Counter()
    pending_kill = {}  # dragon id -> what the current mover hit
    pearl_src = {}      # tile -> (team, kind) for pearls a death dropped
    feed_eaten = collections.Counter()
    eat_log = []        # (round, team, tile, eater length after eating, source team or None, source dragon length)
    free_exits = {}
    blind_moves = collections.Counter()
    born = {}
    drop_log = {}       # tile -> (round dropped, team, dead dragon length, reason, dead id)
    last_dead = None
    mism = 0
    turn_action = {}

    def occupant(tile, exclude=None):
        for d in alive:
            if d == exclude:
                continue
            b = bodies[d]
            if tile in b:
                return d, b[0] == tile
        return None, False

    def snapshot():
        per = {}
        for t in "AB":
            lens = sorted((len(bodies[d]) for d in alive if team[d] == t), reverse=True)
            per[t] = {"n": len(lens), "longest": lens[0] if lens else 0, "total": sum(lens), "top": lens[:3]}
        timeline.append((rnd, per))

    for kind, f in rep["events"]:
        if kind == "roundStart":
            rnd = f["round"]
            snapshot()
        elif kind == "turnStart":
            cur = f["id"]
            cur_ind.pop(cur, None)
        elif kind == "dragonIndicator":
            txt = f["text"]
            cur_ind[f["id"]] = txt
            key = re.sub(r"\s*\d+$", "", txt)
            indicators[key] += 1
            if f["id"] in team:
                ind_team[(team[f["id"]], key)] += 1
        elif kind == "dragonAction":
            d = f["id"]
            if f.get("points") is not None:
                points[team.get(d, "?")].append(f["points"])
            if f.get("tle"):
                tle[team.get(d, "?")] += 1
            turn_action[d] = f
            if d in alive and bodies.get(d):
                # free exits before acting: passable, not a body (own tail included)
                h = bodies[d][0]
                free = 0
                for dch in "NESW":
                    n = geo.step(h, dch)
                    if n is not None and occupant(n)[0] is None:
                        free += 1
                free_exits[d] = free
            if "move" in f and d in alive:
                body = bodies[d]
                moves = f["move"]
                for k, dch in enumerate(moves):
                    nxt = geo.step(body[0], dch)
                    dx, dy = DV[dch]
                    via_portal = nxt is not None and nxt != ((body[0][0] + dx) % geo.w, (body[0][1] + dy) % geo.h)
                    if via_portal and k == 0 and not (((nxt[0] - body[0][0] + 3) % geo.w) < 7 and ((nxt[1] - body[0][1] + 3) % geo.h) < 7):
                        blind_moves[team[d]] += 1
                    if nxt is None:
                        pending_kill[d] = ("kelp", None, False, len(body), k + 1, len(moves))
                        break
                    # collision is checked before anything moves: own tail included
                    if nxt in body:
                        pending_kill[d] = ("self", d, False, len(body), k + 1, len(moves), via_portal, nxt, body[0], k)
                        break
                    occ, is_head = occupant(nxt, exclude=d)
                    if occ is not None:
                        pending_kill[d] = ("other", occ, is_head, len(body), k + 1, len(moves), via_portal, nxt, body[0], k)
                        if is_head:
                            pending_kill[occ] = ("hit by head", d, True, len(bodies[occ]), 0, 0)
                        break
                    body.insert(0, nxt)
                    if nxt in pearls:
                        pearls.discard(nxt)
                        eaten[team[d]] += 1
                        src = pearl_src.pop(nxt, None)
                        drop = drop_log.pop(nxt, None)
                        eat_log.append((rnd, team[d], nxt, len(body), drop))
                        if src and src[1] == "feed":
                            longest = max((len(bodies[x]) for x in alive if team[x] == team[d]), default=0)
                            who = "enemy" if team[d] != src[0] else ("king" if len(body) >= longest else "other ally")
                            feed_eaten[(src[0], who)] += 1
                    else:
                        body.pop()
                    if k > 0 and len(body) > 0:
                        body.pop()  # each extra sprint step costs a segment
        elif kind == "tileChange":
            if f["hasPearl"]:
                pearls.add(f["tile"])
                if last_dead is not None and last_dead[0] == (rnd, cur):
                    pearl_src[f["tile"]] = (last_dead[1], last_dead[2])
                    drop_log[f["tile"]] = (rnd, last_dead[1], last_dead[3], last_dead[4], last_dead[5])
                else:
                    drop_log.pop(f["tile"], None)
            else:
                pearls.discard(f["tile"])
        elif kind == "dragonUpdate":
            d = f["id"]
            if d in bodies and bodies[d]:
                if bodies[d][0] != f["head"] or bodies[d][-1] != f["tail"]:
                    mism += 1
        elif kind == "dragonSplit":
            p, c = f["parent"], f["child"]
            bodies[p] = list(f["parentBody"])
            bodies[c] = list(f["childBody"])
            team[c] = f["team"]
            born[c] = rnd
            alive.add(c)
            splits[f["team"]] += 1
            split_log.append((rnd, f["team"]))
        elif kind == "dragonDeath":
            d = f["id"]
            info = pending_kill.pop(d, None)
            length = len(bodies.get(d, []))
            ind = cur_ind.get(d, "") if cur == d else ""
            rec = {"round": rnd, "id": d, "team": team.get(d, "?"), "len": info[3] if info else length,
                   "reason": f["reason"], "on_turn_of": cur, "ind": ind, "info": info,
                   "free": free_exits.get(d) if cur == d else None, "age": rnd - born.get(d, 0),
                   "pos": bodies[d][0] if bodies.get(d) else None}
            if info and info[0] in ("other",) and info[1] is not None:
                rec["hit"] = (info[1], team.get(info[1]), len(bodies.get(info[1], [])), info[2])
            if info and info[0] == "hit by head":
                rec["hit"] = (info[1], team.get(info[1]), None, True)
            deaths.append(rec)
            alive.discard(d)
            is_feed = ind.startswith("feed") or (f["reason"] == "no valid action" and rnd >= 380 and cur == d)
            last_dead = ((rnd, cur), rec["team"], "feed" if is_feed else "death",
                         rec["len"], rec["reason"], d)
    snapshot()

    # head-to-head trades: pair deaths in the same turn
    by_turn = collections.defaultdict(list)
    for dth in deaths:
        by_turn[(dth["round"], dth["on_turn_of"])].append(dth)
    for key, ds in by_turn.items():
        h2h = [x for x in ds if x["reason"] == "head-to-head"]
        if len(h2h) == 2:
            mover = next((x for x in h2h if x["id"] == key[1]), h2h[0])
            other = h2h[0] if mover is h2h[1] else h2h[1]
            trades.append({"round": key[0], "mover": (mover["team"], mover["len"], mover["ind"]),
                           "victim": (other["team"], other["len"])})

    res = rep["result"]
    names = {"A": rep["botA"], "B": rep["botB"]}
    us = "A" if names["A"] == us_name else "B" if names["B"] == us_name else None
    if us is None:
        us = max("AB", key=lambda t: sum(v for (tt, _), v in ind_team.items() if tt == t))
    return {"path": path, "map": geo.name, "size": (geo.w, geo.h), "us": us, "result": res, "deaths": deaths,
            "trades": trades, "splits": splits, "eaten": eaten, "timeline": timeline, "points": points,
            "tle": tle, "indicators": ind_team, "mismatches": mism, "feed_eaten": feed_eaten,
            "feed_dropped": sum(1 for v in pearl_src.values() if v[1] == "feed"),
            "eat_log": eat_log, "split_log": split_log, "geo": geo, "blind_moves": blind_moves}


def report(a, every=50, show_deaths=False):
    us, them = a["us"], "B" if a["us"] == "A" else "A"
    res = a["result"]
    won = res["winner"] == us
    print(f"=== {a['path'].split('/')[-1].split(chr(92))[-1]}  {a['map']} {a['size'][0]}x{a['size'][1]}  "
          f"we are {us}:  {'WIN' if won else 'LOSS' if res['winner'] else 'DRAW'} ({res['endReason']})")
    print(f"  final: us {res[us]}   them {res[them]}")
    print(f"  {'round':>5} | {'us: n  longest total  top3':<32} | {'them: n  longest total  top3':<32}")
    for rnd, per in a["timeline"]:
        if rnd % every == 0 or rnd == a["timeline"][-1][0]:
            u, t = per[us], per[them]
            print(f"  {rnd:>5} | {u['n']:>3} {u['longest']:>6} {u['total']:>6}  {str(u['top']):<14} | "
                  f"{t['n']:>3} {t['longest']:>6} {t['total']:>6}  {str(t['top']):<14}")
    print(f"  splits: us {a['splits'][us]}  them {a['splits'][them]}    pearls eaten: us {a['eaten'][us]}  them {a['eaten'][them]}")
    for side in (us, them):
        ds = [d for d in a["deaths"] if d["team"] == side]
        c = collections.Counter(d["reason"] for d in ds)
        print(f"  deaths {'us' if side == us else 'them'}: {len(ds)}  " + ", ".join(f"{k} {v}" for k, v in c.most_common()))
    # our deaths by what we were doing
    ours = [d for d in a["deaths"] if d["team"] == us]
    own_turn = [d for d in ours if d["on_turn_of"] == d["id"]]
    cat = collections.Counter()
    for d in own_turn:
        cat[(d["ind"].split(" ")[0] if d["ind"] else "plain move", d["reason"])] += 1
    print("  our deaths on our own turn, by intent: " + ", ".join(f"{k[0]}->{k[1]} {v}" for k, v in cat.most_common()))
    killed = [d for d in ours if d["on_turn_of"] != d["id"]]
    print(f"  our dragons killed on an enemy's turn: {len(killed)} (lengths {sorted((d['len'] for d in killed), reverse=True)[:12]})")
    # trades
    good = bad = even = 0
    lost_len = gained_len = 0
    for t in a["trades"]:
        mt, ml, _ = t["mover"]
        vt, vl = t["victim"]
        if mt == vt:
            continue
        our_len, their_len = (ml, vl) if mt == us else (vl, ml)
        lost_len += our_len
        gained_len += their_len
        good += their_len > our_len
        bad += their_len < our_len
        even += their_len == our_len
    print(f"  head trades vs enemy: {good + bad + even} (good {good}, even {even}, bad {bad}); "
          f"our length lost {lost_len}, theirs {gained_len}")
    friendly = [d for d in ours if d["on_turn_of"] == d["id"] and d.get("hit") and d["hit"][1] == us]
    if friendly:
        fh = sum(1 for d in friendly if d["hit"][3])
        fp = sum(1 for d in friendly if d["info"] and len(d["info"]) > 6 and d["info"][6])
        print(f"  our dragons that ran into an ALLY: {len(friendly)} (into its head {fh}, through a portal {fp}); "
              f"by intent {dict(collections.Counter(d['ind'].split(' ')[0] or 'plain move' for d in friendly))}")
    ind = {k: v for (t, k), v in a["indicators"].items() if t == us}
    print(f"  our indicators: {ind}")
    pts = a["points"].get(us, [])
    if pts:
        print(f"  our CPU: max {max(pts) / 1e6:.1f}M/turn, tle {a['tle'][us]}")
    if a["mismatches"]:
        print(f"  (simulation mismatches: {a['mismatches']})")
    if show_deaths:
        for d in a["deaths"]:
            if d["team"] == us:
                print(f"    r{d['round']:>3} id{d['id']:>3} len{d['len']:>3} {d['reason']:<16} turn of {d['on_turn_of']:>3} "
                      f"ind='{d['ind']}' hit={d.get('hit')}")


GROWTH_ROUNDS = (50, 100, 150)


def growth(a) -> dict:
    """Early-game growth per team: dragons alive, pearls eaten and splits so far at rounds 50, 100
    and 150, and the longest dragon at rounds 150 and 300 (the top teams out-grow us here)."""
    def at(r, t, key):
        v = 0
        for rnd, per in a["timeline"]:
            if rnd > r:
                break
            v = per[t][key]
        return v
    out = {}
    for t in "AB":
        g = {}
        for r in GROWTH_ROUNDS:
            g[f"n{r}"] = at(r, t, "n")
            g[f"eat{r}"] = sum(1 for e in a["eat_log"] if e[1] == t and e[0] <= r)
            g[f"split{r}"] = sum(1 for rnd, tt in a["split_log"] if tt == t and rnd <= r)
        # Length our dragons of 10+ lost to deaths other than feeding (mostly head-on kills).
        g["big_lost"] = sum(d["len"] for d in a["deaths"]
                            if d["team"] == t and d["len"] >= 10 and d["reason"] != "no valid action")
        g["long150"] = at(150, t, "longest")
        g["long300"] = at(300, t, "longest")
        out[t] = g
    return out


if __name__ == "__main__":
    import sys
    if len(sys.argv) == 3 and sys.argv[1] == "--growth":
        import json
        print(json.dumps(growth(analyze(sys.argv[2], "__none__"))))
        sys.exit(0)
    ap = argparse.ArgumentParser()
    ap.add_argument("replays", nargs="+")
    ap.add_argument("--us", default="9008")
    ap.add_argument("--every", type=int, default=50)
    ap.add_argument("--deaths", action="store_true")
    args = ap.parse_args()
    for p in args.replays:
        report(analyze(p, args.us), args.every, args.deaths)
        print()
