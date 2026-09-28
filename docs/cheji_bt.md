# cheji bt V3: their bot, reverse-engineered from ladder replays

**Team:** cheji bt (id 70), rank 2, rated about 2096 on 28 Sep.
**Bot:** V3, live since 27 Sep 16:18 UTC. It won 62.8% (290–172) of its ranked games against a field rated mostly 1800–2100.
**Data:** its 462 finished ranked games from 27 Sep 16:18 to 28 Sep 10:34 UTC.
**How:** we replayed each game move by move and recorded what every cheji dragon could see and what it did (573K sampled turns). We then counted how often each action follows each situation. We trained no models here: `tools/bc/` is the kit for that.

## Summary

cheji plays **a swarm of tiny dragons**:
- It splits every 4-long dragon into 2 + 2 and runs 40–64 dragons at once.
- It trades heads with small dragons, and deliberately kills trapped or surplus dragons so its own dragons eat the pearls they drop.
- At **round 325** it stops splitting and starts growing a longest dragon.

It wins by **eliminating** the opponent: 221 of its 290 wins, at round 160 on the median. It loses at the **round limit** to one long dragon: 112 of its 172 losses. In those losses it usually has more total length and more dragons, but its longest is about half the opponent's.

## 1. Which games are V3

The replays show when cheji changed bots:
- Up to 27 Sep 14:55 its dragons sent sonar on almost every turn.
- A short-lived build ran from 14:26 to 14:31.
- From 27 Sep 16:18 its dragons send **no sonar at all**, sprint far more often, and switch to growth at round 325.

Only those later games are used here (`results/cheji/versions.json` tags every game).

## 2. How V3 plays

### Opening
- A dragon that starts 4 long splits 2 + 2 on round 0 (597 of 1124 round-0 splits).
- Long spawn dragons stuck in dead ends escape with chained splits in the first few turns:
  - Dilemma's 11-long: split 9, two steps out, split 2, then die (53 of 54 games).
  - Slithery Fight's 25-long: split 13 on round 0, then split again on the next turns: 7 then 2 (27 of 43 games), or 4, 4, 2 (14).
- Openings mostly repeat from the same spawn. The first 5 moves are identical in 79% of games (median over 74 spawns), and in 90% or more for 29 of the spawns. It differs only when the opponent or pearls differ, so the policy looks deterministic.

### Growth: a swarm of 2s
- **Splitting.** A 4-long dragon splits into 2 + 2. When it has a safe move it does so with probability 0.20 a turn; when trapped, 0.88.
  - The rate falls as the team grows: 0.55 below 32 dragons, 0.2 above.
  - Longer dragons mostly split k = 2 (keep length−2), or k = length−2 (keep only the head and neck, and the child escapes from the tail end).
- **Team size.** It has about 26 dragons by round 100 and 45 by round 300, and sits at the 64 cap on the big maps (Schooltime, Slithery Fight). Its longest dragon stays at 4–7 until round 325.
- **Pearl rate.** It eats about 71–78 pearls per 50 rounds, against 55–64 for its opponents.

### Moving
- Its moves are almost never fatal (99.7%). The exceptions are deliberate head-ons.
- With a pearl in view, it takes a shortest-path step toward the nearest one 78% of the time. It takes the move with the most room 84% of the time, and walks into a dead end only when nothing better exists (1.4%).
- It has no preference for any compass direction. When options tie it goes straight half the time, otherwise left or right equally. It never avoids tiles an ally could also reach.

### Head-on trades
Its small dragons ram enemy heads. This table shows the probability of moving into an enemy head that is already adjacent, by our length (rows) and the enemy's length (columns):

| our len \ enemy len | 2 | 3 | 4 | 5 | 6+ |
|---|---|---|---|---|---|
| 2 | 0.34 | 0.65 | 0.72 | 0.73 | 0.9–1.0 |
| 3 | 0.27 | 0.67 | 0.58 | 0.67 | 1.0 |
| 4 | 0.21 | 0.67 | 0.43 | — | — |

