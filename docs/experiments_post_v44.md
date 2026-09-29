# Every experiment since v4.4 (Sept 28-29, 2026)

Win % is the candidate against the opponent named. The 95% interval is about +/-10 points at
90-100 games, +/-15 at 40, +/-28 at 12. "Official" maps give our bots the true pearl spawns,
which the ladder now hides. "Blind suite" = 30 ladder-like variants (official walls, re-rolled
pearls) + 20 generated worlds (tools/worldgen.py).

## Baselines
| Run | Maps | Games | Result |
|---|---|---|---|
| v4.4 vs oracle (v2c) | official | 90 | **72.2%**, pearls +11%, final longest +8.1 |
| v4.4 vs oracle (v2c) | 20 generated worlds (blind) | 40 | **77.5%**, pearls +40% |
| sparring early-king bot vs v4.2 | official | 90 | 31.1% (the early-king style alone is weak) |

## 1. King survival in the endgame (official maps, vs v4.4)
| Variant | Games | Win % | Verdict |
|---|---|---|---|
| king splits only when boxed in (space < 4) | 90 + 90 fresh | 53.3 / 52.2 | noise |
| same, space < 8 | 90 | 48.9 | noise |
| feeding ring 3 / ring 4 (+ "crowded" flag) | 90 / 90 | 53.3 / 48.9 | noise |
| big-dragon threat penalty x4 / x10 | 36 / 36 | 44.4 / 44.4 | worse |
| big dragons assume long enemies | 36 | 41.7 | worse |

## 2. Top teams' ideas (official maps, vs v4.4, 90 games each)
| Idea | Win % | Pearls |
|---|---|---|
| switch to consolidation at round 330 / 300 | 37.8 / 35.6 | -20% / -26% |
| no sonar | 41.1 | -1% (so our sonar is worth about 9 points) |
| local recycling | 46.7 | -1% |
| spread out | 43.3 | -17% |
| keep spawn tiles clear | 45.6 | +3% |
| kamikaze small dragons | 48.9 | -3% |
| relaxed split rule | 45.6 | +5% |
| package of the four above | 52.8, then 45.6 on fresh seeds | +6% / +4% |

Verdict: none helps. Consolidating earlier costs a lot of eating.

## 3. Portals "dead end" fix (small dragons may take blind portals and rich pockets)
| Variant | Maps | Games | Win % | Pearls |
|---|---|---|---|---|
| blind-portal penalty 150 (from 1500) | Portals | 12 | 75.0 | +58% |
| + dive cost 0 / 150 | official, all | 90 / 90 | 52.2 / 53.3 | +25% / +19% |
| same | Queen of Spades | 12 / 12 | 16.7 / 33.3 | regression? |
| + dive cost cut only for rich pockets (100 / 200) | 5 maze maps | 40 / 40 | 50.0 / 52.5 | +40% / +50% |

Verdict: eats far more on Portals (7 -> 36 pearls by r50), but the extra length never reaches
the king, so no wins. This was measured with true spawn data; on the blind suite it is neutral (section 6).

## 4. Early king, midgame feeding, guard (big official maps, vs v4.4)
| Variant | Games | Win % | King at r300 (ours vs v4.4) |
|---|---|---|---|
| fix only (reference) | 42 | 50.0 | 7.1 vs 6.7 |
| early king from r150 | 42 | 51.2 | 8.2 vs 6.5 |
| + feeding at the dragon cap | 42, then 70 | 56.0, then 52.1 | 9.0-9.4 vs 6.7-6.8 |
| early king from r100 | 42 | 47.6 | 10.4 vs 6.6 |
| + midgame feeding from r250 / r200 | 42 / 42 | 52.4 / 53.6 | 11.4 / 12.2 |
| + small dragons pulled to the king | 42 | **36.9** | 13.1 (the swarm stops eating) |
| big-dragon guard 20 / 50 / 40 (margin 1) | 70 each | 49.3 / 46.4 / 46.4 | losses -16 length a game, no wins |

Verdict: the king grows, but a bigger king is a bigger target. No win gain.

