# Automated tuning pipeline (tools/tune)

Goal: the strongest robust configuration of the bot's existing strategies and settings against
**v19** (`results/tmp_bots/v19_live`, ladder v17) and **v20** (`bots/v20`, ladder v18), with as few games as
possible. The bot being tuned is `bots/v22` = the live v21 plus switches that are off. Nothing here changes
bot logic; every candidate is `bots/v22` with a different `params.hpp`.

```
python -m tools.tune.pipeline --workers 16      # run or resume (rerunning the same command resumes)
python -m tools.tune.status                     # progress, pointers, last log lines
python -m tools.tune.report                     # rebuild results/tune/report.md + report.json
python -m tools.tune.pipeline --dry --out results/tune_dry   # whole pipeline on simulated games (seconds)
```

## What was inspected

- **The bot** (`bots/v22`, 3,000 lines of `main.cpp`, 280 settings in `params.hpp`). Moves are scored in
  `Brain::evaluate()` (pearls, room, threats, queen safety, hunting, feeding pulls); splitting, feeding,
  trading and striking are separate rule functions (`trySwarmSplit`, `tryFeed`, `tryStrike`, ...). No
  setting is used in a constant expression, so any `constexpr` setting can become a plain variable.
- **Map-size split (v21):** maps of 1,000+ tiles use the declared values, smaller maps the values set by
  `params::useSmallMapValues()`. The harness treats the two as separate parameter sets ("scopes") and fits
  separate models: a big-map game says nothing about small-map values.
- **Existing SPSA** (`tools/spsa.py`): theta+ vs theta- self-play, 25 settings, step 100 beat v19 240-200;
  step 150 equal to v21. It tunes against itself, not against v19/v20, and needs ~150 steps x 34 games.
  Its range table and derived-round rule (`KING_PULL_ROUND = FEED_ROUND`, `FINAL_MERGE_ROUND =
  CONSOLIDATE_ROUND + 10`) are reused in `space.py`.
- **Runner:** `tools/match.py` in the judge's wasm sandbox (as `tools/tourney.py`); the engine is
  deterministic per seed and prints each team's queen, longest dragon, total length and dragon count.
  Builds take ~39 s; games 6-100 s.
- **Maps:** tuning on the 17 exact ladder maps (`maps_official`); validation adds the 5 held-out official
  maps (`maps_heldout`) and the 2 second ladder layouts. Never generated maps.

## Parameter inventory (`tools/tune/space.py`)

| module | settings | type |
|---|---|---|
| combat | THREAT_PER_LEN, THREAT_EVEN, THREAT_DENSITY_PENALTY, HUNT_ATTRACT, HUNT_ATTRACT_AGGRO, AGGRO_RATIO, DIVE_SEG_COST, QUEEN_KILL_GAIN, TRADE_DOWN_GAIN | continuous |
| combat | TRADE_MIN_UNITS | integer |
| economy | EAT_NOW, FOOD_BEST, FOOD_SUM, GAMMA_BEST, ALLY_CLOSER, ENEMY_CLOSER, REVISIT, EAT_SPRINT_COST*, EXPLORE_W*, OUT_OF_VIEW_SMALL | continuous |
| economy | SWARM_SPLIT_LEN | integer |
| queen / endgame | QUEEN_KEEPAWAY, QUEEN_LONGER_P, QUEEN_FREEDOM, QUEEN_UNSEEN, KING_APPROACH, FEED_ATTRACT | continuous |
| queen / endgame | QUEEN_SPLIT_UNTIL, QUEEN_FOOD_RADIUS, FEED_ROUND, CONSOLIDATE_ROUND, FEED_MARGIN, QFEED_ROUND*, QFEED_PULL_PCT*, QFEED_MIN_UNITS* | integer |

(* big maps only: the feature is off on small maps, so the setting does nothing there.)

**Strategy switches** (boolean / "999 = off" rounds / categorical, each a named bundle of settings):

| group | switches |
|---|---|
| aggressive | KAMIKAZE, TRADE_DOWN_2 / TRADE_DOWN_OFF (categorical TRADE_DOWN_MAX 0/1/2, exclusive), QUEEN_HUNT_SHARED, PACK_HUNT, INTERCEPT, NO_SPLIT_VETO |
| defensive | KEEPAWAY, BIG_GUARD, QUEEN_GUARD, QUEEN_CORNERED, QUEEN_STALL, SPLIT_HEADROOM, QUEEN_PORTAL_AVOID, THREAT_ON_HEADS, DIVE_OFF |
| economic | EAT_SPRINT_FLIP, SPREAD, SPAWN_CLEAR, EXPLORE_FAR, SCOUTS, CHAIN_SPLIT, QUEEN_NO_EXPLORE, PORTAL_EXPLORE_OFF, QFEED_FLIP |
| structure | LOCAL_KINGS, EARLY_KING, LOCAL_RECYCLE, ANCHOR_OFF, GUARDS, FEED_RING, MERGE_PULL, QUEEN_BUD, QUEEN_SPLIT_SMALL, QUEEN_FOOD_SMALL_OFF |
| adaptive | ADAPT_NUMBERS, RACE_AWARE, NC_QFEED |

