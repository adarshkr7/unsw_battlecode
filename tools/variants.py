"""Map variants like the ladder's: the official maps' walls, portals and starting dragons, with
new pearl spawn tiles and timings (the devs: "look exactly the same as the old maps BUT have
very different pearl spawning locations / timings", not in downloaded replays, may change
silently).

Each variant re-rolls the spawns in one of these styles, symmetric as the map's SYMMETRY says:
  shuffle   the original gap classes and tile counts, moved to new clustered spots
  uniform   every reachable tile sparse (1, G)
  hotspots  sparse background plus a few rich clusters (gap 1-10) in new places
  pockets   rich food in dead ends and corridors, little in the open
The total spawn rate stays within 0.5x to 2x of the original map's.

    python tools/variants.py --per-map 2 --seed 1 --out maps_variants
"""

from __future__ import annotations

import argparse
import pathlib
import random
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from replay_analyze import Geo  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent


def mirror(sym, w, h, t):
    x, y = t
    if sym == "y":
        return (w - 1 - x, y)
    if sym == "x":
        return (x, h - 1 - y)
    return (w - 1 - x, h - 1 - y)


def rate(gaps):
    return sum(2.0 / (a + b) for a, b in gaps.values() if b > 0)


def reachable(geo, starts):
    seen = set(starts)
    q = list(starts)
    for t in q:
        for d in "NESW":
            n = geo.step(t, d)
            if n is not None and n not in seen:
                seen.add(n)
                q.append(n)
    return seen


def variant(text: str, rng: random.Random, style: str) -> tuple[str, str]:
    lines = text.splitlines()
    w, h = map(int, lines[0].split()[1:3])
    sym = next((l.split()[1] for l in lines if l.startswith("SYMMETRY")), "xy")
    orig = {}
    for l in lines:
        if l.startswith("TILE "):
            x, y, a, b = map(int, l.split()[1:5])
            orig[(x, y)] = (a, b)
    geo = Geo(text)
    live = sorted(reachable(geo, [b[0] for _, b in geo.spawns]))
    exits = {t: sum(1 for d in "NESW" if geo.step(t, d) is not None) for t in live}
    gaps: dict[tuple[int, int], tuple[int, int]] = {}

    def put(t, g):
        gaps[t] = g
        gaps[mirror(sym, w, h, t)] = g

    def cluster(seed_tile, size, g):
        blob, front = [seed_tile], [seed_tile]
        while front and len(blob) < size:
            c = front.pop(rng.randrange(len(front)))
            for d in "NESW":
                n = geo.step(c, d)
                if n is not None and n not in blob:
                    blob.append(n)
                    front.append(n)
        for t in blob:
            put(t, g)

    target = rate(orig) * rng.uniform(0.5, 2.0)
    if style == "shuffle":
        classes = {}
        for g in orig.values():
            if g[1] > 0:
                classes[g] = classes.get(g, 0) + 1
        for g, n in sorted(classes.items(), key=lambda kv: kv[1]):  # rare (rich) classes first
            placed = 0
            while placed < n:
                size = min(n - placed, rng.choice([1, 2, 4, 6]))
                cluster(rng.choice(live), max(1, size // 2), g)
                placed += max(1, size)
    elif style == "uniform":
        g = (1, rng.choice([250, 500, 1000, 2000]))
        frac = rng.choice([1.0, 0.5, 0.25])
        for t in live:
            if rng.random() < frac:
                put(t, g)
    elif style == "hotspots":
        bg = (1, rng.choice([1000, 2000, 4000]))
        for t in live:
            if rng.random() < 0.5:
                put(t, bg)
        for _ in range(rng.randint(2, 6)):
            cluster(rng.choice(live), rng.randint(1, 6), (1, rng.choice([1, 3, 5, 10])))
    else:  # pockets: food where it is dangerous to go
        for t in live:
            if exits[t] <= 2 and rng.random() < 0.5:
                put(t, (rng.randint(1, 5), rng.choice([10, 20, 40])))
            elif rng.random() < 0.15:
                put(t, (1, rng.choice([500, 1500])))
    # scale the timings towards the target rate (keeping each tile's class)
    r = rate(gaps)
    if r > 0:
        k = r / target
        for t, (a, b) in list(gaps.items()):
            b2 = max(a, round(b * k))
            gaps[t] = (min(a, b2), b2)
    out = []
    for l in lines:
        if l.startswith("TILE "):
            x, y = map(int, l.split()[1:3])
            a, b = gaps.get((x, y), (0, 0))
            out.append(f"TILE {x} {y} {a} {b}")
        else:
            out.append(l)
    return "\n".join(out) + "\n", f"{style}, {rate(gaps):.1f} pearls/rd (original {rate(orig):.1f})"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--per-map", type=int, default=2)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--src", default="maps")
    ap.add_argument("--out", default="maps_variants")
    args = ap.parse_args()
    rng = random.Random(args.seed)
    out = ROOT / args.out
    out.mkdir(exist_ok=True)
    styles = ["shuffle", "uniform", "hotspots", "pockets"]
    for src in sorted((ROOT / args.src).glob("*.map")):
        text = src.read_text()
        for k in range(args.per_map):
            style = styles[(k + hash(src.stem) % 4) % 4] if args.per_map < 4 else styles[k % 4]
            style = rng.choice(styles) if args.per_map == 1 else style
            vtext, note = variant(text, rng, style)
            path = out / f"{src.stem}_v{args.seed}{k}.map"
            path.write_text(vtext)
            print(f"{path.name}: {note}")


if __name__ == "__main__":
    main()
