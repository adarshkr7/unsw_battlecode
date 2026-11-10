# Contributing to AllRed

This guide covers how to set up the development environment, build and run bots locally, test changes, and contribute improvements.

---

## 1. Prerequisites

| Tool | Version | Purpose |
|---|---|---|
| Python | 3.11+ | Toolkit and analysis scripts |
| Node.js | 18+ | Replay-analysis `.cjs` scripts |
| `uv` | latest | Recommended Python package manager |
| Clang | 16+ | C++ compilation (bundled in toolkit) |

### Install the `unswbc` toolkit

```bash
uv tool install unswbc          # recommended
# or: pip install unswbc
```

Verify:

```bash
unswbc                           # lists available commands and checks compiler + replay viewer
```

Log in with your team API key (needed for ladder and replay downloads):

```bash
unswbc auth set bc_<your_key>
```

---

## 2. Build and run a bot

### Single game

```bash
unswbc run maps/arena.map mybot bots/v11 --sandbox -v --seed 42
```

- `--sandbox` runs with the judge's WebAssembly sandbox (matches ladder CPU pricing).
- `-v` verbose: prints each turn's output lines.
- `--native` skips the sandbox for speed (useful for quick iteration, but CPU points differ).

### Two bots, one seed, keep the replay

```bash
unswbc run maps/default.map mybot bots/v11 --sandbox --seed 1 --replay replays/test.replay
```

### Full tournament

```bash
python tools/tourney.py mybot bots/v11 --seeds 3 --json results.json
python tools/lengths.py results.json     # per-map win/loss, lengths
python tools/deaths.py results.json      # per-map death causes and CPU usage
```

Options: `--maps a,b` restricts maps; `-j N` sets worker count (default: CPU count).

---

## 3. Bot source layout

The active bot lives in `mybot/`. All logic is in C++20.

```
mybot/
├── main.cpp        # All game logic (~1,700 lines). Edit this.
├── params.hpp      # Tunables. The only file that differs between A/B variants.
├── board.hpp       # Board state, map loading, edge/portal querying.
├── helper.hpp      # Toolkit bindings, BFS helpers, sonar utilities.
├── sonar.hpp       # Sonar packet packing/unpacking.
├── maps_data.hpp   # Auto-generated from maps/. Do not edit by hand.
└── bot.toml        # Toolkit project file. Lists source globs.
```

### Regenerate `maps_data.hpp`

After adding or changing `.map` files in `maps/`:

```bash
python tools/gen_maps.py                            # maps/ → mybot/maps_data.hpp
python tools/gen_maps.py maps_ladder3/ mybot/maps_data.hpp  # use a different map set
```

---

## 4. Naming and freezing versions

### Naming convention

| Pattern | Use |
|---|---|
| `bots/vN` | Major milestones submitted to the ladder |
| `bots/vN.M` | Incremental tuning on the same architecture |
| `bots/vN_tag` | Experimental branch (`aggro`, `nomap`, `dbg`, etc.) |

### Freeze a snapshot

```bash
mkdir bots/v23
cp mybot/*.cpp mybot/*.hpp mybot/bot.toml bots/v23/
# If the bot uses map data, copy the maps_data.hpp too:
cp mybot/maps_data.hpp bots/v23/
```

A frozen bot can then be used as a reference in `tourney.py`:

```bash
python tools/tourney.py mybot bots/v23 --seeds 5
```

---

## 5. Debugging

1. Set `DEBUG = true` in `mybot/params.hpp`.
2. Run a local game: `unswbc run maps/trauma.map mybot bots/v11 --sandbox -v --seed 5`
3. Each dragon prints its four candidate move scores every turn.
4. **Turn `DEBUG = false` before submitting or benchmarking.** Printed output is counted against the 100M CPU-point budget on the judge.

Useful replay tools:

```bash
node tools/replay.cjs replays/test.replay          # timeline, deaths and splits
node tools/dragon_log.cjs replays/test.replay       # per-dragon decision log
python tools/showmap.py maps/trauma.map 10 5 25 20  # ASCII crop of a region
```

---

## 6. Parameter tuning

All tunables are `inline constexpr` values in `mybot/params.hpp`. The tournament script builds each candidate bot once and runs games in parallel.

### Quick A/B test

```bash
# Freeze the current baseline
cp mybot/params.hpp /tmp/params_baseline.hpp

# Edit mybot/params.hpp (change one value)
python tools/tourney.py mybot bots/v22 --seeds 4 --json results_candidate.json
python tools/lengths.py results_candidate.json
```

### SPSA automated tuning

```bash
python tools/spsa.py --help
```

`spsa.py` runs simultaneous-perturbation gradient estimates over a parameter space defined in `tools/tune/space.py`. Results go to `results/spsa/` (gitignored).

The SPSA dashboard:

```bash
python tools/spsa_dashboard.py        # starts a local server
# open http://localhost:8080
```

---

## 7. Adding a new map

1. Place the `.map` file in `maps/` (or a named subdirectory like `maps_heldout/`).
2. Run `python tools/gen_maps.py` to rebuild `mybot/maps_data.hpp`.
3. Inspect it: `python tools/showmap.py maps/yourmap.map`
4. Run a quick sanity tournament: `python tools/tourney.py mybot bots/v11 --maps yourmap --seeds 2`

Map format is documented in `battlecode_docs.md` (search for "Map Format").

---

## 8. Coding conventions

- **Language:** C++20. Avoid RTTI and exceptions (the toolkit strips them).
- **Tunables:** Add new parameters to `params.hpp`, not hardcoded into `main.cpp`.
- **Headers:** `board.hpp` owns map/tile state; `helper.hpp` owns BFS and utility functions; `sonar.hpp` owns message layout. Keep `main.cpp` free of low-level bit manipulation.
- **Comments:** Explain *why*, not *what*. The existing code uses `// ---` section banners and in-function region comments.
- **Debug output:** Always guard with `if constexpr (params::DEBUG)`. Never leave unguarded `printf`/`fprintf` in submitted code.
- **Naming:** `snake_case` for variables and functions; `PascalCase` for structs; `ALL_CAPS` for `params::` constants.

---

## 9. Project workflow summary

```
edit mybot/main.cpp or params.hpp
        │
        ▼
unswbc run maps/arena.map mybot bots/vN    ← quick sanity check
        │
        ▼
python tools/tourney.py mybot bots/vN --seeds 3 --json r.json
        │
        ▼
python tools/lengths.py r.json              ← is it better?
python tools/deaths.py r.json              ← where is it dying?
        │
  if confirmed better
        │
        ▼
mkdir bots/vN+1  &&  cp mybot/... bots/vN+1/
        │
        ▼
unswbc submit -n "AllRed vN+1"
```

