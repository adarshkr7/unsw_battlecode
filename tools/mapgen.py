"""Random maps that follow the game's rules, for testing bots on maps they have never seen.

The tournament maps will be designed, not noise, so each map is built from the pieces the
organisers' maps are made of, with random sizes and placements:
  terrain  open | segments | rooms | pockets | arena | lanes   (kelp walls, mixed per map)
  food     sparse everywhere | hot spots (fountains of gap 1-10) | home + contested food
  armies   a few small dragons | many small ones | one big dragon and small ones
  portals  0-6 pairs
Every piece is placed together with its mirror image (SYMMETRY x: top-bottom, y: left-right,
xy: 180 degree rotation, as in the bundled maps), so the map is fair; mirrored tiles share a
pearl countdown in the engine. Maps whose regions are cut off from each other get doors opened
until both armies can reach almost every tile.

    python tools/mapgen.py --count 30 --seed 1 --out maps_gen
    python tools/mapgen.py --count 5 --seed 7 --out maps_gen --preview   # also draw them
    python tools/mapgen.py --count 30 --seed 1 --out maps_gen --check     # load each in the engine
"""

from __future__ import annotations

import argparse
import collections
import json
import pathlib
import random
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from replay_analyze import Geo  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
OPEN, KELP, PORTAL = 0, 1, 2
DIRS = {"N": (0, -1), "E": (1, 0), "S": (0, 1), "W": (-1, 0)}


class Map:
    def __init__(self, w: int, h: int, sym: str, name: str):
        self.w, self.h, self.sym, self.name = w, h, sym, name
        self.gap: dict[tuple[int, int], tuple[int, int]] = {}
        self.edge: dict[tuple[str, int, int], tuple[int, int]] = {}  # ('h'|'v', x, y) -> (kind, portal id)
        self.dragons: list[tuple[int, list[tuple[int, int]]]] = []
        self.next_pid = 0
        self.notes: list[str] = []

    # -- symmetry ----------------------------------------------------------------
    def mt(self, t):
        x, y = t
        if self.sym == "y":
            return (self.w - 1 - x, y)
        if self.sym == "x":
            return (x, self.h - 1 - y)
        return (self.w - 1 - x, self.h - 1 - y)

    def me(self, e):
        """Mirror of an edge. ('h', x, y) is the north side of tile (x, y), ('v', x, y) its west side."""
        o, x, y = e
        w, h = self.w, self.h
        if self.sym == "y":
            return ("h", w - 1 - x, y) if o == "h" else ("v", (w - x) % w, y)
        if self.sym == "x":
            return ("h", x, (h - y) % h) if o == "h" else ("v", x, h - 1 - y)
        return ("h", w - 1 - x, (h - y) % h) if o == "h" else ("v", (w - x) % w, h - 1 - y)

    def home(self, t) -> bool:
        """Team A's half (strictly off the mirror line)."""
        x, y = t
        if self.sym == "x":
            return y < (self.h - 1) / 2
        return x < (self.w - 1) / 2

    # -- geometry ----------------------------------------------------------------
    def wrap(self, x, y):
        return (x % self.w, y % self.h)

    def side(self, t, d):
        x, y = t
        if d == "N":
            return ("h", x, y)
        if d == "S":
            return ("h", x, (y + 1) % self.h)
        if d == "W":
            return ("v", x, y)
        return ("v", (x + 1) % self.w, y)

    def kind(self, e):
        return self.edge.get(e, (OPEN, -1))[0]

    def set_edge(self, e, kind):
        for f in {e, self.me(e)}:
            if self.kind(f) == PORTAL:
                continue  # never overwrite a portal
            if kind == OPEN:
                self.edge.pop(f, None)
            else:
                self.edge[f] = (kind, -1)

    def wall(self, t, d):
        self.set_edge(self.side(self.wrap(*t), d), KELP)

    def set_gap(self, t, g):
        t = self.wrap(*t)
        for u in {t, self.mt(t)}:
            if g is None:
                self.gap.pop(u, None)
            else:
                self.gap[u] = g

    def add_portal(self, e1, e2) -> bool:
        """A connected pair and its mirror pair (or one pair that is its own mirror)."""
        if e1[0] != e2[0] or e1 == e2:
            return False
        m1, m2 = self.me(e1), self.me(e2)
        if m1 == e1 or m2 == e2:
            return False  # keep portals off the symmetry line
        edges = {e1, e2, m1, m2}
        if any(self.kind(e) == PORTAL for e in edges):
            return False
        pairs = [(e1, e2)] if {m1, m2} == {e1, e2} else [(e1, e2), (m1, m2)]
        if len(pairs) == 2 and len(edges) != 4:
            return False
        for a, b in pairs:
            pid = self.next_pid
            self.next_pid += 1
            self.edge[a] = (PORTAL, pid)
            self.edge[b] = (PORTAL, pid)
        return True

    # -- output ------------------------------------------------------------------
    def text(self) -> str:
        w, h = self.w, self.h
        out = [f"MAP {w} {h}", f"SYMMETRY {self.sym}", f"MAP_NAME {self.name}", f"TILE_COUNT {w * h}"]
        for y in range(h):
            for x in range(w):
                a, b = self.gap.get((x, y), (0, 0))
                out.append(f"TILE {x} {y} {a} {b}")
        edges = []
        for (o, x, y), (k, pid) in sorted(self.edge.items(), key=lambda kv: self.index(kv[0])):
            edges.append(f"EDGE {self.index((o, x, y))} {k} {pid if k == PORTAL else -1}")
        out.append(f"EDGE_COUNT {len(edges)}")
        out += edges
        out.append(f"DRAGON_COUNT {len(self.dragons)}")
        for team, body in self.dragons:
            out.append(f"DRAGON {team} {len(body)} " + " ".join(f"{x} {y}" for x, y in body))
        return "\n".join(out) + "\n"

    def index(self, e):
        o, x, y = e
        row = 2 * y if o == "h" else 2 * y + 1
        return row * (self.w + 1) + x