- **Rule:** it attacks when the enemy is at least as long as the attacker, and nearly always when the enemy is 6+ long. That makes a long enemy dragon a magnet.
- **Volume:** it starts 23.6 head-ons a game against enemies, and suffers 15.7. 43% of its head-ons come from sprints.
- **Pearls:** it eats 46% of the pearls the enemy dies into on these trades.

### Sprints
- It sprints 97 times a game, mostly 2 steps with a 3-long dragon.
- 39% of sprints eat a pearl, 10% end in a head-on, and 70% turn a corner.
- A 3-long dragon sprints with probability 0.11 a turn when an enemy head is 2 away, against 0.01–0.04 otherwise. The rate also rises with the number of pearls in view.

### Recycling: deliberate no-action deaths
A dragon that sends no action dies and drops a pearl on every second segment.
- **Volume:** cheji does this 109 times a game, mostly with 2- and 3-long dragons.
- **Where the pearls go:** its own dragons eat 93% of those pearls, with a median delay of 4 rounds.
- **When:** 65% of these deaths are dragons with no safe move. The rest are deliberate, and they become common after round 325, mostly when an ally head is 1–2 tiles away, so the pearl lands in front of the ally.

### Endgame: switch at round 325
- **No more splitting.** Between rounds 320 and 330 the split rate of a 4-long dragon drops from about 0.19 to 0.05 a turn. On most maps it drops from 0.7 to 0.03 within that 25-round window.
- **Culling.** At the same time, 2-long dragons with a free move start dying on purpose: 0.4% a turn before, 2–3% after, and 7% next to an ally head.
- **Growth of the longest.** In games that reach round 500, the longest goes from 7 at round 300 to 12 at 350, 15 at 400, and 19 at 500. The dragon count falls from 47 to 34.
- **Nothing coordinates this.** V3 has no sonar, so no dragon knows which one is the longest. The switch is a fixed round, and the longest dragon is simply whichever grew most.

## 3. Where it wins and loses

| Map | W–L | How |
|---|---|---|
| Queen Of Spades | 45–3 | eliminates |
| Trophy | 39–11 | eliminates |
| Default | 32–10 | mostly eliminates |
| Schooltime | 34–11 | swarm plus a big king (longest 32 at the end) |
| Prisoners Dilemma | 36–18 | eliminates |
| Autarky | 32–13 | eliminates |
| Devil | 29–15 | eliminations both ways |
| Trauma | 30–20 | round limit, split |
| **Portals** | **6–35** | all 41 games reach round 500. In losses, longest 13.5 against 30.8 |
| **Slithery Fight** | **7–36** | all reach round 500. In losses, longest 20.5 against 41.2, even with total length 351 against 106 |

