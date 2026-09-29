"""Single-elimination tournament between our bots, like the contest: every match is GAMES games
(default 7) on different unseen maps, sides alternating, in the judge's sandbox. The bracket is
seeded 1v8, 4v5, 2v7, 3v6 (byes for the top seeds when the field isn't a power of two); the
seeding is a random draw (--draw-seed) or the order given (--seeding listed).

    python tools/tournament.py v2c=bots/oracle bots/v2d bots/v4 bots/v4.2 bots/v4.4 bots/v4.45 bots/v4.46 bots/fs1
    python tools/tournament.py ... --map-dir maps_world --games 7 --jobs 12 --draw-seed 3

All GAMES games are played even once a match is decided (more data per pairing). A match tied
after them (draws, lost games) gets up to 3 extra games on unused maps, then pearls eaten decide.
Results: results/tournament-<stamp>.json (every game) and a printed bracket.
"""

from __future__ import annotations

import argparse
import datetime as dt
import importlib.util
import json
import pathlib
import random
import shutil
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

ROOT = pathlib.Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location("bc_eval", ROOT / "tools" / "eval.py")
bc_eval = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bc_eval)

ROUND_NAMES = {2: "Final", 4: "Semifinals", 8: "Quarterfinals", 16: "Round of 16", 32: "Round of 32"}


def bracket_order(n: int) -> list[int]:
    """Seed numbers in bracket order: 1 meets n, and 1 and 2 can only meet in the final."""
    order = [1]
    while len(order) < n:
        size = 2 * len(order)
        order = [s for x in order for s in (x, size + 1 - x)]
    return order