# -- terrain pieces ------------------------------------------------------------------
def segment(m: Map, rng, length):
    x, y = rng.randrange(m.w), rng.randrange(m.h)
    if rng.random() < 0.5:  # a horizontal wall: north sides of a row of tiles
        for i in range(length):
            m.wall((x + i, y), "N")
    else:
        for i in range(length):
            m.wall((x, y + i), "W")


def room(m: Map, rng, rw, rh, doors):
    x0, y0 = rng.randrange(m.w), rng.randrange(m.h)
    sides = []
    for i in range(rw):
        sides += [((x0 + i, y0), "N"), ((x0 + i, y0 + rh - 1), "S")]
    for j in range(rh):
        sides += [((x0, y0 + j), "W"), ((x0 + rw - 1, y0 + j), "E")]
    doorset = set(rng.sample(range(len(sides)), min(doors, len(sides))))
    for i, (t, d) in enumerate(sides):
        if i not in doorset:
            m.wall(t, d)
    return (x0, y0, rw, rh)


def pocket(m: Map, rng, depth):
    """A dead-end lane 1 wide: the kind of place maze maps hide their food in."""
    x, y = rng.randrange(m.w), rng.randrange(m.h)
    d = rng.choice("NESW")
    dx, dy = DIRS[d]
    tiles = [m.wrap(x + dx * i, y + dy * i) for i in range(depth)]
    across = ("E", "W") if d in "NS" else ("N", "S")
    for t in tiles:
        for s in across:
            m.wall(t, s)
    m.wall(tiles[-1], d)  # the closed end
    return tiles


def arena(m: Map, rng, margin, gaps):
    """A ring of kelp around the middle with a few gates (Colosseum)."""
    x0, y0 = margin, margin
    rw, rh = m.w - 2 * margin, m.h - 2 * margin
    if rw < 4 or rh < 4:
        return
    sides = []
    for i in range(rw):
        sides += [((x0 + i, y0), "N"), ((x0 + i, y0 + rh - 1), "S")]
    for j in range(rh):
        sides += [((x0, y0 + j), "W"), ((x0 + rw - 1, y0 + j), "E")]
    gate = set(rng.sample(range(len(sides)), min(gaps, len(sides))))
    for i, (t, d) in enumerate(sides):
        if i not in gate:
            m.wall(t, d)


