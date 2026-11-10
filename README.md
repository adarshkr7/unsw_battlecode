# AllRed — UNSW Battlecode 2026

> **AllRed** is UNSW's competitive entry for [Battlecode 2026](https://battlecode.org/), codenamed *AllRed v2: a swarm that consolidates*. The bot is written in C++20 and plays the **AllRed** game — a Snake-like multi-agent competition where teams of up to 64 dragon bots race to grow the longest single dragon by round 500.

---

## Table of Contents

1. [The Game](#the-game)
2. [Final Results](#final-results)
3. [Repo Structure](#repo-structure)
4. [Bot Architecture](#bot-architecture)
5. [Key Design Parameters](#key-design-parameters)
6. [Bot Version History](#bot-version-history)
7. [How to Run](#how-to-run)
8. [Tools Reference](#tools-reference)
9. [Maps](#maps)
10. [Engine Quirks](#engine-quirks)
11. [Known Issues / What v3 Would Tackle](#known-issues--what-v3-would-tackle)
12. [License](#license)

---

## The Game

The game is documented in full in [`battlecode_docs.md`](battlecode_docs.md) (offline copy of the official rules). A brief summary:

- Each team controls up to **64 dragon bots** simultaneously, each running the same C++ program independently.
- Each bot sees only a **7×7 tile window** around its own head.
- Dragons move like in Snake: eat a **pearl** to grow by one segment; crash into a wall (kelp) or any dragon body — including your own — and you die.
- The board wraps toroidally. Kelp edges block movement. **Portals** teleport a dragon through to the partner edge, preserving direction.
- Dragons can **sprint**: a dragon of length L gets `ceil(L/4)` free steps per turn; each additional step costs one tail segment.
- Dragons can **split** into two smaller dragons; the parent does not move on a split turn.
- Dragons communicate exclusively via **sonar**: a 64-bit ping that travels to the first dragon in a cardinal direction.
- **Victory condition:** the team with the **longest single dragon** at round 500, or the first team to eliminate all enemy dragons.

---

## Final Results

AllRed was submitted to the UNSW Battlecode 2026 ladder by the UNSW team. Key milestones corresponding to `bots/v1` through `bots/v8`:

| Bot directory | Platform submission | Strategy / Notes | Record |
|---|---|---|---|
| [`bots/v1`](bots/v1/) | Submission 1 (v2c) | First swarm strategy & consolidation baseline | 24–0–1 in first 25 ladder games; rating 1500 → 1555 |
| [`bots/v2`](bots/v2/) | Submissions 12 & 13 (v9 / v9.1) | Queen-aware bot for 1 Oct rules; no map data, CPU guard | 48 won / 37 lost over 85 games |
| [`bots/v3`](bots/v3/) | Submission 14 (v10) | Fast-swarm + queen integration; no map data | 34–27 in first 61 games |
| [`bots/v4`](bots/v4/) | Submission 15 (v11) | Relaxed splits, queen length retention, endgame horizon | 262–338 over 600 games (peak rating ~1800) |
| [`bots/v5`](bots/v5/) | Submission 16 (v18) | Auction-based food assignment (`FOOD_AUCTION` on) | 41–27 vs v15 reference |
| [`bots/v6`](bots/v6/) | Submission 17 (v19) | Favourable trade-down + feed margin 4 | Ladder active from 3 Oct |
| [`bots/v7`](bots/v7/) | Submission 18 (v20) | Tuned endgame horizon and consolidation weights | Pre-final tournament candidate |
| [`bots/v8`](bots/v8/) | Final (AllRed v2) | Full swarm + consolidation + authenticated sonar king relay | Top-tier competitor |

The bot competed on the UNSW Battlecode 2026 public ladder against dozens of teams worldwide.

---

## Repo Structure

```
unsw_battlecode/
├── bots/               # Submitted platform bots (v1 to v8)
│   ├── v1/             # Submission 1 (v2c)
│   ├── v2/             # Submissions 12 & 13 (v9 / v9.1)
│   ├── v3/             # Submission 14 (v10)
│   ├── v4/             # Submission 15 (v11)
│   ├── v5/             # Submission 16 (v18)
│   ├── v6/             # Submission 17 (v19)
│   ├── v7/             # Submission 18 (v20)
│   └── v8/             # Final submission (AllRed v2)
├── maps/               # 15 official ladder maps (.map files)
├── tools/              # Tournament, analysis, and tuning scripts
│   └── tune/           # SPSA tuning scripts
├── docs/               # Architecture, tuning, and strategy analysis docs
│   ├── BOT_ARCHITECTURE.md
│   ├── PARAMETER_TUNING.md
│   └── STRATEGY_ANALYSIS.md
├── battlecode_docs.md  # Offline copy of the official game documentation
├── README.md           # This file
├── CONTRIBUTING.md     # Setup and contribution guide
├── LICENSE
└── .gitignore
```

### `bots/v8/` files (Final bot)

| File | Purpose |
|---|---|
| `main.cpp` | All bot logic (~1,700 lines) |
| `params.hpp` | Tunable constants (the only file that changes between A/B variants) |
| `board.hpp` | Board state, map loading, edge/portal resolution |
| `helper.hpp` | Toolkit bindings and utilities |
| `sonar.hpp` | Sonar message packing/unpacking |
| `maps_data.hpp` | Auto-generated: 15 maps embedded as C++ string literals |
| `bot.toml` | Toolkit project manifest |

---

## Bot Architecture

The final bot (`bots/v8/main.cpp`, also developed in `mybot/`) is called **AllRed v2: a swarm that consolidates**.

The header comment captures the design precisely:

> *Each turn: fold the 7×7 view into long-lived knowledge (edges, pearl timers, the embedded map if we recognised it), then decide, in order:*
> 1. **strike** — a head trade worth taking, by a move or a sprint (both dragons die);
> 2. **survive** — when every move traps us, split so the most length lives on;
> 3. **feed** — late in the game a small dragon dies next to a much longer ally, whose pearl-seeking then eats what it drops, so we finish with one long dragon;
> 4. **swarm** — early on, split in half whenever long enough, up to the dragon cap;
> 5. **otherwise** — the best-scoring move: pearls reachable (a belief over every tile, discounted by distance and by who gets there first), room to keep moving (a timed flood fill that knows when every visible body frees up), and the head trades enemies could force on us next turn.

### 1. Strike

A head trade kills **both** dragons regardless of their sizes, so a length-3 ramming a length-40 is a huge gain. Each turn the bot:

- Computes the BFS distance from every enemy head in view or in recent memory.
- For each reachable enemy within sprint range (`STRIKE_MAX_STEPS = 6`), evaluates whether the trade is favourable (enemy longer, or we're in an aggressive posture).
- Plans and executes a sprint if needed.

Even trades are taken only when the team has enough dragons (`TRADE_MIN_UNITS = 12`), or when our heads in view outnumber the enemy's (`AGGRO_RATIO = 2.0`).

### 2. Survive

When every cardinal move leads to a fatal cell, the bot tries a range of split sizes (halves, buds of 2, and reversals). It picks the split that preserves the most total length. After a split the new child's head starts at the parent's former tail — often the only exit from a tight spot.

Insurance splits keep the team above `MIN_UNITS = 2` dragons; losing all dragons is an immediate loss.

### 3. Feed (Consolidation)

From `CONSOLIDATE_ROUND = 330`, small dragons look for a **king** — a visible ally that is longer than themselves. If found within `FEED_DIST = 2` steps, the feeder executes a `SPLIT 1` (illegal split of length 1 — it dies immediately), dropping all its body segments as pearls right next to the king's head. The king then eats them on its next turn.

A sonar **KING** message relays the position and length of the longest known friendly dragon across the whole board, so feeders can navigate toward it even without line of sight.

### 4. Swarm

Before `CONSOLIDATE_ROUND`, the bot splits in half whenever its length reaches `SWARM_SPLIT_LEN = 4`. Both halves check safety before the split is committed. This produces 30–64 small dragons that:

- Cover the board in parallel, eating pearls.
- Are individually cheap to lose in head trades.
- Cannot all be caught at once.

The **king** stops splitting from `KING_GROW_ROUND = 280` to start accumulating length.

### 5. Move Scoring

For each legal move the bot computes a scalar score:

| Component | Weight | Description |
|---|---|---|
| Eat now | 400 | Pearl on the destination tile this turn |
| Best food | 300 × γ^d | Best reachable pearl, discounted at γ=0.85 per step |
| Food sum | 3 × Σγ^d | Sum over nearby pearls (local richness tie-break) |
| Safety | −20,000 | Cannot reach own freed body and little room |
| King room | 6 × tiles | Dragons 10+ long get bonus for open Voronoi space |
| Threat | −60/seg | Enemy head reaches destination next turn (we lose more) |
| Threat even | −15 | Enemy can force an even trade |
| Hunting | +60/step closer | Pull toward enemy heads worth trading |
| Feed pull | +400/step closer | Pull toward the king we could feed |
| Revisit | −40 × heat | Penalty for tiles visited recently (breaks loops) |
| Seal bonus | +80/seg | Safe move that pockets an enemy head without room |
| Steal bonus | +50% | Pearl near a big enemy head (starve their king) |

**Pearl belief model:** every tile has an estimated probability of holding a pearl when we arrive. The estimate comes from:
- Observed pearl countdowns (mirrored tiles share a countdown under symmetric maps).
- Remembered pearls (decaying at `REMEMBERED_DECAY = 0.97` per round as others may eat them).
- Spawn-rate priors for tiles we haven't seen (`UNSEEN_PRIOR = 0.6` scale).
- Competition: belief is discounted when an enemy reaches the tile first (`ENEMY_CLOSER = 0.15`) or an ally does (`ALLY_CLOSER = 0.25`).

**Safety (timed flood fill):** the fill propagates from the destination and knows exactly when each visible body tile frees (the tail advances one step per turn). A move is safe if the fill can reach our own freed body (we can follow our tail forever), or if the accessible Voronoi room exceeds a threshold. A big dragon judges safety by its longest surviving piece from round `CONSOLIDATE_ROUND`.

### Map Knowledge

All 15 official ladder maps are embedded in `maps_data.hpp` as raw C++ string literals (generated by `tools/generate_maps_header.py`). On the first turn a dragon:

1. Checks its view against each stored map's wall and portal layout.
2. Accepts the first map where walls and portals match, tolerating up to `SPAWN_MISMATCH_OK = 3` pearl-rate mismatches (the ladder has edited maps mid-season).
3. Immediately knows every kelp edge, portal pair, and spawn rate — including tiles outside its 7×7 window.
4. Applies the map's symmetry to infer the mirror tile's pearl countdown.

On an unseen map, the bot learns edges and portals from whatever it sees each turn.

### Sonar System

Each ping carries 64 bits to the first dragon (ally or enemy) in a cardinal direction. AllRed authenticates every message with a 9-bit keyed hash (enemies cannot forge or decode messages):

```
bits 0–2   type   (BODY=1, KING=2, ENEMY=3)
bits 3–11  check  (keyed hash of type + payload)
bits 12–63 payload (XOR-masked with a keyed constant)
```

**Message types:**

| Type | Payload | Use |
|---|---|---|
| `BODY` | Child's shape (5-bit length + 2 bits/link, up to 24 segments) | Handoff at split: parent sends child's starting body so it knows itself immediately |
| `KING` | King id, length, x, y, round, enemy max | Relay the longest known ally dragon across the board; feeders navigate to it |
| `ENEMY` | Enemy id, length, x, y, round | Relay the longest known enemy; small hunters are pulled toward it |

The child of a split receives its own body shape on its very first turn via the sonar packet the parent sends in the split direction. This avoids a blind turn where the child doesn't know its own length.

---

## Key Design Parameters

All tunables live in [`bots/v8/params.hpp`](bots/v8/params.hpp) (and in `mybot/params.hpp` during local development). The tournament script builds variants that differ only here.

| Parameter | Default | Description |
|---|---|---|
| `DEBUG` | `false` | Log all move scores (turn off before submitting — output costs CPU points) |
| `USE_MAP_DATA` | `true` | Use built-in ladder map knowledge |
| `SPAWN_MISMATCH_OK` | `3` | Pearl-rate mismatches tolerated in map matching |
| **Phases** | | |
| `SWARM_SPLIT_LEN` | `4` | Split in half when this long (swarm phase) |
| `SPLIT_WHILE_EATING` | `true` | Split even on a turn we could eat |
| `CHAIN_SPLIT` | `true` | Count a stuck child of 4+ splitting again on its first turn |
| `CONSOLIDATE_ROUND` | `330` | Switch from swarm to consolidation |
| `ENDGAME_KEEP_WHOLE` | `true` | King judges escape splits by longest surviving piece |
| `FINAL_MERGE_ROUND` | `450` | From here feed down to one dragon |
| `MIN_UNITS` | `2` | Never drop below this many dragons |
| `INSURANCE_MIN_LEN` | `8` | Split an insurance child off dragons this long |
| **Head Trades** | | |
| `TRADE_MIN_UNITS` | `12` | Take even trades only with this many dragons |
| `STRIKE_MAX_STEPS` | `6` | Longest sprint planned to reach an enemy head |
| `THREAT_PER_LEN` | `60.0` | Penalty per extra segment we'd lose in an enemy-forced trade |
| `HUNT_ATTRACT` | `60.0` | Pull toward enemy heads worth trading (per step closer) |
| `HUNT_MEMORY` | `8` | Rounds a head sighting stays worth chasing |
| `AGGRO_RATIO` | `2.0` | Our heads / their heads in view: threshold for aggressive mode |
| **Consolidation** | | |
| `FEED_DIST` | `2` | Die this many steps from the king's head |
| `FEED_MIN_KING_LEN` | `8` | Only kings this long are worth feeding |
| `FEED_KING_RATIO` | `2.0` | King must be this many times our length |
| `KING_PULL_RADIUS` | `12` | Only dragons within this many steps pull toward the king |
| `FEED_MAX_LEN` | `4` | Dragons longer than this forage instead of feeding |
| **Move Weights** | | |
| `EAT_NOW` | `400.0` | Stepping onto a pearl this turn |
| `FOOD_BEST` | `300.0` | Best discounted reachable pearl |
| `GAMMA_BEST` | `0.85` | Per-step discount for the best target |
| `TRAPPED` | `20000.0` | Penalty for being unable to follow own tail |
| `KING_ROOM` | `6.0` | Bonus per reachable tile for dragons 10+ long |
| **Sonar** | | |
| `KING_STALE` | `30` | Rounds a KING record lives without refresh |
| `KING_GROW_ROUND` | `280` | King stops swarm-splitting from this round |
| `KING_PULL_ROUND` | `300` | From this round, all dragons head for the known king |
| `HUNT_KING` | `true` | Relay and hunt the longest enemy head |
| `HUNT_KING_ROUND` | `250` | Start hunting from this round |
| **Pearl belief** | | |
| `ENEMY_CLOSER` | `0.15` | Belief scale when enemy reaches first |
| `ALLY_CLOSER` | `0.25` | Belief scale when ally reaches first |
| `REMEMBERED_DECAY` | `0.97` | Per-round decay for remembered pearls |
| `UNSEEN_PRIOR` | `0.6` | Prior scale for tiles never seen |

---

## Bot Version History

| Version | Key change |
|---|---|
| **v1** | First swarm strategy: split at length 4+, head-trade favourable matches, consolidate from r380 by feeding near a king; 24–0–1 in first 25 ladder games |
| **v2** | Refined split rules, improved pearl belief model, submitted to ladder — established midgame baseline (80% of turns at length 2–3) |
| **v3** | Map matching by walls and portals (tolerate up to 3 pearl-rate mismatches); straight-line start-body assumption; endgame keep-whole for the king; final merge from r450 |
| **v4** | Symmetry detection; deep engine analysis; branches testing seal, steal, shed, dive, hunting, adaptation, tail-strike, spawn-block penalty, and sonar |
| **v5** | Queen-aware rewrite for "Slay Queen" rules: queen is the king, never feeds, splits only until r60, avoids enemy reach with a probabilistic threat model; multi-step paths (up to 3 free steps); CPU guard for big maps (87–89M → 31M points on 64×64) |
| **v6** | Three-way merge: fast-swarm (portal sharing via sonar, anchored king, density-based even-trade rules) + queen bot; no built-in map data; CPU budget for multi-step paths; +61% win rate over v5 |
| **v7** | Relaxed splits (one safe half + child has an exit); endgame horizon (room for remaining rounds counts as safe); queen keeps length in escape splits; sealed-queen fallback; +61% win rate over v6 |
| **v8** (current) | Ladder tuning: map data restored, full sonar (BODY/KING/ENEMY relay), 5-step decision pipeline, swarm that consolidates into one long king dragon |

---

## How to Run

### Prerequisites

Install the `unswbc` toolkit (Python 3.11+):

```bash
uv tool install unswbc   # recommended
# or: pip install unswbc
```

Verify your setup:

```bash
unswbc                   # lists commands and checks compilers / replay viewer
```

Log in with your team API key:

```bash
unswbc auth set bc_...
```

### Run a single match

```bash
unswbc run maps/arena.map bots/v8 bots/v4 --sandbox -v --seed 42
```

### Run a tournament

```bash
python tools/run_tournament.py bots/v8 bots/v4 --seeds 3 --json results.json
```

Options:
- `--seeds N` — number of RNG seeds (plays both sides per seed per map)
- `-j N` — parallel workers (default: CPU count)
- `--native` — faster but not priced like the judge sandbox
- `--replays DIR` — save replays of games bot1 did not win
- `--maps a,b,c` — restrict to specific maps

### Analyse results

```bash
# Win/draw/loss, mean final length and dragon count per map
python tools/analyze_lengths.py results.json

# Death causes and worst CPU points per map for bot1
python tools/analyze_deaths.py results.json
```

### Freeze a version

```bash
mkdir bots/v9
cp mybot/*.cpp mybot/*.hpp mybot/bot.toml bots/v9/
```

### Debug a game

1. Set `DEBUG = true` in `bots/v8/params.hpp` (or `mybot/params.hpp`).
2. Run: `unswbc run maps/trauma.map bots/v8 bots/v4 --sandbox -v --seed 5`
3. Each dragon logs its four move scores every turn in the replay log.
4. **Turn `DEBUG` back to `false` before submitting** — printed output costs CPU points.

### Regenerate map data

Run after `unswbc maps` or `unswbc update` brings new maps:

```bash
python tools/generate_maps_header.py                     # maps/ → mybot/maps_data.hpp
python tools/generate_maps_header.py maps_ladder/ mybot/maps_data.hpp  # use a different map set
```

### View a map

```bash
python tools/render_map.py maps/trauma.map
python tools/render_map.py maps/trauma.map 0 0 15 10   # crop to region
```

ASCII output: `|` / `--` = kelp, `P` = portal, `*` = fast-spawning tile, `A`/`a` = team A/B spawns.

### Submit to the ladder

```bash
unswbc submit -n "AllRed v2" -d "swarm + consolidation + sonar king relay"
```

### Auto-scrim

```bash
python tools/ladder_auto_scrim.py              # ranked scrims against teams rated above ours
python tools/ladder_auto_scrim.py --dry-run --once   # show count and next pick
python tools/ladder_auto_scrim.py --reserve 10       # leave 10 manual challenges per hour
```

---

## Tools Reference

All tools are in `tools/`. Python scripts run with any Python 3.11+. `.cjs` scripts run with Node.js.

### Core match & tournament workflow

| Tool | Description |
|---|---|
| `run_tournament.py` | Build both bots once and run all maps × seeds × sides in parallel; outputs JSON |
| `run_match.py` | Play one game and print the full end-of-game result (called by `run_tournament.py`) |
| `analyze_lengths.py` | Per-map win/draw/loss, mean final length and dragon count from a tournament JSON |
| `analyze_deaths.py` | Per-map death causes and worst CPU points for bot1 from a tournament JSON |
| `render_map.py` | Render a `.map` file as ASCII in the terminal |
| `generate_maps_header.py` | Embed all `.map` files in a directory into `maps_data.hpp` as C++ string literals |

### Ladder interaction

| Tool | Description |
|---|---|
| `ladder_auto_scrim.py` | Continuously spend the hourly game allowance on ranked scrims vs higher-rated teams |
| `download_replays.py` | Download ladder replays for a team |
| `query_team_games.py` | List or download games for a named team |

### Replay analysis (Node.js / Python)

| Tool | Description |
|---|---|
| `inspect_replay.cjs` | Print a game timeline, deaths, splits and strikes every N rounds |
| `replaylib.cjs` | Replay parsing and board geometry helper library |
| `summarize_single_replay.py` | One-line summary of a replay |
| `summarize_replays.cjs` | Aggregate results from a folder of replays |
| `setup_replay_viewer.py` | Install replay viewer JS dependencies (run once after toolkit install) |
| `analyze_turns.cjs` | Per-turn move and decision summaries |

### Tuning and map generation

| Tool | Description |
|---|---|
| `spsa_tune.py` | SPSA simultaneous perturbation parameter tuning on ladder maps |
| `spsa_validate.py` | Validate SPSA averaged values against reference bot |
| `spsa_dashboard.py` | Web server dashboard for real-time SPSA monitoring |
| `spsa_dashboard.html` | Frontend dashboard HTML for SPSA runs |
| `tools/tune/` | Modular pipeline, runner, stats, and database for distributed SPSA runs |
| `generate_world_maps.py` | Procedurally generate maps for generalisation testing |
| `create_bot_variant.py` | Build a param-variant bot from a base and an override list |

### A/B testing scripts

| Tool | Description |
|---|---|
| `run_ab_test.sh` | A/B test a candidate against a frozen reference on ladder maps |
| `run_ab_nomap_test.sh` | A/B test on generated maps (no map data) |



The 15 maps in `maps/` are the official ladder maps. Each is played on both sides of every tournament seed.

| Map file | Size | Notes |
|---|---|---|
| `arena.map` | 11×11 | Starter / small open map; quick eliminations |
| `autarky.map` | 54×18 | Wide corridor; two-team layout; queen spawn hazards |
| `big_empty.map` | — | Large open space; exploration challenge |
| `Colosseum.map` | — | Enclosed arena shape |
| `default.map` | 32×32 | Balanced; square; most games go to round 500 |
| `default_small.map` | — | Smaller default variant |
| `devil.map` | 32×16 | Dense kelp corridors; pearls concentrate in the centre; head-on wars |
| `dilemma.map` | 32×16 | One-tile-wide dead ends at both spawns; teams must split to get out |
| `portals.map` | 32×16 | Portal-heavy; both queens usually die; decided by longest dragon |
| `queen_of_spades.map` | 25×35 | Often ends in elimination; medium-size |
| `schooltime.map` | 60×40 | Large; queens often seal each other; decided by longest dragon |
| `slithery_fight.map` | 63×27 | Very wide; queens start coiled at length 25; decided by longest |
| `stronghold.map` | — | Fortified layout; corridor-heavy |
| `trauma.map` | 48×24 | Labyrinthine; dead ends everywhere; massive pearl reward for diving |
| `trophy.map` | 25×25 | Small; almost always decided by elimination in under 100 rounds |

**Engine facts:**

- In `SYMMETRY y` maps, x is mirrored (left-right). In `SYMMETRY x`, y is mirrored. In `SYMMETRY xy`, the board rotates 180°.
- The board starts with **no pearls**. Each tile's first countdown is drawn from its `[minGap, maxGap]` range; symmetric tiles **share** a countdown.
- Some maps are "puzzle" maps: on Slithery Fight, Dilemma and Devil, dragons spawn or feed in one-tile-wide dead ends and can only escape by splitting.
- Portals **keep the direction of travel**: you exit on the partner edge's far side.



Verified by experiment — not all of these are in the official docs:

- Rounds are numbered from **0**.
- **Splitting does not move the parent** that turn, so a split also acts as a "wait".
- The child of a split receives the parent's sonar sent **opposite its heading**: it leaves through the parent's tail and the ping hits the child's tail on its first turn — a 64-bit body-shape handoff.
- **Portals preserve direction of travel.** You exit on the far side of the partner edge.
- The board starts with **no pearls**. Each tile's first countdown is in its `[minGap, maxGap]` range; symmetric tiles share a single countdown.
- In `.map` files, `SYMMETRY y` mirrors x (left-right), `SYMMETRY x` mirrors y, and `SYMMETRY xy` rotates 180°.
- Some maps are "puzzle" maps: on Slithery Fight, Dilemma and Devil dragons spawn or feed in one-tile-wide dead ends and can only get out by splitting.
- **Sprint reach:** a dragon of length L can move `ceil(L/4)` free steps plus up to `L-2` paid steps in one turn, reaching `ceil(L/4) + L - 2` tiles.
- **Collisions are checked before tails move**, so the tail tile is still occupied on the move it departs.
- Enemy reach passes through our own body (a dragon can sprint across a cell our body is vacating).
- The team can start **60 games per hour** (rolling window) on the ladder; ranked battles are 5 games each; autoscrims are separate.

---

## Known Issues / What v3 Would Tackle

From analysis of ~600 ladder games:

1. **Small-map elimination.** On Trophy, Stripes and Devil the safety-first heuristic (`TRAPPED` penalty of 20,000) prevents dying in dead ends intentionally but starves the swarm on corridor maps.

2. **Big-map consolidation timing.** On Portals, Maze and Islands we start feeding too late. Moving `KING_PULL_ROUND` and `FEED_ROUND` earlier would help but requires more reliable sonar king tracking.

3. **Queen longevity.** In ~80% of losses our queen dies before round 200, killed head-on by 2–4-long swarm units while searching for food. A leash (`QUEEN_LEASH_W`) was prototyped but not confirmed.

4. **Exploration on unseen maps.** On large maps the flat unseen-tile prior and exponential distance decay mean dragons never travel toward large unexplored areas.

5. **Portal blindness.** Stepping through a portal to an unseen tile is a blind move. AllRed penalises unknown portals (`UNKNOWN_PORTAL = 3000`) but doesn't yet share discovered portal pairs via sonar.

---

## License

This project is released under the **MIT License**. See [LICENSE](LICENSE) for details.

> *UNSW Battlecode 2026 — AllRed team. The game engine and toolkit are © the UNSW Battlecode organisers.*
