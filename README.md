# AllRed — UNSW Battlecode 2026

## Layout

| Path | What |
|---|---|
| `mybot/` | The bot being worked on (C++). `main.cpp` is the logic, `params.hpp` the tunables, `maps_data.hpp` is generated. |
| `bots/` | Frozen versions to test against: `starter` (the `unswbc init` template), `v1a`, `v1`. |
| `maps/` | The 15 bundled maps. They are the maps the ladder plays on. |
| `tools/` | Tournament script and helpers (below). |
| `battlecode_docs.md` | Offline copy of the official docs. |

## Workflow

```bash
# play mybot against a frozen version on every map, 3 seeds, both sides, in the judge's sandbox
python tools/tourney.py mybot bots/v1 --seeds 3 --json results.json

# per-map breakdowns from that run
python tools/lengths.py results.json    # win/draw/loss, final length and dragons per map
python tools/deaths.py results.json     # how bot1's dragons died, worst CPU points per map

# freeze a version once it beats the last one
mkdir bots/v2 && cp mybot/*.cpp mybot/*.hpp mybot/bot.toml bots/v2/
```

- `tourney.py` builds each bot once with the judge's clang and runs games in parallel (`-j`). `--native` is faster but not priced like the judge. `--replays DIR` keeps replays of the games bot1 did not win. `--maps a,b` limits the maps.
- `match.py` plays one game through the toolkit and prints the engine's final numbers (team total length, surviving dragons), which `unswbc run` does not show. `tourney.py` calls it; you do not need to.
- `gen_maps.py` rebuilds `mybot/maps_data.hpp` from `maps/`. Run it after `unswbc update` or `unswbc maps` brings new maps.
- `showmap.py maps/x.map [x0 y0 x1 y1]` draws a map as ASCII (`|`/`--` kelp, `P` portal, `*` fast-spawning tile, `A`/`a` spawns).
- Debugging one game: set `DEBUG = true` in `params.hpp`, then `unswbc run maps/x.map mybot bots/v1 --sandbox -v --seed N`. Each dragon logs its four move scores every turn. **Turn it back off before submitting**, since output costs CPU points on the judge.

## Ladder game limits

- The team can start **60 games an hour** by hand, ranked or unranked, on the site or through the API. A ranked battle is 5 games on random maps, so that is 12 ranked scrims an hour. An unranked battle is one game per map we choose.
- Battles against the organisers' teams (marked DEV) have their own allowance of another 60 games an hour.
- Autoscrims don't count. They are ranked 5-game series, drawn every 2 hours, at most 2 per batch.

## Engine facts verified by experiment (not all are in the docs)

- Rounds are numbered from 0.
- **Splitting does not move the parent** that turn, so a split doubles as a "wait".
- The child of a split receives the parent's sonar sent opposite its heading: it leaves through the parent's tail and hits the child's tail. That is a 64-bit handoff on the child's first turn.
- **Portals keep the direction of travel.** You come out on the far side of the partner edge.
- The board starts with no pearls. Each tile's first countdown is drawn from its [minGap, maxGap] range, and mirrored tiles share a countdown.
- In the map file, `SYMMETRY y` mirrors x (left-right), `x` mirrors y, and `xy` rotates 180°.
- Some maps are puzzles. On Slithery Fight, Dilemma and Devil, dragons spawn or feed in one-tile-wide dead ends and can only get out by splitting.

## v1 design (mybot)

1. **Map knowledge.** All 15 maps are built in. A dragon identifies its map on its first turn from the map size and its view, then knows every kelp edge, portal and spawn rate. On an unknown map it learns edges from its view instead. It remembers every tile's pearl countdown and applies the same countdown to the tile's mirror image.
2. **Safety.** A move is fatal if it hits kelp or any dragon, including its own tail. A move is unsafe unless a timed flood fill shows the head can reach its own freed body (it can follow its tail forever) or has plenty of room. The fill knows when each tile of its own body clears, and the same for other dragons whose tail is in view.
3. **Growth.** It keeps a belief of the chance there is a pearl on each tile when we would arrive. Beliefs are discounted by distance, and by whether an enemy or ally reaches the tile first; ties go to us because we are on move.
4. **Splits.** When trapped, it compares the best move with a range of split sizes, by how much length survives. It makes insurance splits when the team is down to fewer than 3 dragons.
5. **Fallbacks.** When every move is fatal, it trades heads with an enemy if it can, and never runs into an ally's head.

Results: 88–0–2 against the starter over 90 sandbox games. The worst turn was about 23M of the 100M points.

## Known weaknesses (v2 list)

- **Longest dragon.** The winner is decided by the longest single dragon, not total length. Splits can leave only small pieces (lost on Trauma with total length 35 against a single length-9 dragon). v2 needs a protected "king" dragon, and later feeding it.
- **Crowding.** On small or corridor-heavy maps (Arena, Colosseum, Devil, Slithery Fight), trapped dragons split, which crowds the board more, which traps more dragons. There are 100–600 deaths per game in self-play. v2 needs an awareness of how many dragons the map can hold.
- **Portals.** Going through a portal to a tile out of view is a blind step. It lost one game when a dragon hit its own ally at the exit.
- Not yet used: sprinting, sonar, attacking enemy heads, head-to-head threats from sprints, and the endgame.
