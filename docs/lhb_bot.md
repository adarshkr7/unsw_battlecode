# 龙虎豹's strategy, reverse-engineered from ladder replays

**Team:** 龙虎豹 (id 470), C++, three players (上海交通大学, East China University of Political Science and
Law, Fudan). Rank 5 at 2054 on 28 Sep 2026; peak 2197 on 27 Sep.

**Data:** their 170 ranked ladder games from 26 Sep 16:49 UTC to 28 Sep 04:00 UTC, with their strong bot
(114–56), which is 1.96 million dragon turns. All games are scheduled ladder matches: they stopped accepting
ranked challenges on 25 Sep. A later bot (38 games from 28 Sep 04:00, 3–35) is covered in section 12.

**How:** `tools/turns.cjs` rebuilds, event by event, the exact board each of their dragons saw at the
start of its turn, then records its 7×7 view, its action and the outcome. The analysis scripts are in
`results/lhb/` (gitignored); section 15 lists them. Every number below comes from those 170 games unless
it says otherwise. "Midgame" means before the map's endgame round (section 9).

---

## 1. At a glance

- **A swarm of length-2 dragons.** 56% of their dragon turns are at length 2 and 31% at length 3. A dragon
  that reaches length 4 splits into 2+2 on its next turn about 90% of the time, until the team hits the
  64-dragon limit.
- **Cheap, constant losses.** They lose about 220 dragons a game, almost all length 2–3 dragons boxed in
  with no exit. Each drops a pearl, and the swarm eats it back.
- **Simple movement:** never into anything deadly, toward the nearest pearl in the 7×7 view, away from
  pockets and from other heads, otherwise straight on.
- **Kamikaze small dragons.** A length-2 dragon next to an enemy head that is at least as long rams it
  about half the time. Sprints are used only to strike heads or to die.
- **Built-in map knowledge:** scripted openings per map and spawn, and an endgame that starts on a fixed round per map.
- **Endgame:** splitting stops, every dragon sends 4 encrypted sonars a turn, and small dragons die just in
  front of a long ally's head to feed it. Their longest dragon goes from about 7 at round 300 to about 39 at 500.

---

## 2. The turn, as pseudo-code

Every dragon runs this independently each turn (each is its own process with a 7×7 view). The numbers are
observed rates, not their code.

```
if spawn dragon and its map/spawn has a script, and the scripted action is legal and safe:
    play the scripted action for this round                          # section 3

endgame = round >= ENDGAME_ROUND[map]                                  # 336 / 359 / 364 / 368 (section 9)
if endgame:
    send 4 sonars (N, E, S, W), encrypted                            # section 10
    if length <= 3 and a long ally's head is within 2 squares:
        die on a square next to it (step into own tail/body, or a 1-step move that can't be paid for)

if the head has no free neighbour:
    if length >= 4 and team < 64 dragons: SPLIT(length - 2)            # 98-100%; reversal, section 5
    else: move into kelp (58%), own tail (23%) or own body (14%)       # die alone, rarely into an ally head

if not endgame (+ about 15 rounds) and team < 64 dragons:
    bud a 2-long child off the tail (SPLIT 2) with probability by length:
        4: ~75-90%   5: 27%   6: 20%   7: 26%   8: 13%   9: 17%   10: 9%   11+: 2%
    less often in a one-wide kelp tube (49% vs 86%) or when the tail has no free square (39% vs 85%)

if length <= 2 and an adjacent enemy head is at least as long: ram it     # ~50%
rarely (1-6%): a 2-step sprint into an enemy head two squares away

otherwise move by score (section 6):
    deadly (kelp, any body, an ally's head)     -6.1   (never unless forced)
    first step toward the nearest pearl in view +2.1
    free area behind the move < 5 squares       -1.8
    ends next to an enemy head                  -1.6
    ends next to an ally head                   -1.4
    straight ahead                              +1.1
```

