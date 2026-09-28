# AllRed bot v2d: strategy and review guide

**Status (28 Sep 2026):** v2d is the next submission. It replaces v2c, which is live at a rating of about 1670.
**Code:** [`bots/v2d/`](../bots/v2d/) on `main` in the private repo `adarshkr7/unsw_battlecode`. `mybot/` plays identically, but also holds switched-off experiments (see section 6).
**Tested:** v2d beat v2c **77–43 (64%)** over 120 local games in the judge's sandbox. It won all 24 Prisoners Dilemma games against every earlier version.

---

## 1. The game plan in one paragraph

Every strong team plays **swarm**, and so do we. We split into many small dragons (length 2–4, up to the cap of 64): they collect pearls in parallel, and there are too many for the enemy to eliminate. Moving onto an enemy head kills *both* dragons whatever their length, so small dragons trade heads with longer enemies. From **round 380** we **consolidate**: small dragons die on purpose next to a longer ally (the "king") so it can eat the pearls they drop. At round 500 the winner is the team with the **longest single dragon**, unless one team was eliminated first.

## 2. What changed from v2c to v2d

Each change was tested on its own against v2c (90 games each) and kept only if it helped.

| Change | Result against v2c | Why it matters |
|---|---|---|
| **Chained splits** | 56%, Dilemma 6–0 | The split evaluation knows a new dragon of length 4+ can split again straight away. The third piece's head then starts partway along the body, so a dragon with both ends stuck in dead ends can still get a piece out. On Dilemma our big dragon used to die in its pocket and we were eliminated within 25 rounds. |
| **Body handoff over sonar** | (needed by the above) | A new dragon sees only its 7×7 window, so it doesn't know its full body and refuses to split. On the split turn the parent sends a sonar out of its own tail (which lands on the new dragon's tail) carrying the body shape, 2 bits per segment. |
| **Diving** | 59% | A dragon of length 8 or less may enter a dead end that holds pearls, if it will then be long enough (4+) to split its way out. The rear piece leaves facing the exit; we lose only the stuck front piece. Strong teams collect most of their pearls this way on Devil and Trauma. |
| **Feeding down to 2 dragons** | 59%, Trauma 6–0 | v2c stopped feeding at 3 dragons and made insurance splits at 2, so length never concentrated in one king. |
| **Splitting while eating** | 61% (against the dive version) | We used to wait until after eating to split. Splitting straight away gives about 20% more dragons and 15% more total length by round 150. |

**Combined:** 12.6 vs 10.8 dragons at round 50, 62.8 vs 57.0 total length at round 150, and a longest dragon of 15.6 vs 13.7 at the end.

## 3. How a dragon decides its turn

Each dragon runs its own copy of the bot, sees a 7×7 window, and keeps its own memory. Every turn (`turn()`, `bots/v2d/main.cpp:1058`) it does the following in order:

1. **Strike** (`tryStrike`, :791): take a head trade that's worth it, by a single move or a sprint (up to L−1 steps, max 6) through free tiles in view.
   - Worth it means: the enemy is longer than us, or the lengths are even and we have 6+ dragons before the endgame (or outnumber the enemies in view 2 to 1).
   - Never with our last dragon.
2. **Escape:** if the best move is unsafe and isn't a dive, compare moving on with about 9 split sizes (`splitValue`, :676) by how much length survives. Chained splits count here.
3. **Doomed:** if every move is fatal, trade heads with an enemy if we can; otherwise die without hitting an ally's head.
4. **Feed** (`tryFeed`, :1000, round 380+): if a visible ally is longer than us (ties go to the lower ID) and its head is within 2 steps, die with an illegal `SPLIT 1` so our body turns into pearls next to it.
5. **Insurance:** a lone dragon of length 8+ splits off a 2-segment child, because losing every dragon loses the game.
6. **Swarm split** (`trySwarmSplit`, :1010, before round 380): split in half at length 4+, if both halves are safe and no enemy head can reach us while we stand still. This now happens even on a turn we could eat.
7. **Best move** (`evaluate`, :507), scored as follows.

## 4. How a move is scored

**Fatal moves are skipped:** kelp, any dragon's body, our own tail (it still blocks on the move it leaves), and an ally's head. A time-aware search then scores what's reachable from the destination. It knows when every tile frees up: ours from the tail, and every other dragon whose tail is in view, with segment j from the tail freeing on our move j+2.