## 5. Blind recognition (after the devs' map variants)
| Variant | Maps | Games | Win % |
|---|---|---|---|
| walls/portals-only recognition (x-terrain) | 30 variants | 60 | 55.0 |
| fully blind (control, should be about 50) | 30 variants | 60 | 53.3 |

v4.4 recognises none of the variants (it plays fully blind on the ladder), except where every
tile spawns, where it would trust wrong timings.

## 6. v4.5 candidates on the blind suite (vs v4.4, 100 games a seed)
| Candidate | Seed 1 | Seed 2 | Combined |
|---|---|---|---|
| x45-t: walls/portals only + map indicator | 53.0 | - | 53.0 (1 seed) |
| x45-td: + dead-end fix | 54.0 | 45.0 | **49.5** |
| x45-tdk: + early king, feeding at the cap | 54.0 | 44.0 | **49.0** |
| x45-tds: x45-td + food sharing + symmetry inference | 53.0 | 52.0 | **52.5** (final longest +4.8 / +3.3) |
| x45-p: x45-tds + king protection (interceptors + keep-away) | 57.0 | 47.0 | **52.0** |
| x45-pk: x45-p + early king | 54.0 | - | 54.0 (1 seed) |

Symmetry inference found the right symmetry on every world tested (round 5-10), with no
wrong adoptions. Protection cut the per-turn kill rate of our big dragons about 13%, but
not the length lost.

## 7. Endgame merging (21 big blind maps, 2 seeds, vs v4.4, 84 games)
| Variant | Win % | Unfed at end | Second king at end | Final longest |
|---|---|---|---|---|
| x45-tds (reference) | 50.0 | 131 | 19.6 | 33.8 |
| m1: pull from r300, twice as strong | 52.4 | 115 | 18.7 | 33.1 (but 142 total length at r380 vs 168) |
| m3: second kings pulled 3x harder | 47.6 | 132 | 18.4 | 31.3 |
| m13: both | **35.7** | 119 | 17.8 | 31.4 |

## Analyses (no benchmark)
- **Kill audit** (big dragons killed head-on, rounds 380+): 55% had no safe move, 22% misjudged,
  12% needed hidden information, 11% scoring.
- **Midgame hazard** (big_risk.py, 28k big-dragon turns): kills need a shorter enemy head within 6
  (0.04% a turn without, 10-15% within 3). Room and dead ends do not matter. Allies help only at 4-6 tiles.
- **Interceptor opportunity:** when a hunter is within 4 of our big dragon, a small ally could strike
  it next turn only 4% of the time. In 62% of cases no small ally is near.
- **Endgame study:** at r380 our longest is only about 8; small dragons are a median 16 steps away;
  the median feed comes 39 rounds after 380; about 130 length is left unfed and a second king of about 20 survives.
  Only 20% of our r380 length ends in the final king.
- **King diagnosis** (debug logs, 3 big maps): 0-26% of our dragons follow the real longest dragon.
  Most follow a dead king (records live 30 rounds) or one that keeps splitting itself (the king
  swarm-splits until r380). Several dragons think they are the king.
- **Ladder** (API, 525 v4.4 games): results match Elo exactly by rating band. By map: Portals 25%,
  Trauma 39%, Slithery 40%, Devil 40%, Queen 44%, Autarky 47%, Trophy 47%, Default 52%, Schooltime 52%, Dilemma 79%.
- **Ladder styles** (200 replays): we win 86% against late-king swarms that don't out-grow us,
  but only **11-12% against fast swarms** (1.3x+ our dragons at r150), which make up 40% of our games.
  In our losses they have 27.7 dragons at r150 (ours 15.4), eat 816 pearls (ours 587), sacrifice
  94 dragons before r380 (ours 42) and use portals about 110 times a game (ours 32; v4.4 plays blind there).

## What held up
Only knowledge sharing (x45-tds, +2.5 points and +4 final length on two seeds) and
walls/portals-only recognition (+3-5 on variants). Everything else was noise or worse.
The real gaps are early growth speed, portal use on unknown maps, and the endgame king
information (followed kings that are dead or shrinking).
