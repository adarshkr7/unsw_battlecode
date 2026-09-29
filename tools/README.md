# Tools: how to use them

Everything runs from the project root (`unsw-battlecode/`) with plain `python`. Games need the
contest toolkit: `uv tool install unswbc`, then `unswbc auth` once (only the ladder tools use the key).

Bots live in `bots/<name>/` (frozen copies). `mybot/` is the v4.x dev bot, `swarm/` the fs-series dev bot.
`bots/oracle` is v2c. Results go to `results/`, replays to `replays/`.

## 1. Benchmarking bots

### `eval.py`: win rate of one bot against another
Plays every map × seed from both sides in the judge's sandbox (CPU priced like the ladder), prints
win % with a 95% interval, a per-map table and growth stats, and appends a line to `results/history.csv`.

```bash
python tools/eval.py bots/fs1 bots/v4.46 --suite quick --jobs 16          # 42 games, ~3 min: first look
python tools/eval.py bots/fs1 bots/v4.46 --suite blind --jobs 16          # 100 games (seed 1): decision run
python tools/eval.py bots/fs1 bots/v4.46 --suite blind --seed-base 2      # second seed
python tools/eval.py bots/a bots/b --suite blind --min-area 1000          # big maps only
python tools/eval.py bots/a bots/b --map-dir maps_world --maps pg_110 pg_112 --seeds 2
python tools/eval.py bots/a bots/b --suite quick --replays --label my-test   # keep replays for analysis
```
Useful options:
- `--suite quick | blind | official`. Use `blind` (ladder-like variants plus generated worlds) to judge changes. `official` gives old bots the true pearl spawns, which the ladder no longer does.
- `--label NAME` names the result file `results/eval-<stamp>-NAME.json`. With `--replays`, the replays go to `replays/eval-<stamp>-NAME/`.
- `--early-stop N` stops once N games are scored and the interval excludes 50%.
- `--timeout S`, `--no-sandbox` (faster, but CPU timeouts go unnoticed).

How to read the results:
- A 42-game quick gate only detects differences of about 10+ points. Any code change reshuffles 7–20 of the 42 games by chance.
- Decide on 2 seeds of the blind suite (200 games).

### `tournament.py`: bracket like the contest
Single elimination, 7 games a match on different unseen maps, sides alternating, seeded 1v8, 4v5, 2v7, 3v6.
```bash
python tools/tournament.py v2c=bots/oracle bots/v2d bots/v4 bots/v4.2 bots/v4.4 bots/v4.45 bots/v4.46 bots/fs1
python tools/tournament.py ... --draw-seed 2 --map-dir maps_world --games 7 --jobs 16 --label t2
```
- `name=folder` sets a display name.
- `--seeding listed` uses your order instead of a random draw.
- Saves every game to `results/tournament-<stamp>.json`.
- A single bracket is noisy: a bot winning 60% of games still loses a 7-game match about 29% of the time.

## 2. Maps

### `worldgen.py`: new unseen worlds (the `maps_world/` set)
```bash
python tools/worldgen.py --batch 20 --outdir maps_world/
python tools/worldgen.py --seed 42 --style labyrinth --size 48x24 --sym xy -o maze.map
```

### `variants.py`: ladder-style variants
The official maps' walls and portals with re-rolled pearl spawns, which is what the ladder plays.
```bash
python tools/variants.py --per-map 2 --seed 1 --out maps_variants
```

### `mapgen.py`: random rule-following maps built from official-map pieces
```bash
python tools/mapgen.py --count 30 --seed 1 --out maps_gen --check
```
`--check` plays each map once to prove it loads.

## 3. Ladder data (uses your API key; read-only, never submits)

### `ladder_history.py`
Our ranked history plus the replays of the last N series.
```bash
"$(uv tool dir)/unswbc/Scripts/python.exe" tools/ladder_history.py --replays 10
```
It must run with the toolkit's Python, which holds the key. It writes `results/ladder_history.json`
and `replays/ladder/<battle>-<game>.replay`. Downloads come gzip-compressed and are unpacked automatically.
- Ladder replays hide pearl spawns and strip bot logs.
- Only our own bot is named in them, so `we_are` in the JSON tells you our side.

## 4. Reading replays

### `replay_decode.py`: the decoder everything else uses
```python
import sys; sys.path.insert(0, "tools")
from replay_decode import load
rep = load("game.replay")  # map text, botA/botB, result (winner, longest, dragons), events
for kind, f in rep["events"]:  # roundStart, turnStart, dragonAction, dragonUpdate, tileChange,
    ...                        # dragonSplit, dragonDeath, sonarPing, dragonLog, ...
```
`python tools/replay_decode.py --stats game.replay` prints the final result and pearls eaten.

### `replay_analyze.py`: one game explained
Lengths over time, every death and its cause, trades, splits, pearls eaten and CPU use.
```bash
python tools/replay_analyze.py game.replay --every 25 --deaths
python tools/replay_analyze.py game.replay --growth        # early-growth summary as JSON
```

## 5. Studies (batch analyses over replay folders)

| Tool | Question it answers | Example |
|---|---|---|
| `king_study.py` | How do opponents grow and protect their king vs ours? (8/15/25 timing, king's food sources, feeding deaths, escort, safety) | `python tools/king_study.py --rows results/ladder_fs1.json --all` |
| `endgame_study.py` | Where does our length go from r380? (fragmentation, distance to king, feed rate, unfed length) | `python tools/endgame_study.py replays/eval-...-x --min-area 900` |
| `growth_study.py` | How fast do swarms grow in rounds 0–150? (splits, eating per dragon, idle splittable dragons) | `python tools/growth_study.py replays/ladder --until 150` |
| `fastswarm_study.py` | What do fast swarms do differently? (trades and who eats the drops, spread, portals, early deaths) | `PYTHONIOENCODING=utf-8 python tools/fastswarm_study.py replays/ladder` |
| `ladder_styles.py` | Opponent styles we beat vs lose to, map by map | `python tools/ladder_styles.py replays/ladder` |
| `big_risk.py` | Which situations get our big dragons killed? (hazard table per turn) | `python tools/big_risk.py replays/eval-...-x --min-len 10` |
| `kill_audit.py` | Was each head-on kill of our big dragons avoidable? | `python tools/kill_audit.py replays/eval-...-x --team cand --min-len 10 --from-round 380` |

Most take eval replay folders (`--replays`) or ladder replays.
- In eval replays our bot is the candidate: the file names end in `candA` or `candB`.
- Set `PYTHONIOENCODING=utf-8` on Windows when opponents have non-ASCII names.

## 6. Neural-net experiments (`tools/nn/`, results in `nn_report.md`)
```bash
python tools/nn/fight_dataset.py                 # ladder replays -> nn_data/fight.npz (~2.5 min, 8 processes)
python tools/nn/train_fight.py                   # train + evaluate the fight-move imitation net
python tools/nn/train_fight.py --baselines-only  # simple-rule baselines only
python tools/nn/analyze_fight.py                 # what strong teams do in fights (trade/backup stats)
```
Needs `numpy` and `torch`. It runs on CPU in minutes; no GPU is needed.

## Typical workflow for a bot change
1. **Offline evidence first:** a replay study showing the problem is real and how big it is.
2. **Build:** copy the dev bot to `bots/<name>` with the switch on, and check the mechanism in 2–4 games with `--replays`.
3. **Quick gate:** `--suite quick` against v4.45 and v4.46 (and fs1). Look at the mechanism, not only win %.
4. **Decide:** 2 seeds of `--suite blind`, plus a few tournaments with different `--draw-seed`.
