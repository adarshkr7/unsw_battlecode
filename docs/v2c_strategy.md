# AllRed bot v2c: strategy notes

**Status (27 Sep 2026):** v2c is our live submission. Its code is frozen in `bots/v2c/` (C++, about 1,300 lines) and committed on the `v2` branch as `7cd0671`.
**Results so far:** 24–0–1 in its first 25 ladder games, and our rating went from 1500 to 1555. In local tests it beat our previous bot (v1) 59–1.

---

## 1. What the top teams do, and why we copied it

Scouting top-10 replays changed our plan. Every strong team plays **swarm**:

- **They split constantly:** 100–300 splits per game, keeping most dragons at length 2–4, up to the cap of 64 dragons.
- **They trade heads:** head-to-head kills *both* dragons whatever their length, so a length-3 dragon trading with a length-40 one is a huge win. Games have dozens of head trades.
- **Against weaker teams they win by elimination:** the swarm hunts down every enemy dragon (for example, 64 alive against 0 by round 198).
- **Against strong teams they consolidate:** at about rounds 350–400 they stop splitting, and small dragons deliberately die next to one "king" so it can eat the pearls they drop. In one game the king went from length 8 to 35 in 50 rounds. The game is decided at round 500 by the **longest single dragon** (typically 30–45).
- **Most use sonar heavily:** 10,000–45,000 pings per game.

v2c implements the swarm, the head trades and the consolidation. It does not use sonar yet.

## 2. How each dragon decides its turn

Every dragon runs its own copy of the bot and sees only a 7×7 window. Each turn it builds up its knowledge of the board and then decides in this order:

1. **Strike:** if an enemy head is reachable this turn (a normal move, or a sprint of up to L−1 steps through free tiles) and the trade is worth it, do it.
   - Worth it means: the enemy is longer than us.
   - Even trades are also taken when we have 6+ dragons before the endgame, or when we outnumber the enemies in view 2 to 1.
   - Never with our last dragon.
2. **Survive:** if every move leads into a trap, try a range of split sizes. Pick the one that saves the most length. After a split the new dragon's head starts at the old tail, facing away, which often gets it out.
3. **Feed** (round 380+): if an ally is longer than us (or the same length with a lower ID) and its head is within 2 steps, die on purpose. We do this with an illegal `SPLIT 1`, and our body turns into pearls right next to the king.
4. **Insurance:** if the team is down to fewer than 3 dragons, split a small child off a long dragon. Losing every dragon loses the game outright.
5. **Swarm split** (before round 380): split in half once length is 4 or more, provided both halves are safe, we aren't about to eat, and no enemy head can reach us while we stand still.
6. **Otherwise, pick the best-scoring move** (section 3).

## 3. How a move is scored

**Fatal moves are skipped first:** kelp, any dragon's body, and our own tail. The tail still counts as blocked on the move it leaves, because collisions are checked before the tail moves.

Each remaining move is scored by a search that knows *when* each tile frees up. Our own body frees from the tail. So does every other dragon whose tail is in view: segment j from the tail frees on our move j+2.

| Term | What it rewards or punishes |
|---|---|
| **Pearls** | 400 for eating now, plus up to 300 for the best pearl reachable (discounted 0.85 per step), plus a small sum over nearby pearls. |
| **Safety** | Big penalty (20,000) unless the head can reach its own freed body, so it can follow its tail forever, or has plenty of room. "Room" only counts tiles we reach before any other head (a Voronoi region). |
| **King room** | Dragons of length 10+ get a bonus for open space, so the king doesn't get boxed in. |
| **Threats** | Penalty if an enemy head could reach the destination next turn (sprints included): 60 per segment we'd lose more than them in the trade, 15 for an even trade. |
| **Hunting** | Pull toward enemy heads worth trading with, seen now or in the last 8 rounds. The pull is stronger when we outnumber them nearby. |
| **Feeding pull** | In the endgame, pull toward a king we could feed. |
| **Loops** | A small penalty for revisiting tiles, which stops dragons circling forever. |

**Pearl beliefs:** every tile has a "chance there's a pearl when we arrive". It comes from the pearl countdowns we've seen (a mirrored tile shares its twin's countdown), remembered pearls (which fade as others may eat them), and spawn-rate priors for tiles we've never seen. It's discounted when an enemy or ally will get there first; ties go to us because we're on move.

## 4. Map knowledge

- All 15 ladder maps are built into the bot (`maps_data.hpp`). On its first turn a dragon identifies its map from the map size and its view, and then knows every kelp edge, portal and pearl spawn rate.
- On an unknown map (e.g. new tournament maps) it learns from what it sees instead.
- Rules verified by experiment (not all are in the docs):
  - When a dragon splits, the parent does **not** move that turn.
  - Portals keep your direction of travel.
  - The board starts with no pearls.
  - Rounds are numbered from 0.
  - A parent can pass 64 bits to its new child with sonar at the moment of the split.

## 5. Known weaknesses (from losses against bibobibo, rated 1619, where we went 6–3)

1. **Mazes like Trauma:** we collect far fewer pearls (19 total length vs 90 by round 350). The safety rule forbids entering dead ends, where many pearls are. Their dragons dive in, eat, and split to get out.
2. **Endgame:** our king is too small (12 vs 29 on Portals). Feeders only find the king if they can see it, and with 7×7 vision on a big map most never do. Their 33,000 sonar pings suggest they coordinate this.
3. **Prisoners Dilemma opening:** eliminated in 19 rounds. They split their big starting dragon at once, twice in a row, pushing pieces out into the middle. Their length-7 pieces then traded heads with our length-3 ones until we had nothing left.

## 6. Next version (v2d, in progress)

- **Diving:** small dragons may enter a dead end with pearls if they'll be long enough (4+) to split their way out.
- **Low-dragon defence:** with 5 or fewer dragons, avoid all enemy reach and trade only for a big length gain.
- **Sonar beacons:** the king broadcasts its position and allies pass it on, so feeders converge from across the map.
- **Better opening splits** on maps where the starting dragon is stuck in a pocket.

## 7. Working on the bot

```bash
# play mybot against a frozen version on every map, 3 seeds, both sides, priced like the judge
python tools/tourney.py mybot bots/v2c --seeds 3

# summarise any replay (ours or a downloaded ladder game): timeline, deaths, splits, strikes
python tools/replay_setup.py          # once, after installing or upgrading unswbc
node tools/replay.cjs game.replay --every 50
```

- **Settings** live in `mybot/params.hpp`. Keep `DEBUG = false` in anything submitted, because output costs CPU points.
- **Freeze a version before changing it:** copy `mybot` into `bots/vX`, then compare with `tourney.py`. Self-play results are noisy (about ±5% over 90 games), so run 3+ seeds.
- **Budget:** the worst turn in our tests used about 28M of the 100M CPU points per dragon per turn.
- **More detail:** `README.md` has the tools and verified engine facts; `battlecode_docs.md` is an offline copy of the official docs.
