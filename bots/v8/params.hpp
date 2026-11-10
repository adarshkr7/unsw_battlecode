#pragma once

// Tunables. The tournament script compares copies of the bot that differ only here.
namespace params {

// Log every decision (local debugging only: output costs points in the judge).
inline constexpr bool DEBUG = false;
// Use the built-in ladder maps (walls, portals, spawn rates, start bodies). Off = play as on an
// unseen tournament map, learning only from what we see.
inline constexpr bool USE_MAP_DATA = true;
// Recognise a map by walls and portals; tolerate this many visible tiles whose pearl spawning
// differs from our copy (the ladder changes maps). 0 = the old strict match.
inline constexpr int SPAWN_MISMATCH_OK = 3;
// Round 0: if the start body is still unknown, assume the straight line behind the head.
inline constexpr bool STRAIGHT_START = true; 

// --- Phases ---------------------------------------------------------------
// Swarm phase: split in half whenever this long, up to the team's dragon cap. Many small
// dragons gather pearls in parallel, cannot all be caught, and trade heads cheaply.
inline constexpr int SWARM_SPLIT_LEN = 4;
// Split even on a turn we could eat (the parent then waits a turn). v2d found ~20% more
// dragons and ~15% more total length by round 150 this way.
inline constexpr bool SPLIT_WHILE_EATING = true;
// Chained splits (from v2d): count a stuck child of 4+ splitting again on its first turn.
inline constexpr bool CHAIN_SPLIT = true;
// No swarm split while an enemy head can reach ours (the parent stands still for a turn).
inline constexpr bool THREAT_ON_OUR_HEADS = false;
// Delay a swarm split whose child could neither see its body nor be told it by sonar.
inline constexpr bool SPLIT_WAITS_FOR_HANDOFF = true;
// 10. When no half is safe, also try budding 2 off the tail and the reversal (child = length - 2).
inline constexpr bool SWARM_SPLIT_CUTS = true;
// From this round on: no more swarm splits; small dragons feed pearls to a big ally, so we
// finish with one long dragon (the winner at round 500 is the team with the longest one).
inline constexpr int CONSOLIDATE_ROUND = 330;
// From CONSOLIDATE_ROUND a dragon of KING_ROOM_LEN+ judges escape splits by its longest surviving
// piece, not their sum (the ladder game we lost 19-20 had our 24-long king split itself to 12).
inline constexpr bool ENDGAME_KEEP_WHOLE = true;

// Insurance: losing every dragon loses the game outright, so keep at least this many alive
// by splitting a small child off any dragon long enough to spare it.
inline constexpr int MIN_UNITS = 2;
inline constexpr int INSURANCE_MIN_LEN = 8;
inline constexpr int INSURANCE_CHILD = 3;

// --- Head trades (moving onto an enemy head kills both dragons) -------------
// Take an even trade (same length) only while we have at least this many dragons.
inline constexpr int TRADE_MIN_UNITS = 12;
// Longest sprint we plan in order to reach an enemy head (a length-L dragon can do L-1 steps).
inline constexpr int STRIKE_MAX_STEPS = 6;
// Standing where an enemy head can reach us next turn: per segment we would lose more than it.
inline constexpr double THREAT_PER_LEN = 60.0;
// ... and a small penalty when the trade it could take would be even.
inline constexpr double THREAT_EVEN = 15.0;
// Hunting: pull towards enemy heads worth trading with, per segment gained (capped).
inline constexpr double HUNT_ATTRACT = 60.0;
inline constexpr double HUNT_GAIN_CAP = 10.0;
inline constexpr int HUNT_MEMORY = 8; // rounds a sighting stays worth chasing
// Aggression: with at least AGGRO_RATIO times as many of our heads in view as theirs (and a few
// dragons to spare), take even trades and chase harder.
inline constexpr double AGGRO_RATIO = 2.0;
inline constexpr int AGGRO_MIN_UNITS = 3;
inline constexpr double HUNT_ATTRACT_AGGRO = 200.0;

// --- Consolidation (feeding the king) -----------------------------------------
// Any visible ally longer than us (ties to the lower id) is a king worth dying next to.
inline constexpr int FEED_MARGIN = 0;         // extra length (beyond a tie) a king needs over us
inline constexpr int FEED_DIST = 2;           // die this close (in steps) to the king's head
inline constexpr int FEED_KEEP_UNITS = 2;     // never feed if it would leave fewer dragons
inline constexpr bool DOOMED_BLIND_SPLIT = true;  // doomed with a partly known body: split anyway
inline constexpr bool FINAL_MERGE = true;     // from FINAL_MERGE_ROUND, feed down to one dragon
inline constexpr int FINAL_MERGE_ROUND = 450;
inline constexpr double FEED_ATTRACT = 400.0; // pull towards a king we could feed
// v8 ideas (docs/v8_ideas.md). The defaults below reproduce v4.4.
// 3. Keep the king unless another dragon is this much longer (0 = switch on any gain, ties to the lower id).
inline constexpr int KING_SWITCH_MARGIN = 3;
// 4. Feed only a king at least this long and this many times our length (before the final merge).
inline constexpr int FEED_MIN_KING_LEN = 8;
inline constexpr double FEED_KING_RATIO = 2.0;
// 5. Only dragons this close (in steps) are pulled toward the king; the rest keep foraging (0 = all).
inline constexpr int KING_PULL_RADIUS = 12;
// 6. Longer dragons don't feed (or head for the king) before FINAL_MERGE_ROUND: they forage.
inline constexpr int FEED_MAX_LEN = 4;

// --- Move scoring weights -------------------------------------------------------
inline constexpr double EAT_NOW = 400.0;         // stepping onto a pearl this turn
inline constexpr double FOOD_BEST = 300.0;       // best discounted pearl reachable
inline constexpr double FOOD_SUM = 3.0;          // sum of discounted pearl value (local richness tie-break)
inline constexpr double GAMMA_BEST = 0.85;       // per-step discount for the best target
inline constexpr double GAMMA_SUM = 0.8;         // per-step discount for the sum
inline constexpr double TRAPPED = 20000.0;       // cannot reach our own tail and little room
// Count as our room only tiles we reach no later than any other dragon's head.
inline constexpr bool VORONOI = true;
// Long dragons value room: per tile reachable, up to this many tiles per segment.
inline constexpr int KING_ROOM_LEN = 10;
inline constexpr double KING_ROOM = 6.0;
inline constexpr int KING_ROOM_TILES_PER_LEN = 3;
inline constexpr double UNKNOWN_PORTAL = 3000.0; // stepping into a portal whose far end we do not know
inline constexpr double OUT_OF_VIEW = 1500.0;    // landing on a tile we cannot see (through a portal)
// 7. Spread out before CONSOLIDATE_ROUND: a penalty for ending next to an ally head, and a reward
// per step from the nearest ally head in view (up to ALLY_SPREAD_CAP steps).
inline constexpr double ALLY_ADJ_PENALTY = 40.0;
inline constexpr double ALLY_SPREAD = 30.0;
inline constexpr int ALLY_SPREAD_CAP = 4;
// 11. Standing on a tile whose pearl is due while our body still covers it (the attempt is lost).
inline constexpr double SPAWN_BLOCK_COST = 150.0;
// 13. The king (10+ long) fears enemy reach this many times more.
inline constexpr double KING_THREAT_MULT = 2.0;
// 15. Tail strike: an enemy head next to our tail tip, at least this long, gets a 2-long child split off
// right there (it moves last this round and rams it). From CONSOLIDATE_ROUND a KING_ROOM_LEN+ dragon only
// does it for an enemy of TAIL_STRIKE_KING_MIN+.
inline constexpr bool TAIL_STRIKE = true;
inline constexpr int TAIL_STRIKE_MIN = 4;
inline constexpr int TAIL_STRIKE_KING_MIN = 8;
// 16. Pocket seal: per enemy segment, for a safe move that leaves an enemy head within SEAL_RADIUS
// (Manhattan) without enough room to live (it dies against us, or splits and loses 2).
inline constexpr double SEAL_BONUS = 80.0;
inline constexpr int SEAL_RADIUS = 5;
// 17. Steal their king's food: pearls within STEAL_RADIUS steps of an enemy head showing STEAL_MIN_LEN+
// segments are worth this much more (1.0 = double).
inline constexpr double STEAL_BONUS = 0.5;
inline constexpr int STEAL_MIN_LEN = 8;
inline constexpr int STEAL_RADIUS = 3;
// 18. Shed the head: when every move leaves a shorter enemy able to reach our head, a SHED_MIN_LEN+
// dragon splits off all but its front 2 (a ram then costs us 2, and the rest leaves from the tail).
inline constexpr bool SHED = true;
inline constexpr int SHED_MIN_LEN = 8;
inline constexpr double REVISIT = 40.0;          // per unit of recent-visit heat on the destination
inline constexpr double REVISIT_DECAY = 0.95;    // heat decay per round

// --- Diving into lanes and dead ends (where most pearls spawn on maze maps) ----------
inline constexpr bool DIVE = true;
inline constexpr bool DIVE_WALLS_ONLY = true;    // dive only into pockets made by kelp (v2d: anywhere)
inline constexpr double DIVE_MIN_FOOD = 1.0;     // expected pearls in the pocket before we bother
inline constexpr int DIVE_MAX_LEN = 5;           // longer dragons are worth too much to risk
inline constexpr int DIVE_ESCAPE_LEN = 4;        // this long after eating: we can split our way out
inline constexpr double DIVE_ESCAPE_COST = 400.0; // the 2-long parent we leave behind, roughly
inline constexpr int DIVE_MAX_DEATH_LEN = 2;     // dragons this small may die in there...
inline constexpr int DIVE_MIN_UNITS = 8;         // ...while we have at least this many
inline constexpr double DIVE_SEG_COST = 350.0;   // per segment of a dragon that dies in the pocket

// --- Sonar ------------------------------------------------------------------------
inline constexpr bool SONAR = true;
inline constexpr bool DEBUG_SONAR = false;    // log body handoffs (local debugging only)
inline constexpr int KING_STALE = 30;         // rounds a king record lives without a refresh
inline constexpr int KING_GROW_ROUND = 280;   // from here the king stops swarm-splitting (999 = never)
inline constexpr int KING_PULL_ROUND = 300;   // from here, head for the king we know of
inline constexpr double KING_APPROACH = 40.0; // per step closer to that king
inline constexpr int KING_FOOD_RADIUS = 3;         // pearls this close to the king are its own
inline constexpr double KING_FOOD_PENALTY = 300.0; // for a feeder that eats one anyway
// 12. Hunt the enemy king: relay the longest enemy head seen (ENEMY pings), and pull our small
// dragons within HUNT_KING_RADIUS steps toward it. tryStrike takes the trade once it is in reach.
inline constexpr bool HUNT_KING = true;
inline constexpr int HUNT_KING_ROUND = 250;     // from here
inline constexpr int HUNT_KING_MIN_LEN = 8;     // an enemy showing at least this many segments (view only bounds it)
inline constexpr int HUNT_KING_MAX_LEN = 4;     // hunters are at most this long
inline constexpr int HUNT_KING_MIN_UNITS = 4;   // and we have this many dragons
inline constexpr int HUNT_KING_RADIUS = 20;     // steps from its last known head
inline constexpr int HUNT_KING_STALE = 15;      // rounds a sighting counts
inline constexpr double HUNT_KING_ATTRACT = 80.0; // per step closer

// --- In-game feedback --------------------------------------------------------------
// Numbers war: ally vs enemy heads around us (view + sonar echoes), smoothed.
inline constexpr bool ADAPT_NUMBERS = true;
inline constexpr double ADAPT_DECAY = 0.9;        // per turn
inline constexpr double ADAPT_HI = 1.5;           // this many of ours per theirs: take even trades, chase
inline constexpr double ADAPT_LO = 0.67;          // this few: no even trades, ...
inline constexpr double ADAPT_THREAT_SCALE = 2.0; // ... and fear enemy reach twice as much
// Length race: from RACE_ROUND compare our king with the longest enemy seen (relayed in KING).
inline constexpr bool RACE_AWARE = true;
inline constexpr int RACE_ROUND = 300;
inline constexpr int RACE_MARGIN = 3;             // this many segments counts as ahead / behind
inline constexpr int ENEMY_MAX_STALE = 60;        // rounds an enemy length sighting counts
inline constexpr double RACE_KING_SAFE = 3.0;     // ahead: the king fears enemy reach 3x (and values room 2x)
inline constexpr double RACE_KING_BOLD = 0.5;     // behind: the king fears it half as much

// --- Pearl-belief model ----------------------------------------------------------
inline constexpr double ENEMY_CLOSER = 0.15;     // enemy head reaches the pearl first
inline constexpr double ALLY_CLOSER = 0.25;      // an ally reaches it first
inline constexpr double REMEMBERED_DECAY = 0.97; // per round a remembered pearl may have been eaten
inline constexpr double UNSEEN_PRIOR = 0.6;      // scale on "has probably spawned by now" for unseen tiles
inline constexpr int UNKNOWN_MAP_GAP = 150;      // assumed mean spawn gap when the map is unknown

} // namespace params