class Bot:
    def __init__(self, spec: str):
        name, _, path = spec.partition("=") if "=" in spec else (pathlib.Path(spec).name, "", spec)
        self.name, self.dir = name, ROOT / path
        if not self.dir.is_dir():
            sys.exit(f"not a bot folder: {self.dir}")
        self.seed = 0
        self.built = ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bots", nargs="+", help="bot folders, or name=folder")
    ap.add_argument("--map-dir", nargs="+", default=["maps_world"])
    ap.add_argument("--games", type=int, default=7)
    ap.add_argument("--jobs", type=int, default=16)
    ap.add_argument("--seeding", choices=["random", "listed"], default="random")
    ap.add_argument("--draw-seed", type=int, default=1, help="seeds the draw, the maps and the game seeds")
    ap.add_argument("--timeout", type=float, default=600)
    ap.add_argument("--label", default="")
    args = ap.parse_args()

    unswbc = shutil.which("unswbc")
    if not unswbc:
        sys.exit("unswbc not found: `uv tool install unswbc`")
    maps = [m for d in args.map_dir for m in sorted((ROOT / d).glob("*.map"))]
    if len(maps) < args.games + 3:
        sys.exit(f"need at least {args.games + 3} maps, found {len(maps)}")
    rng = random.Random(args.draw_seed)
    bots = [Bot(s) for s in args.bots]
    if len({b.name for b in bots}) != len(bots):
        sys.exit("bot names must be unique (use name=folder)")
    if args.seeding == "random":
        rng.shuffle(bots)
    for i, b in enumerate(bots, 1):
        b.seed = i

    print(f"building {len(bots)} bots (sandbox wasm) ...", flush=True)
    for b in bots:
        b.built = bc_eval.prebuild(b.dir, True)

    size = 1
    while size < len(bots):
        size *= 2
    by_seed = {b.seed: b for b in bots}
    slots = [by_seed.get(s) for s in bracket_order(size)]   # None = bye
    print(f"\n{len(bots)} bots, {args.games} games a match on {len(maps)} maps from {', '.join(args.map_dir)}; "
          f"seeding {args.seeding} (draw seed {args.draw_seed})")
    for b in sorted(bots, key=lambda b: b.seed):
        print(f"   [{b.seed}] {b.name}  ({b.dir.relative_to(ROOT)})")

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    log = {"stamp": stamp, "games_per_match": args.games, "map_dirs": args.map_dir, "draw_seed": args.draw_seed,
           "seeding": {b.name: b.seed for b in bots}, "rounds": []}
    started = time.monotonic()

    def run_games(specs):
        """specs: list of (match index, bot A, bot B, map, seed) -> list of (spec, result)."""
        out = []
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            fut = {pool.submit(bc_eval.play, unswbc, m, a.built, b.built, s, None, True, args.timeout): (k, a, b, m, s)
                   for k, a, b, m, s in specs}
            for i, f in enumerate(as_completed(fut), 1):
                k, a, b, m, s = fut[f]
                r = f.result()
                w = r["winner"]
                who = a.name if w == "A" else b.name if w == "B" else w or "error"
                print(f"      [{i:>2}/{len(specs)}] {m.stem:10} {a.name:>8} (A) vs {b.name:<8} (B) -> {who:8} "
                      f"r{r['rounds']} ({r['reason']}) {r['secs']}s", flush=True)
                out.append(((k, a, b, m, s), r))
        return out

    alive = slots
    while len(alive) > 1:
        name = ROUND_NAMES.get(len(alive), f"Round of {len(alive)}")
        pairs = [(alive[i], alive[i + 1]) for i in range(0, len(alive), 2)]
        print(f"\n== {name} ==", flush=True)
        matches = []
        specs = []
        for k, (x, y) in enumerate(pairs):
            used = rng.sample(maps, len(maps))  # this match's map order: the first GAMES, then spares
            m = {"a": x, "b": y, "maps": used, "next_map": args.games, "games": [], "score": {}}
            matches.append(m)
            if x is None or y is None:
                continue
            m["score"] = {x.name: 0.0, y.name: 0.0}
            for g in range(args.games):
                a, b = (x, y) if g % 2 == 0 else (y, x)
                specs.append((k, a, b, used[g], rng.randrange(1, 10**6)))

        extra = 0
        while specs:
            for (k, a, b, mp, s), r in run_games(specs):
                m = matches[k]
                w = r["winner"]
                st = r.get("stats") or {}
                res = (st.get("result") or {}) if st else {}
                m["games"].append({"map": mp.stem, "seed": s, "A": a.name, "B": b.name,
                                   "winner": a.name if w == "A" else b.name if w == "B" else w,
                                   "rounds": r["rounds"], "reason": r["reason"], "secs": r["secs"],
                                   "eaten": {a.name: (st.get("eaten") or {}).get("A"), b.name: (st.get("eaten") or {}).get("B")},
                                   "longest": {a.name: (res.get("A") or {}).get("longest"), b.name: (res.get("B") or {}).get("longest")}})
                if w == "A":
                    m["score"][a.name] += 1
                elif w == "B":
                    m["score"][b.name] += 1
                elif w == "draw":
                    m["score"][a.name] += 0.5
                    m["score"][b.name] += 0.5
            # Tied matches (draws, errors, timeouts): extra games on unused maps.
            specs = []
            extra += 1
            for k, m in enumerate(matches):
                if m["a"] is None or m["b"] is None or extra > 3:
                    continue
                x, y = m["a"], m["b"]
                if m["score"][x.name] == m["score"][y.name]:
                    a, b = (x, y) if len(m["games"]) % 2 == 0 else (y, x)
                    specs.append((k, a, b, m["maps"][m["next_map"]], rng.randrange(1, 10**6)))
                    m["next_map"] += 1
            if specs:
                print(f"   tie-break games: {len(specs)}", flush=True)

        winners = []
        rlog = {"name": name, "matches": []}
        for m in matches:
            x, y = m["a"], m["b"]
            if x is None or y is None:
                w = x or y
                winners.append(w)
                if w:
                    print(f"   [{w.seed}] {w.name} has a bye")
                continue
            sx, sy = m["score"][x.name], m["score"][y.name]
            if sx == sy:  # still tied: pearls eaten over the match
                ex = sum(g["eaten"][x.name] or 0 for g in m["games"])
                ey = sum(g["eaten"][y.name] or 0 for g in m["games"])
                w = x if ex >= ey else y
                note = f" (tied, {w.name} ate more pearls)"
            else:
                w, note = (x if sx > sy else y), ""
            winners.append(w)
            l = y if w is x else x
            elim = sum(1 for g in m["games"] if g["winner"] == w.name and g["reason"] and "elimination" in g["reason"])
            print(f"   [{w.seed}] {w.name:8} beat [{l.seed}] {l.name:8} {max(sx, sy):g}-{min(sx, sy):g}{note}"
                  f"   ({elim} of {w.name}'s wins by elimination)")
            rlog["matches"].append({"a": x.name, "b": y.name, "score": m["score"], "winner": w.name, "games": m["games"]})
        log["rounds"].append(rlog)
        alive = winners

    champ = alive[0]
    log["champion"] = champ.name
    print(f"\nCHAMPION: [{champ.seed}] {champ.name}   ({(time.monotonic() - started) / 60:.1f} min)")
    print("\nBracket:")
    for r in log["rounds"]:
        print(f"  {r['name']}: " + " | ".join(f"{m['winner']} {max(m['score'].values()):g}-{min(m['score'].values()):g} "
                                            f"{m['b'] if m['winner'] == m['a'] else m['a']}" for m in r["matches"]))
    out = ROOT / "results" / f"tournament-{stamp}{'-' + args.label if args.label else ''}.json"
    out.write_text(json.dumps(log, indent=1))
    print(f"\nsaved {out.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