def lanes(m: Map, rng, spacing, gap_every):
    """Long parallel walls with breaks (Slithery Fight's lanes)."""
    horizontal = rng.random() < 0.5
    n = m.h if horizontal else m.w
    run = m.w if horizontal else m.h
    for k in range(spacing, n, spacing):
        offset = rng.randrange(gap_every)
        for i in range(run):
            if (i + offset) % gap_every in (0, 1):
                continue
            m.wall((i, k) if horizontal else (k, i), "N" if horizontal else "W")


def border(m: Map):
    """Close the wrap-around (most bundled maze maps are walled in)."""
    for x in range(m.w):
        m.wall((x, 0), "N")
    for y in range(m.h):
        m.wall((0, y), "W")


# -- connectivity ----------------------------------------------------------------------
def geo(m: Map) -> Geo:
    return Geo(m.text())


def reach(g: Geo, starts):
    seen = set(starts)
    q = collections.deque(starts)
    while q:
        t = q.popleft()
        for d in "NESW":
            n = g.step(t, d)
            if n is not None and n not in seen:
                seen.add(n)
                q.append(n)
    return seen


def connect(m: Map, rng, want=0.97):
    """Open walls between reached and unreached tiles until most of the map is one region."""
    for _ in range(400):
        g = geo(m)
        start = next(((x, y) for y in range(m.h) for x in range(m.w)), None)
        seen = reach(g, [start])
        if len(seen) >= want * m.w * m.h:
            # small leftover pockets: never spawn food there
            for y in range(m.h):
                for x in range(m.w):
                    if (x, y) not in seen:
                        m.set_gap((x, y), None)
            return True
        frontier = []
        for t in seen:
            for d in "NESW":
                e = m.side(t, d)
                if m.kind(e) == KELP:
                    dx, dy = DIRS[d]
                    if m.wrap(t[0] + dx, t[1] + dy) not in seen:
                        frontier.append(e)
        if not frontier:
            return False
        m.set_edge(rng.choice(frontier), OPEN)
    return False


# -- food ------------------------------------------------------------------------------------
def rate(m: Map) -> float:
    """Expected pearls per round if every spawn tile stayed empty."""
    return sum(2.0 / (a + b) for a, b in m.gap.values())


