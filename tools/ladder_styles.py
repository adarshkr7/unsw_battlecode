"""Opponent styles in our ladder games (replays from tools/ladder_history.py --replays).

Per game and side: growth (dragons at r50/r150, pearls eaten), king (longest at r150/r300/
end), midgame self-sacrifice (own-turn deaths before round 380, which is how top teams feed
a king early), aggression (our dragons they kill on their turn) and portal use (head jumps).
Then: what the opponents we beat look like vs the ones we lose to, and map by map.

    python tools/ladder_styles.py [replays/ladder]
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

from replay_analyze import analyze
from replay_decode import load

ROOT = pathlib.Path(__file__).resolve().parent.parent


def our_ids():
    subs = json.loads((ROOT / "results" / "submissions.json").read_text())
    return {str(s["id"]) for s in subs}


def portal_jumps(rep):
    """Head moves longer than the move itself: a portal (per team)."""
    last = {}
    team = {}
    for i, l in enumerate(l for l in rep["map"].splitlines() if l.startswith("DRAGON ")):
        team[i] = "AB"[int(l.split()[1])]
    jumps = collections.Counter()
    w, h = map(int, rep["map"].splitlines()[0].split()[1:3])
    for kind, f in rep["events"]:
        if kind == "dragonSplit":
            team[f["child"]] = f["team"]
        elif kind == "dragonUpdate" and f.get("head"):
            d, hd = f["id"], f["head"]
            if d in last:
                dx, dy = abs(hd[0] - last[d][0]), abs(hd[1] - last[d][1])
                if min(dx, w - dx) + min(dy, h - dy) > 7:
                    jumps[team.get(d)] += 1
            last[d] = hd
    return jumps


def features(path):
    try:
        rep = load(path)
        a = analyze(path, "__none__")
    except Exception as e:  # a truncated download
        return None
    ours = our_ids()
    us = "A" if rep["botA"] in ours else "B" if rep["botB"] in ours else None
    if us is None:
        return None
    them = "B" if us == "A" else "A"
    res = rep["result"] or {}
    won = res.get("winner") == us if res.get("winner") else None
    tl = dict(a["timeline"])

    def at(r, t, k):
        rr = max((x for x in tl if x <= r), default=None)
        return tl[rr][t][k] if rr is not None else 0
    last = a["timeline"][-1][1]
    jumps = portal_jumps(rep)
    out = {"file": os.path.basename(path), "map": a["map"], "won": won, "rounds": a["timeline"][-1][0]}
    for side, t in (("us", us), ("them", them)):
        o = "B" if t == "A" else "A"
        out[side] = {
            "n50": at(50, t, "n"), "n150": at(150, t, "n"),
            "long150": at(150, t, "longest"), "long300": at(300, t, "longest"), "long_end": last[t]["longest"],
            "eaten": a["eaten"][t],
            "self_mid": sum(1 for d in a["deaths"] if d["team"] == t and d["round"] < 380 and d["on_turn_of"] == d["id"]
                            and d["reason"] in ("hit self", "no valid action")),
            # the other side's dragons dying on a turn not their own (mostly killed by this side)
            "kills": sum(1 for d in a["deaths"] if d["team"] == o and d["on_turn_of"] != d["id"]),
            "portal": jumps.get(t, 0),
        }
    return out


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "replays" / "ladder")
    files = sorted(glob.glob(os.path.join(folder, "*.replay")))
    with multiprocessing.Pool(16) as pool:
        rows = [r for r in pool.map(features, files) if r and r["won"] is not None]
    print(f"{len(rows)} ladder games with replays: {sum(r['won'] for r in rows)} won")
    keys = [("n50", "dragons r50"), ("n150", "dragons r150"), ("eaten", "pearls eaten"), ("long150", "longest r150"),
            ("long300", "longest r300"), ("long_end", "longest at end"), ("self_mid", "own-turn deaths <380 (feeding)"),
            ("kills", "enemy dragons killed"), ("portal", "portal jumps")]

    def med(rs, side, k):
        v = [r[side][k] for r in rs]
        return statistics.mean(v) if v else float("nan")
    wins = [r for r in rows if r["won"]]
    losses = [r for r in rows if not r["won"]]
    print(f"\n{'mean per game':34} {'WINS: us':>9} {'them':>7}   {'LOSSES: us':>10} {'them':>7}")
    for k, name in keys:
        print(f"  {name:32} {med(wins, 'us', k):9.1f} {med(wins, 'them', k):7.1f}   {med(losses, 'us', k):10.1f} {med(losses, 'them', k):7.1f}")
    print("\nby map (games, our win %, our/their pearls, our/their longest at end, our/their portal jumps):")
    by = collections.defaultdict(list)
    for r in rows:
        by[r["map"]].append(r)
    for m, rs in sorted(by.items(), key=lambda kv: sum(x["won"] for x in kv[1]) / len(kv[1])):
        print(f"  {m:18} {len(rs):>3}  {100 * sum(x['won'] for x in rs) / len(rs):5.1f}%   "
              f"{med(rs, 'us', 'eaten'):6.0f}/{med(rs, 'them', 'eaten'):<6.0f} {med(rs, 'us', 'long_end'):5.1f}/{med(rs, 'them', 'long_end'):<5.1f} "
              f"{med(rs, 'us', 'portal'):6.1f}/{med(rs, 'them', 'portal'):<6.1f}")
    # opponent styles: early king (their longest at r300 >= 15) and early feeding (10+ own-turn deaths before 380)
    def style(r):
        t = r["them"]
        s = []
        s.append("early king" if t["long300"] >= 15 else "late king")
        s.append("fast swarm" if t["n150"] >= r["us"]["n150"] * 1.3 else "")
        s.append("mid feeding" if t["self_mid"] >= 10 else "")
        return ", ".join(x for x in s if x)
    st = collections.defaultdict(lambda: [0, 0])
    for r in rows:
        k = style(r)
        st[k][0] += r["won"]
        st[k][1] += 1
    print("\nopponent style (from their replay)            games   our win %")
    for k, (w, n) in sorted(st.items(), key=lambda kv: -kv[1][1]):
        print(f"  {k:44} {n:>5}   {100 * w / n:5.1f}%")
    (ROOT / "results" / "ladder_styles.json").write_text(json.dumps(rows, indent=1))


if __name__ == "__main__":
    main()