A 5-step version of the move rule (ram, drop pockets and enemy-adjacent squares, avoid ally heads, nearest
pearl, straight) matches 73% of their midgame moves. A fitted model on the same features matches 75%. The
split rules hold about 90% of the time.

---

## 3. Openings

On the ladder they almost always had the same spawn side (200 of 208 games), and their spawn dragons repeat
the same moves across games even though pearls spawn at random. **These look scripted per map and spawn
position, or planned from built-in map data.** The table shows the longest move prefix that at least 60% of
games share (`P<k>` = split off k; `EE` = 2-step sprint):

| Map | Spawn | Len | Games | Scripted prefix |
|---|---|---|---|---|
| Autarky | (7,1) | 3 | 18 | `W W W W W W S S S S S S S E` |
| Autarky | (7,15) | 3 | 18 | `W S W W W W W N N N N N N E E E E E` |
| Autarky | (18,2) | 14 | 18 | `N N E P12 P3 E` |
| Autarky | (22,2) | 3 | 18 | `S S` |
| Autarky | (22,17) | 3 | 18 | `N N N N N N` |
| Autarky | (30,6) | 3 | 18 | `W W W W W W W W` |
| Default | (0,0) | 4 | 18 | `P2 S S S S S S S` |
| Default | (4,0) | 4 | 18 | `P2 S E E E S S S S S S` |
| Default | (8,0) | 4 | 18 | `P2 N N W W W W` |
| Default | (12,0) | 4 | 18 | `P2 N W W W W W` |
| Devil | (3,5) | 4 | 10 | `P2 E S S E E E E E E E E E N N N E E S` |
| Devil | (3,8) | 4 | 10 | `P2 E E E E E E E E E E S S S E E N` |
| Devil | (3,11) | 4 | 10 | `P2 E N N E E E E E E E E S S S E E E S S S E P2 N W S E P2 E` |
| Portals | (0,5) | 3 | 17 | `E E E N N W W N N E E E E P2 W` |
| Portals | (1,11) | 3 | 17 | `S S S E N N N N N N N N E S P2 W N P2 E S S S S S S S S S P2 W N P2 E S W W` |
| Portals | (12,1) | 3 | 17 | `N E E E S S S S S S S P5 E E` |
| Prisoners Dilemma | (3,6) | 3 | 16 | `E E E E S S E E E E E` |
| Prisoners Dilemma | (3,12) | 3 | 16 | `E E E N N N W W W W W S E E E E S E N W S E N E N E E E E` |
| Prisoners Dilemma | (14,2) | 11 | 16 | `N N P8 P3 N` |
| Prisoners Dilemma (new version) | (15,14) | 2 | 6 | `N N N N` |
| Prisoners Dilemma (new version) | (19,6) | 2 | 6 | `S` |
| Queen Of Spades | (2,9) | 3 | 17 | `N N N N N E E N` |
| Queen Of Spades | (5,11) | 3 | 17 | `S S S S S S W W W S S S` |
| Schooltime | (3,2) | 4 | 17 | `P2 S S S S S S S S S W W W W S W W W W W W W` |
| Schooltime | (16,3) | 4 | 17 | `P2 W W S S S S W W W W N N N W W N N W W W W W W` |
| Schooltime | (23,3) | 4 | 17 | `P2 W W W W W W W W W N N W W W W W W W W W W W W W W` |
| Slithery Fight | (3,22) | 25 | 21 | `P23 E S W N N E S W W S S` |
| Slithery Fight | (8,10) | 7 | 21 | `P2 P2 N P2 N N` |
| Slithery Fight | (15,13) | 2 | 21 | `W W W W W N N W` |
| Slithery Fight | (18,7) | 4 | 21 | `P2 W W P2 W` |
| Slithery Fight | (20,4) | 2 | 21 | `N N E N` |
| Slithery Fight | (26,23) | 2 | 21 | `S W` |
| Slithery Fight | (33,2) | 5 | 21 | `P2 N P2 N N` |
| Trauma | (4,2) | 4 | 19 | `P2 E E E E E N N E E S S W N W W W W S S S S S S E P2 P2 P2 E` |
| Trauma | (4,15) | 4 | 19 | `N×15, W×4, S×23, E×4, N×14` (60 rounds, no split) |
| Trophy | (4,2) | 3 | 13 | `E N N N N N E E E E E E` |
| Trophy | (4,22) | 3 | 13 | `E E EE E E E E N W S S S S S` |