def food(m: Map, rng, style, pockets_found):
    notes = []
    if style in ("sparse", "hotspots", "home"):
        frac = rng.choice([1.0, 1.0, 0.5, 0.25]) if style == "sparse" else rng.choice([0.0, 0.25, 0.5, 1.0])
        gmax = rng.choice([250, 500, 1000, 2000, 4000])
        for y in range(m.h):
            for x in range(m.w):
                if rng.random() < frac:
                    m.set_gap((x, y), (1, gmax))
        notes.append(f"background {frac:.2f} of tiles (1,{gmax})")
    if style == "hotspots":
        spots = []
        for tiles in pockets_found:
            spots.append(rng.choice(tiles))
        for _ in range(rng.randint(1, 4)):
            spots.append((rng.randrange(m.w), rng.randrange(m.h)))
        for t in spots:
            g = rng.choice([(1, 1), (1, 3), (1, 5), (1, 10), (5, 30)])
            for dx in range(rng.choice([1, 1, 2])):
                for dy in range(rng.choice([1, 1, 2])):
                    m.set_gap((t[0] + dx, t[1] + dy), g)
        notes.append(f"{len(spots)} hot spots")
    if style == "home":
        # food near each army, and a richer contested strip on the mirror line
        for team, body in m.dragons:
            if team != 0:
                continue
            hx, hy = body[0]
            for _ in range(rng.randint(3, 8)):
                m.set_gap((hx + rng.randint(-4, 4), hy + rng.randint(-4, 4)), (10, rng.choice([40, 80, 150])))
        for _ in range(rng.randint(2, 6)):
            if m.sym == "x":
                t = (rng.randrange(m.w), m.h // 2 + rng.randint(-1, 0))
            else:
                t = (m.w // 2 + rng.randint(-1, 0), rng.randrange(m.h))
            m.set_gap(t, rng.choice([(1, 1), (1, 5), (1, 10)]))
        notes.append("home food + contested middle")
    # keep the total spawn rate in the bundled maps' range (about 1.5 to 60 pearls a round)
    r = rate(m)
    if r < 1.5:
        for y in range(m.h):
            for x in range(m.w):
                if (x, y) not in m.gap and rng.random() < 0.3:
                    m.set_gap((x, y), (1, 400))
    elif r > 60:
        for t, (a, b) in list(m.gap.items()):
            if b <= 10 and rng.random() < 0.5:
                m.set_gap(t, (a, b * 4))
    return notes


# -- armies ----------------------------------------------------------------------------------
def place_dragon(m: Map, rng, g: Geo, length, taken) -> list | None:
    for _ in range(300):
        x, y = rng.randrange(m.w), rng.randrange(m.h)
        head = (x, y)
        if not m.home(head) or head in taken:
            continue
        body = [head]
        ok = True
        for _ in range(length - 1):
            last = body[-1]
            opts = []
            for d in "NESW":
                n = g.step(last, d)
                # a plain step (no portal), not through kelp, onto a free tile of our half
                dx, dy = DIRS[d]
                if n is None or n != m.wrap(last[0] + dx, last[1] + dy):
                    continue
                if n in body or n in taken or not m.home(n):
                    continue
                opts.append(n)
            if not opts:
                ok = False
                break
            # mostly straight bodies, like the bundled maps
            if len(body) >= 2:
                straight = m.wrap(2 * last[0] - body[-2][0], 2 * last[1] - body[-2][1])
                if straight in opts and rng.random() < 0.7:
                    opts = [straight]
            body.append(rng.choice(opts))
        if not ok:
            continue
        # the head needs a way out: two free neighbours besides its neck
        exits = sum(1 for d in "NESW" if (n := g.step(head, d)) is not None and n not in body and n not in taken)
        if exits < 2:
            continue
        # keep clear of other dragons' heads
        if any(abs(head[0] - t[0]) + abs(head[1] - t[1]) < 3 for t in taken):
            continue
        return body
    return None


def armies(m: Map, rng, style):
    g = geo(m)
    if style == "few":
        lens = [rng.choice([3, 4]) for _ in range(rng.randint(1, 3))]
    elif style == "many":
        lens = [rng.choice([2, 3, 3, 4]) for _ in range(rng.randint(4, 7))]
    else:  # one big dragon and a few small ones
        lens = [rng.randint(8, 25)] + [rng.choice([2, 3, 4]) for _ in range(rng.randint(1, 4))]
    taken = set()
    placed = []
    for L in lens:
        L = min(L, max(2, min(m.w, m.h) // 2 - 1))
        body = place_dragon(m, rng, g, L, taken)
        if body is None:
            continue
        taken |= set(body)
        placed.append(body)
    if not placed:
        return False
    for body in placed:  # ids alternate A, B as in the bundled maps
        m.dragons.append((0, body))
        m.dragons.append((1, [m.mt(t) for t in body]))
    return True


# -- a whole map --------------------------------------------------------------------------
def generate(rng: random.Random, name: str) -> Map | None:
    w = rng.randint(10, 64)
    h = rng.randint(10, 64)
    if rng.random() < 0.6:  # the bundled maps are mostly wider than tall and not tiny
        w, h = max(w, h), min(w, h)
        w = max(w, 16)
    # no stranger than the bundled maps' 3:1 (Autarky, 54x18)
    if w > 3.5 * h:
        h = min(64, -(-w * 2 // 7))
    if h > 3.5 * w:
        w = min(64, -(-h * 2 // 7))
    sym = rng.choice(["xy", "xy", "y", "x"])
    m = Map(w, h, sym, name)
    area = w * h
    terrain = rng.sample(["segments", "rooms", "pockets", "arena", "lanes"], k=rng.randint(1, 3))
    if rng.random() < 0.2:
        terrain = ["open"]
    if rng.random() < 0.4:
        border(m)
        m.notes.append("walled border")
    found_pockets = []
    for t in terrain:
        if t == "segments":
            for _ in range(int(area * rng.uniform(0.01, 0.05))):
                segment(m, rng, rng.randint(2, max(3, min(w, h) // 3)))
        elif t == "rooms":
            for _ in range(rng.randint(1, max(1, area // 250))):
                room(m, rng, rng.randint(3, 8), rng.randint(3, 8), rng.randint(1, 3))
        elif t == "pockets":
            for _ in range(rng.randint(2, max(2, area // 120))):
                found_pockets.append(pocket(m, rng, rng.randint(2, 5)))
        elif t == "arena":
            arena(m, rng, rng.randint(2, max(2, min(w, h) // 4)), rng.randint(2, 6))
        elif t == "lanes":
            lanes(m, rng, rng.randint(3, 6), rng.randint(5, 12))
    m.notes.append("terrain " + "+".join(terrain))
    # portals: shortcuts between random edges of the same orientation
    n_portals = rng.choice([0, 0, 1, 2, 3, 6])
    made = 0
    for _ in range(50 * n_portals):
        if made >= n_portals:
            break
        o = rng.choice("hv")
        e1 = (o, rng.randrange(w), rng.randrange(h))
        e2 = (o, rng.randrange(w), rng.randrange(h))
        if m.add_portal(e1, e2):
            made += 1
    m.notes.append(f"{made} portal pairs (plus mirrors)")
    if not connect(m, rng):
        return None
    if not armies(m, rng, rng.choice(["few", "many", "big"])):
        return None
    # pockets that were cut off or opened keep their food only if reachable (connect() cleared the rest)
    m.notes += food(m, rng, rng.choice(["sparse", "hotspots", "home"]), found_pockets)
    g = geo(m)
    seen = reach(g, [m.dragons[0][1][0]])
    for y in range(h):
        for x in range(w):
            if (x, y) not in seen:
                m.set_gap((x, y), None)
    if any(body[0] not in seen for _, body in m.dragons):
        return None
    for t in list(m.gap):  # a tile under a starting dragon may spawn; that is fine
        pass
    return m


def preview(m: Map) -> str:
    rows = []
    for y in range(m.h):
        top = "".join("+" + {OPEN: "  ", KELP: "--", PORTAL: "PP"}[m.kind(("h", x, y))] for x in range(m.w))
        mid = ""
        heads = {body[0]: "AB"[team] for team, body in m.dragons}
        segs = {t: "ab"[team] for team, body in m.dragons for t in body[1:]}
        for x in range(m.w):
            mid += {OPEN: " ", KELP: "|", PORTAL: "P"}[m.kind(("v", x, y))]
            t = (x, y)
            if t in heads:
                mid += heads[t] + " "
            elif t in segs:
                mid += segs[t] + " "
            elif t in m.gap:
                b = m.gap[t][1]
                mid += "**" if b <= 10 else ".." if b <= 200 else ". "
            else:
                mid += "  "
        rows += [top, mid]
    return "\n".join(rows)


def check(path: pathlib.Path) -> str | None:
    """Load the map in the engine with a cheap bot; return its complaint, if any."""
    bot = ROOT / "bots" / "spar-earlyking"
    proc = subprocess.run(["unswbc", "run", str(path), str(bot), str(bot), "--no-replay", "--no-debug", "--seed", "1"],
                          capture_output=True, text=True, cwd=ROOT, timeout=600)
    out = proc.stdout + proc.stderr
    if "wins after" in out or "draw after" in out:
        return None
    return out.strip()[-400:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=20)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out", default="maps_gen")
    ap.add_argument("--preview", action="store_true")
    ap.add_argument("--check", action="store_true", help="play each map once in the engine to prove it loads")
    args = ap.parse_args()
    out = ROOT / args.out
    out.mkdir(exist_ok=True)
    rng = random.Random(args.seed)
    made, tries = 0, 0
    index = {}
    while made < args.count and tries < args.count * 20:
        tries += 1
        name = f"gen{args.seed}_{made:02d}"
        m = generate(rng, name)
        if m is None:
            continue
        path = out / f"{name}.map"
        path.write_text(m.text())
        info = {"size": f"{m.w}x{m.h}", "sym": m.sym, "dragons": len(m.dragons),
                "lens": sorted((len(b) for t, b in m.dragons if t == 0), reverse=True),
                "pearls_per_round": round(rate(m), 1), "notes": m.notes}
        if args.check:
            err = check(path)
            info["engine"] = "ok" if err is None else err
        index[name] = info
        print(f"{name}: {info['size']:>6} sym {m.sym:<2} dragons {info['lens']} ~{info['pearls_per_round']} pearls/rd | "
              + "; ".join(m.notes) + (f" | engine {info['engine'][:60]}" if args.check else ""), flush=True)
        if args.preview:
            print(preview(m))
        made += 1
    (out / "index.json").write_text(json.dumps(index, indent=1))


if __name__ == "__main__":
    main()