Left out on purpose: CPU-sensitive search limits (MULTI_*, EVAL_MAX_DEPTH, STRIKE_MAX_STEPS), debug flags,
rules of the engine (NEW_SPRINT, OWN_BODY_OPEN), and map-data switches (no map data, per the user's rule).

**Strong interactions expected from the code** (searched jointly in the interaction stage when they matter):
FEED_ROUND / CONSOLIDATE_ROUND / KING_APPROACH / FEED_ATTRACT (one feeding schedule); THREAT_PER_LEN /
THREAT_EVEN / THREAT_DENSITY_PENALTY (a `max()` of the three); HUNT_ATTRACT / AGGRO_RATIO /
HUNT_ATTRACT_AGGRO / TRADE_MIN_UNITS (aggression); EAT_NOW / FOOD_BEST / GAMMA_BEST / ALLY_CLOSER (pearl
value); QFEED_ROUND / QFEED_PULL_PCT / QFEED_MIN_UNITS; EAT_SPRINT with EAT_SPRINT_COST; ANCHOR_KING with
GUARDS; EARLY_KING with LOCAL_KINGS.

## The loop (`tools/tune/pipeline.py`)

```
calibrate -> screens (switches | combat | economy | queen) -> interactions -> race -> validate -> promote -> repeat
```

1. **Calibrate:** baseline vs v19 and v20, 17 maps x 3 seeds x 2 sides (204 games).
2. **Screens**, 8 games per configuration (4 big-map, 4 small-map; opponents alternate; random side; fresh seeds):
   - switches: random factorial design, each switch on with probability 0.15 per scope (5% of configs are the
     baseline). From halfway, switches with P(helps) < 0.1 drop to 0.04 and those above 0.6 rise to 0.30.
   - numeric modules: Bayesian optimisation. Latin hypercube in a box of +-span around the baseline for 40%
     of the batches, then Thompson sampling from the surrogate (25% random). A setting moves only if its
     linear+quadratic terms are jointly significant (p < 0.05); the chosen point maximises the posterior mean
     minus half a standard deviation.
   - interactions: the 5 most significant settings per scope, with all pairwise terms.
3. **Race:** combinations (baseline, aggressive, defensive, economic, adaptive, structure, the six pairs, all
   switches, the 4 best single switches, each numeric module's optimum, all modules, all + switches),
   deduplicated. Successive halving on shared seeds: 68 games a round (17 maps x 2 sides x 2 opponents),
   keep the better half after round 1, the best 3 after round 3, 6 rounds; anything with Δ + 1.5 SE < 0 is
   dropped at once.
4. **Validate:** the top 2 survivors with Δ > 0 and the baseline, on fresh seeds: 17 maps x 5 seeds + held-out
   and extra layouts x 5 seeds, both sides, vs v19 and v20 (480 games), plus v15 and v18 on 17 maps x 2 seeds
   (136). A close call (only the aggregate rule fails, z > 1) gets up to two more blocks of fresh seeds.
5. **Promote** only if all rules pass: aggregate paired Δ > 0 at one-sided 95%; vs v19 and vs v20 each
   Δ > -2 points and not significantly negative; big and small maps not significantly negative; held-out
   maps Δ > -4; secondary opponents not significantly negative; CPU under 70M points/turn. The promoted bot is
   written to `results/tune/promoted/it<k>_<id>/`; the previous baseline stays in the pointer history.
   The next iteration (60% budget) searches around the new baseline (switches flip relative to it).
   The pipeline stops after an iteration that promotes nothing.

**Statistics (`stats.py`).** Side and map decide many games, so every comparison is within cells
(opponent, map, side): paired score differences on identical slots for the race and validation, and a
Bayesian logistic model (cell effects + design terms, Gaussian priors, Laplace posterior) for the screens.
Draws count half. Fitness (reported) = 0.45 x score vs v19 + 0.45 x score vs v20 + 0.10 x the worst
(opponent x map size) score; promotion uses the paired rules above, never the fitness alone.

## Data

- `results/tune/tune.db` (SQLite): `configs`, `games` (one row per game with config, opponent, map, size,
  seed, side, score, queens, longest, totals, dragons, CPU, errors, experiment, stage, git), `events`,
  `pointers` (current_baseline, best_overall, best_vs_v19, best_vs_v20, most_robust; history kept).
  `results/tune/journal.jsonl` holds an append-only copy of every write.
- `results/tune/experiments/<name>.json`: every design and decision; `driver.log`: the run log.
- `results/tune/report.md` / `report.json`: the report (rebuilt after each iteration).
- Invalid games (wall-clock timeouts, engine failures) are stored with ok=0 and replayed.