Things worth noticing:
- Every length-4 spawn splits 2+2 on round 0, except Trauma (4,15). That one walks a fixed 60-round loop at
  length 4 without splitting, which is against their general rule, so it's map-specific.
- Long spawns split at once: Slithery Fight's 25-long spawn does a reversal (child 23) on round 0, and
  Dilemma's 11-long spawn splits 8 then 3 on rounds 2–3.
- Trophy (4,22) sprints on round 2 in 13 of 13 games.
- Openings mostly run in straight lines with one or two turns: L-shaped routes to a target area.

---

## 4. Swarm growth: budding

**Split sizes.** Every split leaves one piece exactly 2 long: either the child is 2 (a bud off the tail) or
the child is the parent's length minus 2 (a reversal, section 5). They split 257 times a game.

**Bud rate per turn** (midgame, team below 64, head has a free neighbour):

| Length | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11+ |
|---|---|---|---|---|---|---|---|---|
| Buds | 75% | 27% | 20% | 26% | 13% | 17% | 9% | 2% |

- On the **first turn at length 4** they bud about 90% of the time; if not, later turns at 4 bud only
  about 13%, so something persistent is blocking it. The blockers found:
  - **One-wide kelp tube** (kelp on both sides, one exit): 49% instead of 86%. They eat along it first.
  - **Tail boxed in** (the child would be born with no free square): 39% instead of 85%.
  - **The unit limit:** at 64 dragons they never split (31,000 length-4 turns at the limit). The limit
    binds on Schooltime (65% of midgame turns) and Slithery Fight (53%), rarely elsewhere.
- The rate varies by map: length-4 buds per turn are 31% on Dilemma, 50% on Autarky, 60% on Default,
  72–85% on Trauma, Portals, Slithery Fight and Devil, and 97–99% on Queen Of Spades, Schooltime and Trophy.
- More enemy heads in view makes a bud more likely (45% with none, 79% with 3+), as does a boxed-in head.
- **Budding stops around the endgame:** 82–85% (rounds 100–300), 52–58% (300–380), then 4–6% after 380.

**Result:** 87% of turns at length 2–3, 5% at 4. The team grows from 3–7 dragons to about 25 by round 100
and 36–40 by round 300 (59–64 on Slithery Fight, 53 on Devil and Schooltime).

---

## 5. Reversal splits and boxed-in dragons

- When the head has **no free neighbour**, a dragon of length 4+ below the unit limit splits 98–100% of
  the time, with **child = length − 2**. The parent keeps a 2-long stub at the trapped head (it dies next
  turn, dropping one pearl). The child is the old tail end, now leading, and leaves backwards.
- 94% of reversal splits happen with a boxed-in head, against 4% of buds.
- After round 400, every split by a long dragon is a reversal. This is how their king escapes dead ends,
  and why its id keeps changing.
- A boxed-in dragon of length 2–3 can't split, so it dies. It picks kelp (58%), its own tail (23%) or its
  own body (14%), and rarely an ally head (2%; 6% when both kelp and an ally head are next to it). **It dies
  alone rather than taking a teammate with it.**

---

## 6. Movement

**Feature weights** (conditional logit over the four moves; positive = preferred):