- **Losses at round 500 (112):** cheji's longest averages 16 against the opponent's 33, though its total is higher (182 against 79).
- **Wins at round 500 (69):** longest 25 against 15.
- **Opponents who do best against it** (cheji's record): SSS 11–14, Team SKKU and Sabotage-d 23–17 each, calc 21–14, forgot to mention 19–16.

## 4. How to beat it

1. **Don't get eliminated.** Two-thirds of its wins are wipe-outs by about round 160. Keep enough dragons alive, and away from its swarm, on Queen Of Spades, Trophy, Default, Dilemma and Autarky.
2. **Win the longest-dragon tiebreak.** From round 500 it averages only 16–19 for its longest. A protected king of 25–30+ wins most games that reach the limit, and its total length doesn't matter.
3. **Keep the king's head out of reach of its small dragons.**
   - A 2-long cheji dragon rams any adjacent head of length 3+ with probability 0.65–1.0.
   - A 3-long one sprints 2 steps to reach a head 2 away.
   - So the king should never end a turn with a small enemy head within 2 steps (3 for a 4-long enemy). Kill or avoid those heads first.
4. **Trade heads only with care.** It eats 46% of the pearls our dragons drop in head-ons, nearly as many as we do (52%).
5. **Expect more wipe-out attempts after round 325**, when its growth starts. The two maps it can't crack (Portals, Slithery Fight) are exactly the ones where the opponent builds a 30–40 king.

## 5. Ideas to test in our bot

Compared with v4.2 (`bots/v4.2/params.hpp`). Each is an A/B against v4.2 over 160+ games.

1. **Earlier growth switch.** cheji stops proactive splitting at round 325; v4.2 consolidates at 380 (`CONSOLIDATE_ROUND`). Try 320–340.
2. **Split rate tied to team size.** cheji's 4-long dragons split on about 55% of turns below 32 dragons and 20% above, but on 88% when trapped. v4.2 splits whenever both halves are safe. Try stopping proactive splits above 32–40 dragons, so the swarm eats instead of splitting.
3. **Local recycling without a king.** From round 325, a 2-long dragon next to any longer ally dies in front of it (cheji: 7% a turn when an ally head is adjacent), and 93% of those pearls go to the team. v4.2 feeds only a king it knows about through sonar, from round 380.
4. **Cheap kamikaze trades.** A 2–3-long dragon rams any enemy head at least as long as itself. v4.2 needs 6+ dragons (`TRADE_MIN_UNITS`) and a length gain. Try allowing these trades whenever we have plenty of dragons.
5. **Pearl sprints.** A 3-long dragon sprints 2 steps to reach a pearl or head first; 39% of cheji's sprints eat a pearl. v4.2 sprints only to strike.
6. **Chained escape splits on round 0.** cheji splits the 25-long Slithery Fight spawn three times in three turns (13, then 7, then 2); on Dilemma it's split 9, two steps, split 2.
7. **Check what sonar is worth.** V3 dropped sonar and went from 47% to 63%, though other changes came with it. Run v4.2 with `SONAR=false` to see what our sonar is worth (feeding depends on it).
8. **Guard the king against rammers.** cheji's small dragons ram adjacent heads of length 3+ with probability 0.65–1.0, and sprint in from 2 away. Never let the king end a turn within 2 steps of a small enemy head.

## 6. Behaviour-cloned model

We trained a network (`tools/bc/train.py`, default size, 8 epochs, about 12 minutes on the 16-thread CPU here). Its job is to predict cheji's action from the dragon's 7×7 view, its length, round, age and team size.
- **Data:** 1.16M V3 turns for training, and 69K turns from 46 games held out for validation.
- **Validation accuracy:** 73.4% (weighted to true action frequencies). It was still rising slowly at epoch 8.
- **Moves:** "straight ahead" is caught 88% of the time, turns about 62%.
- **Splits:** caught 60%, and right 89% of the times it predicts one. The split size is exact 93% of the time.
- **No-action deaths:** caught 60%, right 91%.
- **Sprints:** caught only 2–6%. They are rare (4% of turns), so the network seldom picks them.

The weights are in `results/bc/cheji_v3/model/` and copied into `bots/cheji_clone`, which plays the most likely legal action every turn.

**As a bot it is weak.** Against v4.2 on the 10 ladder maps (one seed, both sides) it went 3–17. When it sampled from the probabilities instead (temperature 1) it went 1–19. Per-turn imitation at 73% is not enough:
- It under-splits: 22 dragons at round 200, against about 40 for the real V3.
- It almost never sprints: 6 a game, against 97.
- It lacks whatever memory the real dragons keep.

It is still useful as a rough cheji-style opponent, but don't read its results as cheji's strength.

## 7. Reproduce

Scripts (`results/` is git-ignored, so the analysis scripts live there; the reusable tools are in `tools/`):
- `tools/team_games.py` lists a team's games from the public Games page and downloads their replays (`--ranked --replays`).
- `tools/replaylib.cjs` replays a game event by event (bodies, pearls, edges and portals).
- `results/cheji/profile.cjs` → `analyze.py`, `king.py`, `openings.py` produce the per-game profile and summaries. `versions.py` finds the version change, and the V3 subset is `profile_v3.jsonl` (`*_v3.txt` has the outputs).
- `results/cheji/turns.cjs` → `rules.py` produce per-turn features and the decision tables (`turnsT_v3.csv`, `rules_v3.txt`).
- `tools/bc/` is the behaviour-cloning kit (see its README). The V3 dataset is `results/bc/cheji_v3` (73 MB).
