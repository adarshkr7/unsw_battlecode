# Cutlery's bot, reverse-engineered from ladder replays

**Team:** Cutlery (id 306), C++, rank 1 at 2121 on 28 Sep 2026 (peak 2135).
**Data:** their 98 ranked battles (486 games) completed between 26 Sep 14:10 UTC and 28 Sep 10:30 UTC, a
record of 303–183 (62%). The per-turn analysis uses 60 of those games (6 per map, 3 from each side):
545,000 Cutlery dragon turns. The same sonar key runs through the whole window, so it is most likely one bot
throughout.
**How:** `results/cutlery/scripts/extract.cjs` rebuilds the board each dragon saw on its own turn, event by
event from the replay, and records its view, its four options, its action and the outcome. The game list, the
replays and all scripts are in `results/cutlery/` (gitignored).

---

## The algorithm on one page

```
every dragon, every turn (7x7 view; no pearl knowledge beyond it):

  if no safe move:                            self-destruct (illegal action)          # 98.8%
  if len <= 3 and an enemy head is adjacent:  ram it, more often the longer it is    # table in 4
      (sometimes a 2-3 step sprint into an enemy head)

  PHASE 1, rounds 0-259 (swarm):
      if len >= 4 and team < 64 dragons:      SPLIT(len - 2)   # head keeps 2, the rest leaves backwards
                                                                # from the tail; same turn it reaches 4 (74%)
  PHASE 2, rounds 260-500 (growth):
      stop splitting (65% -> 3% within 5 rounds, same round on every map)
      grow long dragons for the longest-dragon tiebreak
      short dragons sometimes self-destruct, often next to a long ally (it eats the pearls)

  move = best of the safe options by a score:
      away from the nearest ally head       (strongest term)
      toward the nearest visible pearl      (BFS inside the view)
      not next to an enemy head, not next to an ally head
      not into a pocket (room < 5)
      not onto a tile whose pearl is due within 2 rounds (standing there blocks the spawn)
      keep going straight
  nothing in view: straight 63%, left 19%, right 18%

  every turn: up to 4 sonar pings, XOR-encrypted per (round, team), carrying dragon positions (see 7)
```

The fitted move score picks their move 61% of the time (random choice among the safe moves would be about 40%).
With nothing in view the choice looks partly random (per-dragon seeded RNG), which caps any rule-based fit.
Trapped dragons self-destruct 98.8% of the time, and a dragon that reaches length 4 splits within one turn 86%
of the time.

---

## 1. A swarm of short dragons

Nearly half of all Cutlery turns (48%) are made by length-2 dragons and 28% by length-3. On other maps they
out-split their opponents (median 222 splits a game against 162), and on Schooltime and Slithery Fight they sit
at the 64-dragon cap for about half the rounds.

**Opening.** Length-4 spawns split 2+2 on turn 0 (Default, Devil, Schooltime, Trauma). Long spawns bud 2 off the
tail every turn (the 14-long on Autarky, the 11-long on Dilemma). Length-3 spawns just walk (they cannot split).

## 2. The split rule

| Length | Child size when splitting (all rounds) |
|---|---|
| 4 | 2 (the only legal split) |
| 5 | 3 in 68%, 2 in 23% |
| 6 | 4 in 74%, 2 in 17% |
| 7–11 | length − 2 in 75–85% |

- The usual split is SPLIT(len − 2): the head keeps 2 and the rest becomes a child that leaves backwards from the
  old tail. A dragon that eats its way to length 4 splits on that same turn 74% of the time, within 1 turn 86%.
- Splits are rarer when the tail has no free neighbour, because the child would be stuck: 5.5% of turns,
  against 8–13% otherwise.
- At 60+ dragons the split rate falls to 4%.

## 3. Movement

Pairwise logistic model over the safe options (406,000 decisions). The biggest weights, in units of log-odds:

| Term | Weight |
|---|---|
| Distance from the nearest ally head (farther is better) | +1.2 to +1.6 |
| On the shortest path to the nearest visible pearl | +0.8 to +1.1 |
| Next to an enemy head | −1.0 |
| Next to an ally head | −0.8 |
| Into a pocket with room < 5 | −0.9 |
| Onto a tile whose pearl spawns within 2 rounds | −0.6 |
| Keep going straight | +0.5 |

- **They spread out.** Moving away from teammates matters more than pearls.
- **They keep spawn tiles clear.** A pearl only spawns on an empty tile, so they avoid standing on one that is
  about to spawn.
- **No sign of pearl knowledge beyond the view.** Heading for the nearest pearl anywhere on the map gets no
  weight once the local terms are in.
- **Exploration.** With no pearl or head in view and open space: straight 63%, left 19%, right 18%; every
  compass direction about 25%.

## 4. Aggression: head-on trades

Short dragons ram enemy heads. When a move into an enemy head is available (both die, each dropping half its
length as pearls), the share of turns they take it:

| Own \ enemy length | 2 | 3 | 4 | 5 | 6+ |
|---|---|---|---|---|---|
| 2 | 31% (845) | 50% (313) | 68% (62) | 54% (13) | 100% (17) |
| 3 | 30% (379) | 55% (98) | 60% (15) | 50% (8) | 56% (9) |
| 4 | 12% (68) | 24% (17) | 33% (6) | – | – |
| 6+ | 3% (132) | 0% (20) | 40% (5) | – | 38% (8) |