| Feature of a move | Midgame | Endgame (370+) |
|---|---|---|
| deadly: kelp, any body, an ally's head | −6.1 | −2.9 |
| into an adjacent enemy head | +2.9 | +2.0 |
| first step of a shortest path (BFS in the 7×7 view) to the nearest pearl | +2.1 | +2.4 |
| free area behind it < 5 squares | −1.8 | −1.8 |
| ends next to an enemy head | −1.6 | −1.5 |
| ends next to an ally head | −1.4 | n/a |
| straight ahead | +1.1 | +1.0 |
| ends 2 squares from an enemy head | −0.7 | −0.6 |
| free area < 15 / free area (per 40 squares) | −0.5 / +0.5 | −0.6 / +0.4 |
| nearest spawn countdown in view / map's spawn-rate hot spots | +0.2 / +0.2 | +0.25 / +0.1 |
| toward their longest dragon | — | +0.2 net |

- **Pearls:** a pearl is reachable inside the view on only 22% of moves. When one is, they step along a
  shortest path to the nearest 85% of the time. They skip about 15% of adjacent pearls. An ally head just as
  close lowers that only a little (77% taken vs 85%), and ally id doesn't matter, so there's **no pearl
  claiming** between dragons.
- **No pearl in view (78% of moves):** straight 54%, left 22%, right 23%, back 1%; 79% straight in fully
  open space. North, east, south and west are equally likely, so there's no global heading.
- **Spacing:** stepping next to an ally head is avoided almost as strongly as next to an enemy head. The
  swarm spreads out.
- **Not used:** spawn countdowns (weight 0.2) and the map's pearl-rich areas (tested with a spawn-potential
  field built from the real spawn rates: weight 0.2 or less). In the midgame they don't move toward their
  longest dragon either.

---

## 7. Combat

**Rams on an adjacent enemy head**, by the rammer's length:

| Our dragon's length | 2 | 3 | 4 | 5 | 6+ |
|---|---|---|---|---|---|
| Rams | 48% | 19% | 10% | 14% | 5% |