| Term | Weight |
|---|---|
| Eat now | +400 |
| Best pearl reachable | +300 × belief × 0.85^steps |
| Unsafe (can't reach our own freed tail, and not much room) | −20,000, or −500 for a dive |
| "Room" | Counts only tiles we reach no later than any other head (a Voronoi region). Food and tail-reach still use every reachable tile. |
| Long dragons (10+) | +6 per tile of room, so the king stays in the open |
| Enemy could reach us next turn | −60 per segment we'd lose more than it; −15 for an even trade |
| Hunting | Pull toward enemy heads worth trading with, seen now or in the last 8 rounds |
| Feeding | In the endgame, pull toward a king we could feed (400 / (1 + steps)) |
| Loops | −40 per recent visit to the tile, which stops dragons circling |

**Pearl belief (per tile, when we'd arrive):**
- **In view:** 1 if there's a pearl, 0.9 if its countdown ends by then.
- **Remembered pearl:** fades at 0.97 per round.
- **Never seen:** chance it has spawned by arrival, from the tile's spawn range. Mirrored tiles share a countdown, so seeing one tile times its twin too.
- **Discounts:** ×0.15 if an enemy head gets there first, ×0.25 if an ally does. Ties go to us because we're on move.

## 5. Map knowledge

- All 15 ladder maps are built into the bot (`maps_data.hpp`, generated by `tools/gen_maps.py`). On its first turn a dragon identifies its map from the map size and its view, and then knows every kelp edge, portal and spawn rate. On unknown maps it learns from what it sees.
- Engine rules we verified by experiment:
  - a splitting parent does **not** move that turn;
  - portals keep your direction of travel;
  - the board starts with no pearls;
  - rounds are numbered from 0;
  - a ping sent opposite your heading leaves through your tail.

## 6. Tried and rejected (still in the code, switched off)

| Experiment | Setting | Result against v2c / v2d |
|---|---|---|
| King beacons over sonar | `SONAR` | 54%. It hurt on big_empty, where feeders stopped eating to travel. |
| Linear (strong) pull toward a distant king | `FEED_PULL_LINEAR` | The dragons with it stalled at about 450 total length while v2c grew to 809 |
| Low-dragon defence | `LOW_UNITS` | 55%, neutral |
| Accept losing a 2-piece when splitting | `SPLIT_SAFETY=1` | 48% |
| Split within enemy reach | `SPLIT_THREAT_OK` | No effect |
| Pearl sharing over sonar (up to 12 pearls a turn, relayed) | `PEARL_SONAR` (only in `mybot/`) | 49–54% against v2d, neutral |
| Waiting near a pearl about to spawn | `FUTURE_WAIT_GAMMA` (only in `mybot/`) | 42% against v2d, worse |

## 7. Known weaknesses (from ladder games)

1. **Early collection against 1750+ teams:** they collect pearls 2–5× faster by round 150 and then eliminate us or out-grow us. Examples: Settlers of Battlecode 5–0, dev test 1 6–4. Pearl information isn't the cause, since sharing it didn't help. The likely causes are risk tolerance (they lose hundreds of dragons to kelp and self-collisions while diving) and how early and often they split.
2. **The endgame:** we sometimes end with several medium dragons instead of one king (for example, 126 total length but a king of only 21).
3. **Local testing** plays only against our own cautious bots, so it underrates these problems.

## 8. How to review it

Read [`bots/v2d/main.cpp`](../bots/v2d/main.cpp) (about 1,230 lines), [`board.hpp`](../bots/v2d/board.hpp) (map parsing and stepping through edges and portals) and [`params.hpp`](../bots/v2d/params.hpp) (every setting, with comments).

**Worth checking:**
- **Turn order:** dragons act in ID order, and we assume each other dragon moves exactly once between our moves. That's what the j+2 release rule and the threat map rely on.
- **`splitValue`:** release timings for the parent, child and chained piece, and whether the "safe" test (reach our own freed tail, or a lot of room) is too strict or too loose.
- **`tryStrike`:** sprint paths go only through free tiles in view, and we ignore pearls eaten along the way.
- **The body handoff:** the parent sends it only when it knows its whole body. The new dragon checks every segment against its own view before trusting it.
- **CPU:** the worst turn in tests used about 28.6M of the 100M points. Keep `DEBUG = false` in anything submitted.

**To test a change:**

```bash
python tools/variant.py myidea SOME_PARAM=value      # copies mybot to bots/myidea with the change
python tools/tourney.py bots/myidea bots/v2d --seeds 3   # 90 games, both sides, judge sandbox
```

Only keep a change if it wins clearly over 90+ games. The tournament also prints dragon count and total length at rounds 50 and 150, and the longest dragon at the end.