The longer the enemy, the more eager they are: trading a 2 for a 4 is a gain. Dragons of length 4 or more
rarely ram. **Sprints are mostly attacks:** of 2,811 sprints in 60 games, 1,199 (43%) ended in a head-on kill.

## 5. Deaths

- They never die to kelp or to their own body.
- **Trapped dragons self-destruct.** With no safe move they send an illegal action 98.8% of the time. The
  engine records this as death reason `A` (no valid action). Before round 260 every such death is a trapped
  dragon.
- After round 260 there are also about 29 self-destructs a game with a safe move available. 20% of them are
  within 2 tiles of an ally head of length 6+, five times the base rate, so they recycle short dragons into long
  ones.

Note on death codes in the replays: `W` kelp, `S` own body, `O` another dragon's body, `H` head-on,
`A` no valid action.

## 6. Endgame: switch at round 260

| Round | 0–259 | 260 | 261 | 262 | 263 | 264 | 265+ |
|---|---|---|---|---|---|---|---|
| Split rate, length ≥ 4, below the cap | 60–80% | 27% | 25% | 15% | 18% | 8% | 3% |

The drop happens at round 260 on every map with enough data (Default, Devil, Portals, Queen Of Spades, Slithery
Fight, Trauma), so it is a fixed round, not a per-map setting. After it the median longest Cutlery dragon grows
from 3–4 (rounds 0–200) to 7 at R300, 26 at R400 and 38 at R490. The longest dragon almost never splits after
R400 (1%). Several final kings were born late as the long child of a split (length 31–69).

## 7. Sonar

Every dragon sends up to four pings a turn (about 21,000 a game).

- **Encryption.** Each 64-bit value is XORed with a key that depends on the round and the team, not the game.
  Round 0 on side A reads `3b05fd…` in every game. The low 16 bits look like a checksum of the rest: identical
  payloads give identical values.
- **Layout** (after decryption):

| Bits | Field |
|---|---|
| 16–21 | y |
| 22–27 | x |
| 28–33 | length |
| 34–36 | subtype |
| 37–42 | dragon id (low 6 bits) |
| 43–46 | a counter that grows through the game (hop count or age?) |
| 59–61 | message type |
| 63 | flag |

- **Type 0: dragon reports**, 58% of messages. 21% of them carry the sender's own post-move head, length and
  id. 50% relay another Cutlery dragon's exact state, 0–5+ rounds old. The rest match nothing within 12 rounds
  (older relays, or noise in the key estimate). So the swarm knows roughly where its teammates are.
- **Types 5 and 7: map broadcasts.** Constant payloads repeated by many dragons for hundreds of rounds, and
  identical across games on the same map (for example 0x224 and 0xa4be on both Autarky games). They encode
  something fixed about the map. The layout differs from reports and is not decoded.
- About 4% of their pings stop on one of our dragons. With a per-(round, team) key table taken from their
  replays, our bot could read those reports. The key may change with their next upload.

## 8. Where it wins and loses (all 486 games)

| Map | W–L | Map | W–L |
|---|---|---|---|
| Autarky | 42–6 | Slithery Fight | 36–13 |
| Queen Of Spades | 44–12 | Trauma | 30–19 |
| Schooltime | 28–9 | Trophy | 24–21 |
| Portals | 36–19 | Default | 23–27 |
| Prisoners Dilemma | 31–22 | Devil | 8–35 |

They win where their swarm out-grows the opponent's and lose where it cannot. On Devil they stay in their own
third (x 0–12) during the first 50 rounds, while the teams that beat them take the centre corridors. There they
split 84 times a game (median) against the opponent's 268, and are usually wiped out.

## What this means for us

1. **Same family as 龙虎豹 and cheji bt:** a swarm of 2s, a fixed switch to growth, and recycling into long
   dragons. Cutlery's version: SPLIT(len − 2) rather than budding 2s, a global switch at round 260 rather than
   per map, and spreading away from allies as the main move term.
2. **Their short heads are lethal to our long dragons.** They ram 50–100% of the time when our dragon is longer.
   Our safety check should treat tiles next to a short enemy head as deadly.
3. **Their king comes late.** Until round 260 they have no dragon longer than about 4. After it, one
   dragon grows to 26–38 and decides the longest-dragon tiebreak. Our short dragons trading heads with it is the
   best swing available.
4. **Contest the centre early on corridor maps.** On Devil their swarm never starts when the centre is taken
   first.
5. **Copyable ideas:** the split-when-you-reach-4 rule, keeping spawn tiles clear, spreading away from allies, and
   trapped dragons self-destructing (it is free).

## Reproduce

```
<unswbc python> results/cutlery/scripts/collect.py 306 results/cutlery BATTLE_ID ...   # records + replays
node results/cutlery/scripts/extract.cjs results/cutlery/replays OUT GAME:SIDE ...      # per-turn views
python results/cutlery/scripts/analyze1.py .. analyze7.py                               # rules above
python results/cutlery/scripts/sonar8.py                                                # sonar decoding
```

The analysis scripts read `ext/` and `sample.txt` from their working directory. `sample.txt` lists the 60 games.
