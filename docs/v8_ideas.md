# v8_engine: what the top three teams do, compared with v4.4

**Branch:** `v8_engine`, from `main`. **Baseline:** `bots/v4.4` (Ajitesh's, ladder bot 10043); `mybot` = v4.4.
**Sources:** [cutlery_bot.md](cutlery_bot.md) (rank 1), [cheji_bt.md](cheji_bt.md) (rank 2),
[lhb_bot.md](lhb_bot.md) (龙虎豹, rank 5), and our own ladder replays (section 2).

## 1. What v4.4 changed from v4.2

The midgame is unchanged. Everything new is about unknown or edited maps, or the endgame:

| Change | Setting |
|---|---|
| Match a map by walls and portals, and allow up to 3 visible pearl tiles to differ | `SPAWN_MISMATCH_OK = 3` |
| Round 0 with an unknown body: assume a straight line behind the head | `STRAIGHT_START` |
| Doomed with a partly known body: split off length − 2 anyway | `DOOMED_BLIND_SPLIT` |
| From round 380, a 10+-long dragon judges escape splits by its longest piece, and counts only enemy heads as rivals for room | `ENDGAME_KEEP_WHOLE = true` |
| From round 450, feed down to one dragon, with no insurance split | `FINAL_MERGE`, `FINAL_MERGE_ROUND = 450` |
| The king stops swarm-splitting from a set round | `KING_GROW_ROUND = 999` (off) |

The first three are v4.4's versions of the v6 fixes E5 and E6 (v6 allowed 12 pearl tiles to differ, not 3).
Because the midgame code is v4.2's, **v4.2's ladder games show how v4.4 plays until round 380**.

## 2. Our ladder profile (v4.2, 50 games on 28 Sep)

The 50 games in `results/ladder` with bot 9789 went 20–30, against teams rated about 1700–1850. We ran the same
replay tools used for the other teams on them. `results/allred/` has the scripts.

**Where we already match the top teams:**

| | Cutlery | cheji | 龙虎豹 | v4.4 (v4.2 ladder) |
|---|---|---|---|---|
| Turns at length 2 / 3 | 48% / 28% | — | 56% / 31% | **54% / 35%** |
| Splits on its first turn at length 4 | 74% (86% within a turn) | 55% a turn below 32 dragons | ~90% | **74%** |
| Split sizes | len 5 → 3 (68%), 6 → 4 (74%) | 2 or len − 2 | bud 2, or len − 2 when boxed in | **5 → 3 (68%), 6 → 4 (61%)** |
| A 2-long rams an adjacent head of 4 / 6+ | 68% / 100% | 72% / 90–100% | about half | **93% / 100%** |
| Steps toward the nearest pearl in view | — | 78% | 85% | **80%** |
| Deaths per game, mostly boxed-in 2s and 3s | — | — | 220 (88% boxed in) | **222 (about 190 boxed in)** |
| Fresh (spawned) pearls eaten before the endgame | — | — | 267 | **286** |

So **the swarm itself is not the gap.** We split as fast, at the same sizes, chase pearls as well, and churn as
much. The one early shortfall: in rounds 0–99 we eat 67 fresh pearls against our opponents' 74.

**Where we fall behind: the endgame.**

| Round | 300 | 350 | 380 | 400 | 450 | 500 |
|---|---|---|---|---|---|---|
| Our longest | 4.1 | 4.3 | 4.4 | 9.5 | 17.6 | 21.3 |
| Opponent's longest (our games) | 6.0 | 6.8 | — | 8.7 | 16.7 | 23.3 |
| 龙虎豹's longest | 7.5 | 12.8 | — | 18.1 | 31.0 | 38.8 |
| Cutlery's longest (median) | 7 | — | — | 26 | — | 38 (R490) |
| cheji's longest | 7 | 12 | — | 15 | — | 19 |
| Our total length | 81 | 89 | — | 69 | 61 | 63 |
| Opponent's total (our games) | 86 | 91 | — | 94 | 83 | 85 |

- **18 of our 30 losses came at the round limit**, decided by the longest dragon.
- **Our king starts at length 4 in round 380.** The swarm split halves every dragon until then, including the
  one that will be king. The top teams' longest is already about 14–19 at that point.
- **The king also grows more slowly:** +17 in 120 rounds, against +21 in 100 rounds for 龙虎豹 (R400–500).
- **Feeding is spread out.** After round 340, only 2.2 small-dragon feeds a game land within 2 tiles of the
  team's longest dragon; 5.6 land 16+ tiles away. Which dragon is longest changes 3.9 times a game after round 380.
- **The team burns length instead of converting it.** Our total falls from 89 to 63 between rounds 350 and 500,
  while opponents hold 83–94. After round 340 about 32 small dragons a game die 16+ tiles from the longest
  (into kelp, themselves or other bodies), which feeds nothing.
- 8+-long dragons fed themselves 26 times in 50 games (each drops only half its length for the king).

Caveat: our opponents here were weaker than the top teams' fields, and the data predates v4.4's endgame changes
(`FINAL_MERGE`, `ENDGAME_KEEP_WHOLE`).

## 3. Side by side

| | Cutlery (#1) | cheji (#2) | 龙虎豹 (#5) | v4.4 |
|---|---|---|---|---|
| Swarm | 2s, SPLIT(len − 2) | 2 + 2 at length 4 | bud 2s off the tail | halves at length 4+ |
| Switch to growth | round 260, all maps | round 325 | 336–368, per map | 380 (king pull from 340) |
| Who becomes king | whoever grew most | whoever grew most | whoever grew most | longest heard of over sonar |
| Feeding | self-destruct near a 6+ ally | dies near an ally head, 7% a turn | dies within 2 of an 8+ ally; doesn't travel | dies within 2 of the sonar king; everyone pulled toward it |
| Even 2v2 trades | 31% | 34% | ~44% | **94%** |
| Sprints a game | 47 (43% kill) | 97 (39% eat) | 25 (strikes, suicides) | 8 (all strikes) |
| Spacing from ally heads | strongest move term | none | −1.4 | no explicit term |
| Avoids standing on a spawn tile about to spawn | yes (−0.6) | — | no | no |
| Sonar | 4 pings a turn with positions | none | endgame only | body handoff and king record |
| Map knowledge | none seen | none seen | scripted openings, per-map endgame round | built-in maps, tolerant match |
| Wins by | out-growing | elimination (221 of 290, median R160) | round-limit king | — |
| Loses to | centre control on Devil (8–35) | long kings at the round limit | — | long kings at the round limit (18 of 30) |

## 4. Ideas, ranked

### Tier 1: the endgame (biggest measured gap)

1. **Grow a king before the switch.** v4.4 already has `KING_GROW_ROUND`, which is off. From that round, the
   sonar king stops halving itself while the rest keep swarming. Try 250, 280 and 300. The top teams' longest is
   about 14–19 by round 380, and ours is 4.
2. **Switch earlier.** Try `CONSOLIDATE_ROUND` at 300–340 instead of 380, with `KING_PULL_ROUND` moved along with
   it (Cutlery 260, cheji 325, 龙虎豹 ~350). Test it as a grid with idea 1, for example grow from 280 and
   consolidate at 330.
3. **One stable king.** Only switch the king when another dragon is at least 3 longer (hysteresis), not on a tie
   broken by id. After round 380 our longest changes 3.9 times a game, and most feeds miss it. Idea 1 fixes most of
   this, since the king will be clearly longest.
4. **Top-team feed rule as the fallback.** Without a fresh sonar king, die within 2 of an ally head that is at
   least max(8, 2 × our length). Today any ally longer than us qualifies (`FEED_MARGIN = 0`), which spreads length
   over 5–8-long dragons.
5. **Feed opportunistically, don't travel.** 龙虎豹's feeders keep foraging and die only when they happen to be
   near the king. That keeps fresh pearls coming in and their team total at ~100. Ours falls to 63. Try a smaller
   `KING_APPROACH` / `FEED_ATTRACT`, or pull only dragons within N steps of the king.
6. **Delay merges of long dragons.** Before `FINAL_MERGE_ROUND`, only dragons of length 4 or less feed. An 8+
   dragon still forages; merging it early trades a forager for half its length.

### Tier 2: the midgame

7. **Spread away from ally heads.** This is Cutlery's strongest move term, and 龙虎豹 scores −1.4 for ending next
   to an ally head. v4.4 only discounts pearls an ally reaches first. Add a penalty for ending next to an ally
   head, plus a small reward per step of distance from the nearest one (capped at about 4).
8. **Re-test diving.** We re-eat 264 of our own dropped pearls a game before round 380, against 228 for our
   opponents and 209 for 龙虎豹, with the same fresh intake. That's churn. Diving (from v2d) deliberately trades
   small dragons for pocket pearls. A/B test `DIVE = false`, and `DIVE_MAX_DEATH_LEN = 2`, on v4.4.
9. **Fewer even trades.** Our 2-longs take 2v2 trades 94% of the time, against 31–34% for Cutlery and cheji.
   Try `TRADE_MIN_UNITS` at 12 or 20. It's a cheap test that could go either way: cheji eats 46% of the pearls
   dropped in head-ons.
10. **Faster first 100 rounds** (67 fresh pearls against 74). Chain escape splits for long spawns: Dilemma
    11 → 9, then 2; Slithery Fight 25 → 13, 7, 2; Autarky 14 → 12, 3. This is the "deeper split search" item in
    the v6 queue.
11. **Keep spawn tiles clear.** Add a small penalty for standing on a known spawn tile whose pearl is due within
    2 rounds, as Cutlery does (−0.6). We already track `nextSpawn`.

### Tier 3: counter-play against swarm teams

12. **Hunt their king.** All three grow exactly one king, and late: from round 260, 325 and ~350. cheji's only
    reaches 16–19. Relay the enemy's longest head position in the KING ping (today it carries only its length),
    and pull nearby small dragons toward it. A 2-for-30 trade flips the round-500 tiebreak, and our 2-longs
    already ram adjacent 6+ heads 100% of the time.
13. **Guard the king against rammers.** All three ram long heads 50–100% of the time and sprint in from 2 steps
    away. v4.4's threat map already covers sprint reach (length − 1), and our 8+ dragons died head-on only once in
    50 games. Keep it, and check against `bots/cheji_clone` (a rough cheji-style sparring bot, in the v6 checkout).
14. **Survive wipe-out attempts.** cheji wins 221 of its 290 wins by elimination, at round 160 on the median, and
    12 of our 30 ladder losses were wipe-outs. Study those 12 games before designing anything.

### Not worth copying

- **Splitting 2 + 2 at length 4, the split sizes, dying alone when trapped, and reversal splits when boxed in.**
  v4.4 already does all of these at the same rates (section 2); the escape split tries length − 2.
- **Ramming longer heads.** We already do it 93–100% of the time.
- **Pearl sprints.** A 2-step sprint costs a segment, so eating one pearl with it gains no length, only tempo.
- **Scripted openings and per-map endgame rounds.** The knockouts use unknown maps.
- **Dropping sonar.** cheji's V3 gain came bundled with other changes, and v4.4's body handoff and king both
  depend on sonar. Run `SONAR=false` only as a measurement.

## 5. How to test

- Build each idea as a copy: `python tools/variant.py NAME KEY=VALUE` for settings, or a code change in `bots/v8_*`.
- Test it against v4.4 over 160 games on the 10 ladder maps:
  `python tools/tourney.py bots/X bots/v4.4 --maps <10 ladder maps> --seeds 8 --keep-replays`.
- Repeat with `USE_MAP_DATA=false` on both bots, since the knockouts use unknown maps.
- Look past W–L at longest@380, longest@500 and total@500:
  `node tools/replay.cjs DIR/*.replay --json | node results/allred/profile.cjs BOT`.
- Start with ideas 1 + 2 (settings only), then 3, 4 and 6 (small changes), then 7, 8 and 9, and 12 last (it needs
  a sonar change).
- Once v4.4 (bot 10043) has ladder games, run the section 2 profile on them to see whether `FINAL_MERGE` and
  `ENDGAME_KEEP_WHOLE` already moved the endgame numbers.

## 6. Implementation in `mybot`

Every idea is a setting in `mybot/params.hpp`, and the defaults reproduce v4.4. Checked on 16 games against v4.2
on four maps: `mybot` and `bots/v4.4` gave identical winners, rounds, deaths, lengths and CPU points.

| Idea | Setting (v4.4 value) | Variant under test |
|---|---|---|
| 1. Grow a king before the switch | `KING_GROW_ROUND` (999). From that round the king also keeps itself whole when trapped (`ENDGAME_KEEP_WHOLE`) | `v8_grow280`: 280 |
| 2. Switch earlier | `CONSOLIDATE_ROUND` (380), `KING_PULL_ROUND` (340) | `v8_cons330`: 330 / 300; `v8_grow280_cons330`: with idea 1 |
| 3. One stable king | `KING_SWITCH_MARGIN` (0) | `v8_margin3`: 3 |
| 4. Feed only a real king | `FEED_MIN_KING_LEN` (0), `FEED_KING_RATIO` (0) | `v8_feedrule`: 8 and 2× |
| 5. Feed opportunistically | `KING_PULL_RADIUS` (0 = everyone is pulled) | `v8_pull12`: 12 steps |
| 6. Delay merges of long dragons | `FEED_MAX_LEN` (999) | `v8_feedmax4`: 4 |
| 7. Spread from ally heads | `ALLY_ADJ_PENALTY` (0), `ALLY_SPREAD` (0), `ALLY_SPREAD_CAP` (4) | `v8_spread`: 40 and 30 a step |
| 8. Re-test diving | `DIVE` (true), `DIVE_MAX_DEATH_LEN` (3) | `v8_nodive`, `v8_dive2` |
| 9. Fewer even trades | `TRADE_MIN_UNITS` (6) | `v8_trade12`: 12 |
| 10. More split cuts | `SWARM_SPLIT_CUTS` (false): also try a 2-bud and the reversal when no half is safe | `v8_cuts` |
| 11. Keep spawn tiles clear | `SPAWN_BLOCK_COST` (0): moving onto a tile whose pearl is due while our body covers it | `v8_spawn`: 150 |
| 12. Hunt their king | `HUNT_KING` (false) and `HUNT_KING_*`. A new ENEMY sonar message (type 3) carries the longest enemy head seen; one direction a turn, rotating, sends it instead of KING | `v8_hunt` |
| 13. Guard the king | `KING_THREAT_MULT` (1) | `v8_guard`: 2× |
| 14. Survive wipe-outs | `ADAPT_NUMBERS` (false), an existing switch: no even trades when outnumbered locally | `v8_adapt` |
| 15. Tail strike | `TAIL_STRIKE` (false): an enemy head of 3+ (6+ for our endgame king) next to our tail tip gets a 2-long child split off there, which moves last this round and rams it | `v8_tail` |
| 16. Pocket seal | `SEAL_BONUS` (0), `SEAL_RADIUS` (4): per enemy segment, for a safe move that leaves a nearby enemy head without room to live | `v8_seal`: 150 |
| 17. Steal their king's food | `STEAL_BONUS` (0): pearls within 2 of an enemy head showing 6+ segments are worth more | `v8_steal`: double |
| 18. Shed the head | `SHED` (false): a 6+ dragon that a shorter enemy can reach after every move splits off all but its front 2 | `v8_shed` |
| More aggression | `TRADE_MIN_UNITS` = 3 (even trades from 3 dragons) | `v8_aggro` |
| Value long kills fully | `HUNT_GAIN_CAP` = 30 (was 10: a 30-long king pulled hunters like a 12-long) | `v8_huntcap` |

Ideas 12, 15, 17 and 18 target top-team behaviour (early kings, rammers) that v4.4 rarely shows, so they are
also tested against `bots/cheji_clone` (behaviour-cloned cheji), compared with v4.4's own result against it.

**What the wipe-out study found (idea 14).** Of our 12 ladder losses by elimination:
- 3 were the edited-Dilemma bug: 5 spawn dragons, never split, over by round 31–80. v4.4's map fixes cover these.
- In the other 9, the opponent had 2–5× our dragon count by round 100–150, and 1-for-1 head-on trades drained us
  (our head-on deaths match theirs almost exactly). v4.4 takes even trades whenever it has 6+ dragons,
  whatever the enemy's numbers. Ideas 9 and 14 address this.
- Mirror tests against v4.4 rarely create that imbalance, which may be why `ADAPT_NUMBERS` tested neutral before.

**Why hunting can't show up against v4.4.** Hunting needs a long enemy dragon in view (6+ visible segments; a view
only bounds an enemy's length from below) while we still have small dragons. v4.4's king only grows after round
380, and by round 400 our small dragons have mostly fed. Test idea 12 against an early-king opponent, for example
the best tier-1 variant.

## 7. Results

**Everything on at once (`v8_all`, 28 Sep).** 240 games against v4.4 on all 15 maps:

| | Score | Worst maps | Best maps |
|---|---|---|---|
| With map data | **96–144 (40%)** | Dilemma 0–16, Devil 1–15, Stronghold 1–15, Schooltime 4–12 | Big Empty 13–3, Colosseum and Default Small 10–6 |
| No map data (both bots) | **106–132–1 (45%)** | Dilemma 0–16, Stronghold 2–14, Devil 3–13, Trauma 3–12 | Big Empty 15–1, Autarky 12–4, Trophy 10–6 |

Two separate problems:
- **The opening.** On Dilemma we are down to one dragon by round 50 in every game; on Devil we trail 11 dragons
  to 19 at round 50. A Dilemma-only check of each idea alone (2 games each) found two causes: spreading from ally
  heads (idea 7) and fewer even trades (idea 9) each lost 0–2. Every other idea split 1–1, as v4.4 does against
  itself.
- **The endgame.** The early king works: on Schooltime and Stronghold `v8_all` is far ahead by round 300
  (Stronghold total 102 vs 51, Schooltime longest 9 vs 5). But it loses the round-500 tiebreak (Stronghold
  longest 32 vs 49): the stricter feeding rules keep our length spread over several dragons.

**Each idea alone** (120 games each against v4.4 on the 10 ladder maps; one standard error is about ±4.6%):

| Idea | Score | Verdict |
|---|---|---|
| 3. Stable king (`KING_SWITCH_MARGIN=3`) | 71–49 (59%) | keep (about 2 standard errors) |
| 11. Keep spawn tiles clear (`SPAWN_BLOCK_COST=150`) | 66–1–53 (55%) | keep |
| 5. Pull only within 12 steps | 63–57 (52.5%) | even |
| 8b. Diving, deaths only at length 2 | 62–58 (52%) | even |
| 9. Fewer even trades (`TRADE_MIN_UNITS=12`) | 61–59 (51%) | even, but lost both Dilemma games |
| 14. No even trades when outnumbered (`ADAPT_NUMBERS`) | 61–1–58 (51%) | even |
| 10. More split cuts | 60–60 (50%) | even |
| 13. King fears reach 2× | 60–60 (50%) | even |
| 8a. No diving | 59–61 (49%) | drop |
| 4. Feed only a real king | 57–63 (48%) | drop |
| 1. Early king at 280 | 56–64 (47%) | drop |
| 6. Long dragons don't feed before 450 | 55–65 (46%) | drop |
| 7. Spread from ally heads | 41–1–78 (35%) | drop |
| 2. Switch at 330 | 40–80 (33%) | drop |

(Split cuts took 24,361 s only because the laptop slept from 00:08 to 06:47 in the middle of it.)

**Reproduce section 2** (`results/` is git-ignored, so the scripts are only in the main checkout):
```
node tools/replay.cjs results/ladder/*.replay --json > ladder.json && node results/allred/profile.cjs < ladder.json
# per-turn view: a folder with games.json (results/allred/games_v42.json) and the 50 replays
node tools/turns.cjs <folder> > turns.jsonl
python results/allred/rates.py turns.jsonl      # swarm, rams, pearl chasing, deaths, pearls per 50 rounds
python results/allred/deaths.py turns.jsonl     # boxed-in vs free deaths
python results/allred/endgame.py turns.jsonl    # feeding distance, long-dragon deaths, king changes
node results/lhb/origin.cjs <folder>            # spawned vs recycled pearls
```