By enemy length minus theirs: −3: 15%, −2: 21%, −1: 7%, 0: 44%, +1: 54%, +2: 55%, +3: 40%, +4: 46%, +5: 57%.
So a small dragon rams heads **at least as long as itself** about half the time, and rarely shorter ones.
(A bot only sees an enemy's segments inside its 7×7 view, so its idea of enemy length is a lower bound.)

**Sprints are only weapons or suicides.** They sprint about 25 times a game, and 99.7% of sprints end in the
sprinter's death:
- 47% head-on strikes, always at the exact square an enemy head occupied at the start of the turn
  (length 2–3 strikers mostly, against targets of length 2–12);
- 31% into their own body (deliberate deaths, mostly in the endgame);
- 22% a length-2 dragon ordering 2 steps it can't pay for, which kills it after one step. It's a way to die
  on a chosen square.

With an enemy head exactly 2 squares away, a small dragon strikes only 1–6% of the time. Length 4–5 strikers
only hit longer targets.

---

## 8. Deaths and pearl recycling

- About **220 deaths a game**: 97 into kelp, 87 into themselves, 26 head-on, 5 unpaid sprints, 4 into
  other bodies. Before round 300, **88% had no free neighbour.**
- Most boxed-in deaths are dragons 10+ rounds old wandering into dead ends, not newborns: only 5% of
  children are born trapped.
- Boxed-in deaths before round 300, per game: Trophy 11, Default 12, Queen Of Spades 39, Autarky 57,
  Dilemma 75, Schooltime 112, Trauma 134, Portals 158, Devil 281, Slithery Fight 320.
- **Recycling:** each dead length-2 dragon leaves one pearl. Per game in the midgame they eat 267 spawned
  pearls, **209 dropped by their own dead dragons**, and 26 dropped by enemies. The churn costs little
  length.

---

## 9. Endgame: turning the swarm into a king

**Start round** (the first sonar), fixed per map:

| Map | Endgame starts |
|---|---|
| Devil, Portals, Prisoners Dilemma (32×16) | 336 |
| Queen Of Spades, Slithery Fight | 359 |
| Schooltime, Trauma | 364 |
| Autarky | 368 |
| Default, Trophy | sonar from round 3–8; consolidation from about round 300–330 |

**What changes:**
- Budding stops (split rate at length 4: 80%+ → 4%); reversal splits continue.
- 98% of turns send 4 sonars, one each way, all with different values (section 10).
- **Feeding:** deliberate deaths (dying with a free exit available) rise from 12% of their deaths in the
  midgame to 44%. 44% of those small-dragon deaths happen **within 2 squares of an ally head of length 8+**,
  against 0.7% of other small-dragon turns. The feeder steps next to the long dragon's head and dies there
  (into its own tail or body, or with an unpaid sprint), leaving a pearl in its path.
- Feeding is opportunistic: small dragons don't travel to the king (50% of their moves get closer), and
  within 2 squares they move away (22% closer).
- Of what their 21+ dragons eat in the endgame, 74% are pearls dropped by their own dead dragons.

| Round | Their dragons | Their longest | Their total | Opponent longest | Opponent total |
|---|---|---|---|---|---|
| 100 | 27 | 5.7 | 66 | 4.9 | 57 |
| 200 | 36 | 6.8 | 94 | 6.5 | 90 |
| 300 | 40 | 7.5 | 109 | 8.4 | 107 |
| 350 | 37 | 12.8 | 112 | 12.3 | 109 |
| 400 | 30 | 18.1 | 119 | 18.1 | 106 |
| 450 | 12.5 | 31.0 | 104 | 25.4 | 102 |
| 500 | 7.5 | 38.8 | 100 | 32.5 | 100 |

(Means over games still running at that round; 114 of the 170 reached round 500.)

---

## 10. Sonar

- None in the midgame (except Default and Trophy). From the endgame round, every dragon sends exactly 4
  pings a turn with 4 different values.
- **Encrypted:** all 64 bits are unbiased, and no value repeats across games. Within a game, two dragons
  sometimes send the same value in the same round, so the key probably depends on the round and content,
  not the sender. We can't read it.
- In a sampled game, 64% of pings hit kelp, 25% an ally body and 11% an ally head; none reached an enemy.
- What it's for is unknown. Behaviour doesn't show movement toward a shared target, so it may carry "who is
  the king" for the feeding decision.

---

## 11. Per-map profile

| Map | W–L | Dragon turns / game | Splits / game | Length-4 buds | Deaths / game | At 64 | Dragons @100 | @300 | Longest @300 | @400 | @500 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Autarky | 9–9 | 11,371 | 155 | 50% | 115 | 10% | 24 | 40 | 3.6 | 5.7 | 31.5 |
| Default | 12–6 | 8,108 | 107 | 60% | 68 | 0% | 16 | 33 | 3.2 | 11.9 | 25.6 |
| Devil | 10–0 | 11,326 | 459 | 85% | 400 | 1% | 30 | 53 | 5.8 | 7.3 | 39.7 |
| Portals | 15–3 | 10,955 | 279 | 74% | 267 | 0% | 25 | 35 | 5.8 | 31.2 | 41.1 |
| Prisoners Dilemma | 11–6 | 4,484 | 130 | 31% | 118 | 0% | 12 | 21 | 9.1 | 9.3 | 27.4 |
| Queen Of Spades | 10–8 | 4,503 | 127 | 99% | 98 | 0% | 8 | 16 | 3.4 | 5.3 | 21.4 |
| Schooltime | 8–10 | 17,907 | 349 | 99% | 265 | 65% | 39 | 53 | 8.3 | 15.1 | 40.9 |
| Slithery Fight | 15–6 | 24,196 | 548 | 75% | 514 | 53% | 59 | 64 | 9.8 | 30.5 | 51.7 |
| Trauma | 17–2 | 14,521 | 315 | 72% | 288 | 0% | 25 | 40 | 15.8 | 20.1 | 46.1 |
| Trophy | 7–6 | 2,831 | 77 | 97% | 37 | 2% | 19 | 8.5 | 3.0 | 4.5 | 13.0 |

("Length-4 buds" is the per-turn rate in the midgame below the limit with a free exit; "At 64" is the share
of midgame turns at the unit limit; longest @500 is over games that reached round 500.)

- **Strongest:** Devil (10–0), Trauma (17–2), Portals (15–3), Slithery Fight (15–6).
- **Weakest:** Schooltime (8–10), Autarky (9–9), Trophy (7–6), Queen Of Spades (10–8).
- On **Trophy the swarm collapses** after round 200 (30 dragons → 8 by round 300) and the king only reaches 13.

---

## 12. The 28 Sep bot (after 04:00 UTC, 3–35)

| | Old (170 games) | New (38 games) |
|---|---|---|
| Dragon turns per game | 11,538 | 6,991 |
| Splits per game | 257 | 133 |
| Length-4 bud rate | 79% | 87% |
| Rams an adjacent enemy head | 32% | 21% |
| Sprints per game | 25 | 13 |
| Deaths per game | 220 | 110 |
| Endgame start: Default / Trophy / Schooltime | 3 / 6–8 / 364 | 330 / 332 / 340 |

The split rule, split sizes and pearl chasing are unchanged. It lost on maps whose settings didn't change
too, so the regression is mainly in midgame survival. Expect them to fix or revert it.

---

## 13. How to play against it

- **Keep long heads away from their small heads.** A length-2 head next to our long dragon rams about half
  the time when it thinks we're at least as long, a 2-for-N trade. They also strike from 2 squares away now and then.
- **Their king is late.** Their longest is about 6–8 until the endgame round, and 39 on average at round
  500: weaker on Trophy (13), Queen Of Spades (21), Default (26), Dilemma (27) and Autarky (31); strongest on
  Slithery Fight (52), Trauma (46), Portals (41), Schooltime (41) and Devil (40). A king of 40+ at round 500
  wins the tiebreak in most games.
- **The endgame depends on feeders reaching the king's head.** Hunting their king from the endgame round,
  or killing small dragons near it, cuts its growth. The king escapes dead ends by reversal splits, which
  cost it 2 each time.
- **No coordination before the endgame.** No sonar, no pearl claiming, only spacing, so contested pearl
  areas are decided by local greed.
- **Unknown maps.** Openings and the endgame round are tied to known maps. On new maps (the knockouts) we
  don't know what they fall back to. Their endgame timing may be off there.

## 14. Ideas worth copying

- **Budding 2+2 at length 4** up to the unit limit. It keeps a large, cheap swarm, and our known weakness
  is early splitting rate.
- **The reversal split** (child = length − 2) when the head is boxed in.
- **Dying alone when trapped** (kelp or own body, never an ally's head).
- **Spacing:** avoid ending next to an ally head.
- **Endgame feeding:** small dragons die just in front of a long ally's head.

---

## 15. Limits of this model, and how to reproduce it

- The rules describe what they do, not why. The move model explains 75% of moves; the rest depends on
  something we don't capture: a better safety check, lookahead, or randomness.
- A quick sparring bot built from these rules (not kept) grew too slowly and lost 6–34 to v4.2. Its
  per-turn rates matched on Schooltime, but it had about 15 dragons at round 100 against their 26. So the
  rules capture the shape of the strategy, not all of its strength.
- The sonar content, the Default/Trophy early sonar, and exactly how the king is chosen are unknown.

**Reproduce:**
```
<unswbc python> tools/team_games.py 470 --since 2026-09-21T00:00Z --ranked      # game list
node tools/turns.cjs results/teams/龙虎豹 --until 2026-09-28T04:00 > results/lhb/before.jsonl
python results/lhb/load.py before                                               # -> before.pkl
python results/lhb/choice.py results/lhb/before.pkl 0 330                       # move weights
python results/lhb/endgame.py results/lhb/before.pkl                            # feeding
node results/lhb/origin.cjs results/teams/龙虎豹 > origin.jsonl                  # pearl origins
python results/lhb/compare.py                                                   # old vs new bot
```
Replays are in `results/teams/龙虎豹/` (download with `team_games.py … --replays` or
`fetch_replays.py`). `results/lhb/openings.json` has the opening scripts, and
`results/lhb/real_fingerprint.json` the per-map profile.
