#pragma once

// fs series (fast swarm). Forked from the v4.x bot at v4.45; these defaults = fs1 (frozen in
// bots/fs1, see fs1_comparison.md and fs_series_design.md). Benchmarks compare copies that
// differ only here.
namespace params {

// Log every decision (local debugging only: output costs points in the judge).
inline constexpr bool DEBUG = false;
// v10 carries no map data (no maps_data.hpp): every map is played as an unseen one.
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
inline constexpr bool CHAIN_SPLIT = false;
// No swarm split while an enemy head can reach ours (the parent stands still for a turn).
inline constexpr bool THREAT_ON_OUR_HEADS = false;
// Delay a swarm split whose child could neither see its body nor be told it by sonar.
inline constexpr bool SPLIT_WAITS_FOR_HANDOFF = true;
// From this round on: no more swarm splits; small dragons feed pearls to the king (our queen,
// see QUEEN_KING), so we finish with one long queen.
inline constexpr int CONSOLIDATE_ROUND = 439;
// Feeding starts here: a dragon that reaches the king dies for it at once. v4.46 waited for
// CONSOLIDATE_ROUND while everyone gathered from KING_PULL_ROUND, boxing the king in.
inline constexpr int FEED_ROUND = 419;
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
inline constexpr int TRADE_MIN_UNITS = 7;
// Longest sprint we plan in order to reach an enemy head (see NEW_SPRINT for how far one can go).
inline constexpr int STRIKE_MAX_STEPS = 6;
// Standing where an enemy head can reach us next turn: per segment we would lose more than it.
inline constexpr double THREAT_PER_LEN = 45.878519;
// ... and a small penalty when the trade it could take would be even.
inline constexpr double THREAT_EVEN = 9.642579;
// Hunting: pull towards enemy heads worth trading with, per segment gained (capped).
inline constexpr double HUNT_ATTRACT = 107.989314;
inline constexpr double HUNT_GAIN_CAP = 10.0;
inline constexpr int HUNT_MEMORY = 8; // rounds a sighting stays worth chasing
// Aggression: with at least AGGRO_RATIO times as many of our heads in view as theirs (and a few
// dragons to spare), take even trades and chase harder.
inline constexpr double AGGRO_RATIO = 1.487199;
inline constexpr int AGGRO_MIN_UNITS = 3;
inline constexpr double HUNT_ATTRACT_AGGRO = 200.0;

// --- Consolidation (feeding the king) -----------------------------------------
// Any visible ally longer than us (ties to the lower id) is a king worth dying next to.
// v19: 4 (was 0). With 0, from round 440 every dragon fed any ally one longer, so 2s fed 3s all over the
// map: v15 lost 14 length on the big maps between rounds 440 and 460 while its longest grew 5. With 4,
// 80-72 over 152 games on the ladder maps (big maps 28-20) and +12 to +16 final total length.
inline constexpr int FEED_MARGIN = 3;         // extra length (beyond a tie) a king needs over us
inline constexpr int FEED_DIST = 2;           // die this close (in steps) to the king's head
inline constexpr int FEED_KEEP_UNITS = 2;     // never feed if it would leave fewer dragons
inline constexpr int KEEP_WHOLE_MIN_SPACE = 999; // endgame king: no escape split while it has this much room (999 = off)
inline constexpr bool DOOMED_BLIND_SPLIT = true;  // doomed with a partly known body: split anyway
inline constexpr bool FINAL_MERGE = true;     // from FINAL_MERGE_ROUND, feed down to one dragon
inline constexpr int FINAL_MERGE_ROUND = 449;
inline constexpr double FEED_ATTRACT = 400.0; // pull towards a king we could feed

// --- Move scoring weights -------------------------------------------------------
inline constexpr double EAT_NOW = 379.091653;         // stepping onto a pearl this turn
inline constexpr double FOOD_BEST = 393.845035;       // best discounted pearl reachable
inline constexpr double FOOD_SUM = 3.0;          // sum of discounted pearl value (local richness tie-break)
inline constexpr double GAMMA_BEST = 0.862613;       // per-step discount for the best target
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
// ... for a dragon of DIVE_MAX_LEN or less, whose far end we know: Portals hides every food box
// behind a portal, and the 2000-rated team out-ate us 106 to 12 by round 50 there. 1500 = off.
inline constexpr double OUT_OF_VIEW_SMALL = 150.0;
inline constexpr double REVISIT = 57.802755;          // per unit of recent-visit heat on the destination
inline constexpr double REVISIT_DECAY = 0.95;    // heat decay per round

// --- Diving into lanes and dead ends (where most pearls spawn on maze maps) ----------
inline constexpr bool DIVE = true;
inline constexpr bool DIVE_WALLS_ONLY = true;    // dive only into pockets made by kelp (v2d: anywhere)
inline constexpr double DIVE_MIN_FOOD = 1.0;     // expected pearls in the pocket before we bother
inline constexpr int DIVE_MAX_LEN = 5;           // longer dragons are worth too much to risk
inline constexpr int DIVE_ESCAPE_LEN = 4;        // this long after eating: we can split our way out
inline constexpr double DIVE_ESCAPE_COST = 400.0; // the 2-long parent we leave behind, roughly
// ... less this per expected pearl in the pocket beyond the first: rich lanes (Portals' middle
// lane respawns every round) are worth the parent. 0 = off.
inline constexpr double DIVE_RICH_DISCOUNT = 200.0;
inline constexpr int DIVE_MAX_DEATH_LEN = 3;     // dragons this small may die in there...
inline constexpr int DIVE_MIN_UNITS = 8;         // ...while we have at least this many
inline constexpr double DIVE_SEG_COST = 300.809689;   // per segment of a dragon that dies in the pocket

// --- Sonar ------------------------------------------------------------------------
inline constexpr bool SONAR = true;
// Knowledge sharing on maps we don't know (a split child is a new process that knows nothing):
// FOOD pings carry the map's symmetry and rich pearl tiles (largest countdown seen <= RICH_CD),
// sent on half the free directions before KING_PULL_ROUND, relayed by whoever hears them.
inline constexpr bool SHARE_FOOD = true;
inline constexpr int RICH_CD = 36;            // a tile never seen with a longer countdown is "rich"
inline constexpr int FOOD_STALE = 120;        // rounds a relayed spot is kept without a refresh
// --- fs1: portals and aggressive splitting (fs_series_design.md) ---------------------------
// Portal knowledge: every dragon keeps the portal pairs it knows (seen, travelled or heard) and
// broadcasts them (PORTAL pings, a third of the free directions), so split children learn them.
// With the symmetry known, a known pair also gives its mirror pair.
inline constexpr bool SHARE_PORTALS = true;
// Small dragons (this long or less) may go through a portal whose far end nobody knows yet;
// they learn it on the way. 0 = never (v4.x: an unknown portal is a -3000 trap).
inline constexpr int PORTAL_EXPLORE_MAX_LEN = 3;
inline constexpr int PORTAL_EXPLORE_MIN_UNITS = 3;
inline constexpr double PORTAL_EXPLORE_SCORE = 150.0;
// Portal scouts: this share of small dragons (picked by a hash of the dragon's id, so every process
// knows its role without talking) seeks out portals whose far side it hasn't seen lately, goes
// through without the out-of-view penalty, and (via the PORTAL and FOOD pings) tells the team
// what is there. Not map-specific: works on unseen maps. 0 = off.
inline constexpr double SCOUT_FRACTION = 0.0;
inline constexpr int SCOUT_MAX_LEN = 3;
inline constexpr int SCOUT_UNTIL = 300;
inline constexpr double SCOUT_PULL = 60.0;          // per step closer to a portal worth scouting
inline constexpr double SCOUT_CROSS = 300.0;        // for going through one (known or unknown exit)
inline constexpr int SCOUT_STALE = 60;              // a far side seen longer ago than this is worth a look
// Out-of-view penalty for going through a portal whose exit we know (seen, travelled or heard):
// PORTAL_KNOWN_OOV for small dragons (<= DIVE_MAX_LEN), PORTAL_KNOWN_OOV_BIG for longer ones.
// -1 = off (the plain OUT_OF_VIEW_SMALL / OUT_OF_VIEW penalties apply).
inline constexpr double PORTAL_KNOWN_OOV = -1.0;
inline constexpr double PORTAL_KNOWN_OOV_BIG = -1.0;
// Exploration: until EXPLORE_UNTIL, a move earns EXPLORE_W per never-seen tile it brings into
// view (dragons of EXPLORE_BIG_LEN+ get 30% of it), so the swarm fans out into unknown ground
// instead of circling what it can see. 0 = off.
inline constexpr double EXPLORE_W = 15.0;
inline constexpr int EXPLORE_UNTIL = 300;
inline constexpr int EXPLORE_BIG_LEN = 10;
// Scale exploration by map size: none below EXPLORE_MIN_AREA tiles (on small maps it only walks the
// swarm into the enemy: fs1-e was eliminated by round 30-50 on Arena), full from EXPLORE_FULL_AREA.
inline constexpr int EXPLORE_MIN_AREA = 900;
inline constexpr int EXPLORE_FULL_AREA = 2000;
// Long-range exploration (v11): until EXPLORE_UNTIL, on maps of EXPLORE_MIN_AREA+ tiles, a dragon of
// EXPLORE_FAR_MAX_LEN or less heads for the nearest never-seen tile it reaches before any ally,
// EXPLORE_FAR_W per step closer. 0 = off.
inline constexpr double EXPLORE_FAR_W = 0.0;
inline constexpr int EXPLORE_FAR_MAX_LEN = 6;
// A tile only counts as new if its mirror is unseen too (the map is symmetric, so the enemy half
// teaches nothing our half hasn't), and never where an enemy head can reach next turn.
inline constexpr bool EXPLORE_SMART = true;
// Our queen explores too (v10). Off: she gets no exploration bonus and stays with what she sees.
inline constexpr bool QUEEN_EXPLORE = true;
// Density-gated even trades: take an even head trade (outside aggressive mode) only where our other
// heads near the target outnumber theirs, i.e. where we will eat the pearls the two dead dragons
// drop. On the ladder fast swarms ate 60% of those drops (+86 vs our 58 pearls a game). Off: v4.x
// (any even trade while we have TRADE_MIN_UNITS dragons).
inline constexpr bool TRADE_DENSITY = true;
// Density-aware defence (the mirror of TRADE_DENSITY): a tile that an enemy head of our length or
// shorter can reach next turn costs this much when their heads near it (TRADE_DENSITY_RADIUS)
// outnumber or equal ours, since they would eat the drops. v4.x charged only THREAT_EVEN (15) for
// an even trade, and trade-happy swarms farmed us (Quaker, Dilemma: 11 of our 16 deaths were their
// strikes, and they ate 76 of the drops to our 9). 0 = off.
inline constexpr double THREAT_DENSITY_PENALTY = 148.609191;
inline constexpr int TRADE_DENSITY_RADIUS = 4;
// A swarm split waits while an enemy head could reach the splitting dragon (v4.x). Off: 2-long
// halves are cheap, split anyway.
inline constexpr bool SPLIT_THREAT_VETO = true;
// Work out the map's symmetry from what we see (mirrored tiles share a pearl countdown), then
// mirror every sighting of pearls and walls.
inline constexpr bool INFER_SYMMETRY = true;
inline constexpr int SYM_MIN_AGREE = 4;       // matching countdown pairs, with no contradiction
inline constexpr bool DEBUG_SONAR = false;
// Log, for every turn a dragon could swarm-split (length >= SWARM_SPLIT_LEN, before
// CONSOLIDATE_ROUND), whether it split or which rule stopped it (local debugging only).
inline constexpr bool DEBUG_SPLITS = false;    // log body handoffs (local debugging only)
inline constexpr int KING_STALE = 30;         // rounds a king record lives without a refresh
// Don't follow a king record shorter than this (0 = off): a king that split itself down keeps
// sending fresh records, and the dragons that never hear of the new longest follow a 2-long "king".
inline constexpr int KING_MIN_FOLLOW_LEN = 0;
inline constexpr int KING_GROW_ROUND = 999;   // from here the king stops swarm-splitting (999 = never)
// Local kings: from LOCAL_KING_ROUND a dragon of LOCAL_KING_MIN_LEN+ that sees no longer ally stops
// swarm-splitting and grows (no sonar needed; each part of the map grows its own king, merged in the
// endgame). The teams that beat us in king races have an 8+ dragon by round ~158; ours reached 8
// only at ~448 because every 4+ dragon kept splitting until the switch. 999 = off.
inline constexpr int LOCAL_KING_ROUND = 999;
inline constexpr int LOCAL_KING_MIN_LEN = 6;
// Split headroom: swarm splits stop SPLIT_HEADROOM dragons short of the cap. At the cap a boxed-in
// dragon can't escape-split and dies whole: 8+ dragons did so ~29 times a game (avg length 11.5),
// 97% of them at the cap. 0 = off (fill the cap).
inline constexpr int SPLIT_HEADROOM = 0;
// Pack hunt: the longest enemy dragon seen (PACK_MIN_PREY+ long) is shared over sonar (PREY record,
// from PACK_ROUND - PACK_SHARE_LEAD). From PACK_ROUND, PACK_SHARE_PCT% of our dragons of PACK_MAX_LEN
// or less (while we have PACK_MIN_UNITS+) head for it instead of feeding, and trade heads with it:
// a length-2 dragon for its whole length. In round-500 losses one of ours was within 3 of the
// enemy's longest dragon (median 24 long, 1 escort) in only 14% of checks from r400. 999 = off.
inline constexpr int PACK_ROUND = 999;
inline constexpr int PACK_SHARE_LEAD = 30;
inline constexpr int PACK_MIN_PREY = 8; // segments seen at once (a 7x7 view shows only part of a long dragon)
inline constexpr int PACK_MAX_LEN = 4;
inline constexpr int PACK_MIN_UNITS = 16;
inline constexpr int PACK_SHARE_PCT = 50;
inline constexpr int PREY_STALE = 12;
inline constexpr double PACK_ATTRACT = 300.0;
inline constexpr double PACK_APPROACH = 40.0;
// Anchored king (what Stockfish did on Slithery Fight: a king of 10 from r150 that never split again,
// fed by small dragons dying next to it at the dragon cap from r250, 78 long at the end). On maps of
// ANCHOR_MIN_AREA+ tiles (under that, games end in eliminations), from ANCHOR_ROUND_MID (or
// ANCHOR_ROUND_BIG on ANCHOR_BIG_AREA+) the early-king machinery runs, with:
//  - one king by length: only a dragon of ANCHOR_KING_MIN_LEN+ can be king (food-poor maps get none);
//    a king's record counts for ANCHOR_STALE rounds; a challenger needs ANCHOR_REPLACE_MARGIN more
//    length; a dragon that sees the tile a fresh record put the king on, and no king there, marks it
//    dead for ANCHOR_DEAD_MEMORY rounds (debug logs: 39% of checks followed a dead king, 6-16 self-kings);
//  - a slot for the king: other dragons don't split into the last ANCHOR_HEADROOM slots (boxed-in
//    deaths at the cap are the king's first killer: ~2% a round even with no enemy near);
//  - a leash: the king pays ANCHOR_LEASH per tile beyond ANCHOR_RADIUS from where it became king,
//    so feeders know where to find it;
//  - feeding at the cap: at the dragon cap, small dragons (ANCHOR_FEED_MAX_LEN or less) within
//    ANCHOR_PULL_DIST steps that see no enemy head head for the king and die next to it.
inline constexpr bool ANCHOR_KING = true;
inline constexpr int ANCHOR_MIN_AREA = 1500;
inline constexpr int ANCHOR_BIG_AREA = 2000;
inline constexpr int ANCHOR_ROUND_BIG = 150;
inline constexpr int ANCHOR_ROUND_MID = 200;
inline constexpr int ANCHOR_KING_MIN_LEN = 8;
inline constexpr int ANCHOR_STALE = 12;
inline constexpr int ANCHOR_REPLACE_MARGIN = 3;
inline constexpr int ANCHOR_DEAD_MEMORY = 30;
inline constexpr bool DEBUG_ANCHOR = false;    // log king beliefs every 25 rounds (local debugging only)
inline constexpr int ANCHOR_RADIUS = 6;
inline constexpr double ANCHOR_RADIUS_PER_LEN = 0.5; // the leash grows with the king (a 46-long king leashed to 6 tiles coiled into itself)
// Swarm splits stop this many short of the cap in the anchor phase, so the king can always escape-split.
inline constexpr int ANCHOR_HEADROOM = 1;
inline constexpr double ANCHOR_LEASH = 60.0;
inline constexpr int ANCHOR_PULL_DIST = 15;
inline constexpr int ANCHOR_FEED_MAX_LEN = 3;
// Crowd control: other dragons keep ANCHOR_CROWD_RADIUS+1 steps off the king's head (penalty per step
// inside), except one feeder at a time going in. Our 10+ dragons die boxed in 16-18 times a game, 98%
// at the cap, next to ally bodies 58% of the time. 0 = off.
inline constexpr int ANCHOR_CROWD_RADIUS = 3;
inline constexpr double ANCHOR_CROWD_PENALTY = 80.0;
// Zone round the anchored king. From the zone study (446 games): head-on risk to the king rises inside
// 8 steps (2% per 10 rounds beyond 11, 9.5% at 7-8, 22% at 0-4); the killer is within 8 only ~4 rounds
// before the kill while sonar news is 2-4 rounds old at 6-15 steps, so the radar is for moving away
// early; local numbers decide fights (with 2x our heads we eat 73-78% of the pearls there and each of
// our heads is half as likely to die); a king on the enemy half dies head-on 2.5x as often.
//  - radar: dragons within ZONE_COMM_R of the king report the enemy heads they see (THREAT record,
//    kept THREAT_STALE rounds); the king pays RADAR_W per enemy per step inside RADAR_R of a group
//    (twice in FLEE), so it moves away early (and so toward our side);
//  - mode (set by the king, sent in its record): FEED with no enemy within ZONE_R; FIGHT when our heads
//    there are FIGHT_RATIO x theirs; FLEE under FLEE_RATIO x; else HOLD. Feeding only in FEED; in
//    HOLD/FLEE dragons within ZONE_COMM_R escort the king ESCORT_DIST steps off (the king is the
//    moving rally point); in FIGHT those that see enemies near the king fight (even trades, hunting).
// Home: the anchored king's anchor is where our team started (HOME sonar record, relayed on one slot in
// HOME_SLOT_EVERY), not where it happened to be elected. A king on the enemy half is killed head-on
// 2.5x as often (10% vs 4% per 10 rounds) and outnumbered 5x as often; Stockfish's king stayed on its
// side 94% of the time, ours 75%.
inline constexpr bool ANCHOR_HOME = true;
inline constexpr int HOME_SLOT_EVERY = 4;
// Bodyguards. King death stories: small enemies stalk the king at distance 2 for 4-5 rounds, then
// strike; no ally of ours was within 3 of it (Stockfish's king had a median of 1 escort within 4).
// In the anchor phase GUARD_PCT% of our small dragons (GUARD_MAX_LEN or less) within GUARD_RECRUIT_R
// of the king keep GUARD_DIST steps off its head (no feeding); with INTERCEPT on they trade heads with
// any enemy head that comes within INTERCEPT_RADIUS of it. 0 = off.
inline constexpr int GUARD_PCT = 0;
inline constexpr int GUARD_MAX_LEN = 4;
inline constexpr int GUARD_RECRUIT_R = 12;
inline constexpr int GUARD_DIST = 3;
inline constexpr double GUARD_W = 40.0;
inline constexpr bool ZONE = false;
inline constexpr int ZONE_R = 8;
inline constexpr int ZONE_COMM_R = 12;
inline constexpr int THREAT_STALE = 4;
inline constexpr int RADAR_R = 12;
inline constexpr double RADAR_W = 8.0;
inline constexpr double FIGHT_RATIO = 1.5;
inline constexpr double FLEE_RATIO = 0.8;
inline constexpr int ESCORT_DIST = 5;
inline constexpr double ESCORT_W = 30.0;
inline constexpr bool DEBUG_PACK = false; // log the hunt state every 10 rounds (local debugging only)
inline constexpr int KING_PULL_ROUND = 419;   // from here, head for the king we know of
inline constexpr double KING_APPROACH = 19.380778; // per step closer to that king
// Merging: dragons of MERGE_MIN_LEN+ that are not the king are pulled this many times harder,
// so a second king travels to the first and feeds it (at round 500 a second king of about 20
// was usually left over on big maps). 1 = off.
inline constexpr int MERGE_MIN_LEN = 8;
inline constexpr double MERGE_APPROACH_MULT = 1.0;
// Feeding ring: feeders keep this many steps from the king's head unless it's their turn
// (one at a time; the king flags "crowded" over sonar). 0 = off.
inline constexpr int FEED_RING = 0;
inline constexpr double BIG_THREAT_SCALE = 1.0;  // endgame: threat penalty multiplier for dragons of KING_ROOM_LEN+
inline constexpr bool BIG_ASSUME_LONG = false;   // endgame big dragons: enemies with unseen tails may sprint the max
inline constexpr int RING_FRESH = 5;              // rounds the king's "crowded" flag counts
inline constexpr double RING_ENTER_PENALTY = 400.0;
// Early king (the ~2000-rated teams grow one from round ~150; we sat at length 3-4 until 380 and
// the late feeding wasted most of our length): on big maps, from EARLY_KING_ROUND the longest
// dragon (agreed over sonar) stops swarm-splitting, is protected like the endgame king, and
// others leave the pearls around it alone. Small maps keep the pure swarm.
inline constexpr bool EARLY_KING = false;
inline constexpr int EARLY_KING_ROUND = 150;
inline constexpr int EARLY_KING_MIN_AREA = 900;   // width x height
inline constexpr bool EARLY_KING_FEED_AT_CAP = false; // at the dragon cap, small dragons next to it feed it
// Midgame feeding (the 2000-rated team's king ate mostly its own small dragons from round ~250):
// from EARLY_FEED_ROUND small dragons next to the early king die for it while we keep enough dragons,
// and with EARLY_PULL_MIN_UNITS+ dragons small ones head for it. 999 = off.
inline constexpr int EARLY_FEED_ROUND = 999;
inline constexpr int EARLY_FEED_MAX_LEN = 3;
inline constexpr int EARLY_FEED_MIN_UNITS = 12;
inline constexpr int EARLY_PULL_MIN_UNITS = 999;
// Big-dragon guard: dragons of 10+ lost ~140 length a game to head-on kills on the enemy's turn,
// mostly from round 150 and mostly with no safe move left by then. Keep a margin: a penalty per
// segment we are longer than any enemy whose reach (its sprint, 6 if its tail is unseen) plus
// BIG_GUARD_MARGIN covers the tile. 0 = off.
inline constexpr double BIG_GUARD_PER_LEN = 0.0;
inline constexpr int BIG_GUARD_MARGIN = 2;
inline constexpr int BIG_GUARD_ROUND = 150;
// King protection (king_protection_design.md). A: interceptors. Small dragons treat an enemy
// head near a big ally (INTERCEPT_MIN_KING+, longer than that enemy) as worth a trade, even an
// even or slightly losing one, and hunt it.
inline constexpr bool INTERCEPT = false;
inline constexpr int INTERCEPT_RADIUS = 5;     // enemy head this close (steps) to the big ally's head
inline constexpr int INTERCEPT_MIN_KING = 10;
inline constexpr int INTERCEPT_MAX_LEN = 5;    // only dragons this small intercept
inline constexpr int INTERCEPT_MIN_UNITS = 4;
inline constexpr double INTERCEPT_GAIN = 1.5;  // counts like a trade gaining 1.5 segments
// C: keep-away. A big dragon with an enemy head near keeps its distance, graded by distance,
// and backs towards its own dragons (its interceptors).
inline constexpr bool KEEPAWAY = false;
inline constexpr int KEEPAWAY_ROUND = 150;
inline constexpr int KEEPAWAY_DIST = 4;        // penalty for ending within this many steps of an enemy head
inline constexpr double KEEPAWAY_W = 60.0;     // per step inside that distance
inline constexpr double KEEPAWAY_ALLY = 30.0;  // per step closer than 6 to an ally head
inline constexpr int KING_FOOD_RADIUS = 3;         // pearls this close to the king are its own
inline constexpr double KING_FOOD_PENALTY = 300.0; // for a feeder that eats one anyway

// --- In-game feedback --------------------------------------------------------------
// Numbers war: ally vs enemy heads around us (view + sonar echoes), smoothed.
inline constexpr bool ADAPT_NUMBERS = false;
inline constexpr double ADAPT_DECAY = 0.9;        // per turn
inline constexpr double ADAPT_HI = 1.5;           // this many of ours per theirs: take even trades, chase
inline constexpr double ADAPT_LO = 0.67;          // this few: no even trades, ...
inline constexpr double ADAPT_THREAT_SCALE = 2.0; // ... and fear enemy reach twice as much
// Length race: from RACE_ROUND compare our king with the longest enemy seen (relayed in KING).
inline constexpr bool RACE_AWARE = false;
inline constexpr int RACE_ROUND = 340;
inline constexpr int RACE_MARGIN = 3;             // this many segments counts as ahead / behind
inline constexpr int ENEMY_MAX_STALE = 60;        // rounds an enemy length sighting counts
inline constexpr double RACE_KING_SAFE = 3.0;     // ahead: the king fears enemy reach 3x (and values room 2x)
inline constexpr double RACE_KING_BOLD = 0.5;     // behind: the king fears it half as much

// --- Ideas from the top teams' reports (each off by default) --------------------------
inline constexpr bool LOCAL_RECYCLE = false;     // after the switch, small dragons die in front of long allies
inline constexpr int RECYCLE_MIN_GAIN = 5;       // the ally is at least this much longer
inline constexpr double RECYCLE_PROB = 0.5;      // per turn when next to one
inline constexpr double SPREAD_WEIGHT = 0.0;     // swarm phase: per tile of distance from the nearest ally head (max 4)
inline constexpr double SPREAD_ADJ = 80.0;       // ... and a penalty for ending next to one
inline constexpr double SPAWN_CLEAR = 0.0;       // penalty for standing on a tile whose pearl is due within 2 rounds
inline constexpr bool KAMIKAZE = false;          // 2-3 long dragons ram any enemy head at least as long
inline constexpr int KAMIKAZE_MIN_UNITS = 4;
inline constexpr bool SPLIT_RELAXED = true;      // swarm split if one half is safe and the child has an exit

// --- Queens (1 Oct 2026 "Slay Queen" rules) ----------------------------------------
// Dragons 0 and 1 are the queens. At round 500 the longer queen wins (a dead queen is 0 long),
// then the longest dragon, then total length. Every dragon gets ceil(L/4) free steps a turn.
// Our queen is the king: the one everyone feeds, and it never feeds anyone. Off: the v4.46
// king (the longest dragon we know of).
inline constexpr bool QUEEN_KING = true;
// The queen swarm-splits like the others only before this round (0: never). 60 beat 0 by 28-14
// head to head (2 Oct): twice the swarm early, and she is still short when it gets dangerous.
inline constexpr int QUEEN_SPLIT_UNTIL = 55;
// ... and only while she is this short: a queen that starts long (25 on the new Slithery Fight)
// keeps her length.
inline constexpr int QUEEN_SPLIT_MAX_LEN = 8;
// Queen first at food: the others leave pearls within this many steps of our queen when they
// see her (all game), and she ignores which ally would reach a pearl first. -1 = off.
inline constexpr int QUEEN_FOOD_RADIUS = 3;
// After a split the child moves later in the round, so its tail frees before the parent's next
// move. v4.46 counted one move later, so a split keeping a long queen looked fatal: on the new
// Slithery Fight our coiled 25-long queen kept 2 and gave away 23.
inline constexpr bool SPLIT_CHILD_EARLY = true;
// Budding: before QUEEN_BUD_UNTIL, a queen of QUEEN_BUD_LEN+ splits off a child of
// QUEEN_BUD_CHILD (which then swarm-splits), keeping the rest. 0 = off.
inline constexpr int QUEEN_BUD_LEN = 0;
inline constexpr int QUEEN_BUD_CHILD = 4;
inline constexpr int QUEEN_BUD_UNTIL = 300;
// The queen takes no head trades and no dives into dead ends...
inline constexpr bool QUEEN_SAFE = true;
// ... and stays off every tile an enemy head can reach before her next turn (any length:
// even a length-2 enemy trading heads costs us the game), assuming unseen tails sprint the max.
// More than TRAPPED: being boxed in may still end in an escape, a head trade never does.
// Scaled by the chance an enemy reaches the tile: 1 within the reach of the length we see;
// an enemy whose tail is out of view may be longer, QUEEN_LONGER_P per extra segment it needs.
inline constexpr double QUEEN_THREAT = 25000.0;
inline constexpr double QUEEN_LONGER_P = 0.482292;
// Enemies seen in the last QUEEN_STALE_AGE rounds, out of view now: from where we saw them,
// a step closer per round since, at QUEEN_STALE_P.
inline constexpr int QUEEN_STALE_AGE = 3;
inline constexpr double QUEEN_STALE_P = 0.5;
// Room to dodge: per tile short of QUEEN_FREEDOM_CAP within 3 steps of where she ends
// (a 1-wide corridor has about 6, the open about 20).
inline constexpr double QUEEN_FREEDOM = 60.0;
inline constexpr int QUEEN_FREEDOM_CAP = 16;
// Two-round look-ahead: next round each enemy may step its free steps towards her and then
// strike. Where she ends, count the tiles her next free steps reach that stay out of every
// such reach; short of QUEEN_ESCAPES, pay QUEEN_CORNERED in proportion. 0 = off.
inline constexpr double QUEEN_CORNERED = 0.0;
inline constexpr int QUEEN_ESCAPES = 3;
// ... and does not end a move near the edge of her view, where an enemy she cannot see may
// sprint in: per step closer than 3 to a tile out of view (a single step never is).
inline constexpr double QUEEN_UNSEEN = 1000.0;
// ... and pays this for a move through any portal, known or not, including one step of a
// multi-step path (v10). A huge value means only when every other move is fatal. 0 = off.
inline constexpr double QUEEN_PORTAL_PENALTY = 0.0;
// ... and keeps her distance: per step inside QUEEN_KEEPAWAY_DIST of an enemy head.
inline constexpr double QUEEN_KEEPAWAY = 87.326366;
inline constexpr int QUEEN_KEEPAWAY_DIST = 4;
// A trapped queen judges escape splits by her own surviving length (she keeps the head), plus
// this much per segment of a child that survives (it can feed her later, at half value).
inline constexpr double QUEEN_CHILD_WEIGHT = 0.25;
// Hunting: any dragon but our queen trades heads with the enemy queen, whatever its length.
inline constexpr bool HUNT_QUEEN = true;
inline constexpr double QUEEN_KILL_GAIN = 10.0;  // counts like a trade gaining this many segments
inline constexpr int QUEEN_STRIKE_MAX = 8;       // longest sprint we plan at the enemy queen's head
inline constexpr int QUEEN_HUNT_MEMORY = 20;     // rounds a sighting of her stays worth chasing
// Her sightings travel over sonar (ENEMY_QUEEN) for QUEEN_SHARE_AGE rounds (0 = off), and hunters (dragons
// of QUEEN_HUNTER_MAX_LEN or less, every QUEEN_HUNT_SHARE-th id, while we have QUEEN_HUNT_MIN_UNITS)
// score QUEEN_HUNT_APPROACH per step closer to where she was seen, until FEED_ROUND. 0 = off.
inline constexpr int QUEEN_SHARE_AGE = 0;
inline constexpr double QUEEN_HUNT_APPROACH = 0.0;
inline constexpr int QUEEN_HUNTER_MAX_LEN = 6;
inline constexpr int QUEEN_HUNT_SHARE = 1;
inline constexpr int QUEEN_HUNT_MIN_UNITS = 4;
// Guards: a small dragon (QUEEN_GUARD_MAX_LEN or less, while we have QUEEN_GUARD_MIN_UNITS)
// trades heads with any enemy head within QUEEN_GUARD_RADIUS steps of our queen, counted like a
// trade gaining QUEEN_GUARD_GAIN segments.
inline constexpr bool QUEEN_GUARD = false;
inline constexpr int QUEEN_GUARD_RADIUS = 4;
inline constexpr int QUEEN_GUARD_MAX_LEN = 6;
inline constexpr int QUEEN_GUARD_MIN_UNITS = 3;
inline constexpr double QUEEN_GUARD_GAIN = 3.0;
// Sprint reach under the new rules: ceil(L/4) free steps, then one segment per step down to
// length 2, so ceil(L/4) + L - 2 steps (v4.46 assumed L - 1).
inline constexpr bool NEW_SPRINT = true;
// Enemy reach passes through our own body (it moves away, and we often move onto it). v4.46
// treated it as a wall, so the tile we moved to could look safe with an enemy next to it.
inline constexpr bool OWN_BODY_OPEN = true;
// Use the free steps: a dragon of 5+ (2+ free steps) also weighs every path of up to
// MULTI_MAX_STEPS steps through tiles in view. A long queen outruns small hunters this way.
inline constexpr bool MULTI_STEP = true;
inline constexpr int MULTI_MAX_STEPS = 3;
inline constexpr bool MULTI_QUEEN_ONLY = false;
// CPU (v10): when scoring every path would search more than this many tiles in total (a 9+
// dragon on a big open map: 48 paths x ~4000 tiles, up to 97M points of the 100M limit), score
// every 2-step path and extend only the best to 3 steps until this many tiles are searched.
// Below it nothing changes. 0 = off.
inline constexpr long MULTI_VISIT_BUDGET = 80000;
// CPU (v11, from v9.1): every move's search stops this many steps out (or the dragon's length
// plus 8, so it still finds its tail). v9.1 peaked at 31M points a turn with it. 0 = off: at 30
// it went 32-36 against v10 on the ladder maps and only cut v10's peak from 60.8M to 55.8M.
inline constexpr int EVAL_MAX_DEPTH = 0;
// A dragon with room for the moves left in the game (plus HORIZON_MARGIN) is safe, however long
// it is: no escape splits in the last rounds for room it will never need.
inline constexpr bool ENDGAME_HORIZON = true;
inline constexpr int HORIZON_MARGIN = 2;
// Our queen escape-splits only when no move leaves her QUEEN_KEEP_MIN_SPACE tiles of room: on the
// ladder she split herself down to 2 from 13-38 while she could still move, and died.
inline constexpr bool QUEEN_KEEP_LENGTH = true;
inline constexpr int QUEEN_KEEP_MIN_SPACE = 3;
// From FEED_ROUND, a dragon with no known way to our queen (sealed in kelp, as on Schooltime) feeds
// the biggest ally it sees instead, so a long dragon decides a tie on queens.
inline constexpr bool QUEEN_SEALED_FALLBACK = true;

// --- v19: tactics of the top teams (docs/top_teams_tactics.md) ------------------------------
// Every switch below off = v15 as submitted (bots/v11 at 5015023).
// 1. Trade down (from v16): a dragon of TRADE_DOWN_MAX_LEN or less trades heads with an enemy up
// to TRADE_DOWN_MAX shorter, while we have TRADE_MIN_UNITS dragons and our other heads near the
// target outnumber theirs by TRADE_DOWN_MARGIN or more (we eat the drops). Counted like a gain of
// TRADE_DOWN_GAIN. 0 = off.
inline constexpr int TRADE_DOWN_MAX = 1; // v19: on. 81-71 over 152 games against v15 (small maps 41-31)
inline constexpr int TRADE_DOWN_MAX_LEN = 3;
inline constexpr int TRADE_DOWN_MARGIN = 1;
inline constexpr double TRADE_DOWN_GAIN = 0.3;

// 2. Local kings (tactic 1): on big maps the top teams grow 3-5 dragons of 8+ from round 200-250, fed
// by small allies that die next to them (their longest was the queen only 20-30% of the time).
// From LK_ROUND, on maps of LK_MIN_AREA+ tiles, a dragon whose id hashes under LK_PCT (never the
// queen) stops swarm-splitting at LK_MIN_LEN+ and grows. A small dragon (LK_FEED_MAX_LEN or less,
// not a candidate) within LK_FEED_DIST steps of a candidate of LK_FEED_KING_LEN+ (and at least
// RECYCLE_MIN_GAIN longer) dies for it with chance LK_FEED_PROB a turn, while we have
// LK_FEED_MIN_UNITS dragons and no enemy head is in view. Others leave pearls within LK_FOOD_RADIUS
// of such a king to it (KING_FOOD_PENALTY; -1 = off). A candidate of LK_KEEP_LEN+ feeds nobody and
// follows no king: it is kept for the longest-dragon tiebreak. 999 = off.
inline constexpr int LK_ROUND = 999;
inline constexpr int LK_MIN_AREA = 1500;
inline constexpr int LK_PCT = 12;
inline constexpr int LK_MIN_LEN = 4;
inline constexpr int LK_FEED_MAX_LEN = 3;
inline constexpr int LK_FEED_DIST = 2;
inline constexpr int LK_FEED_KING_LEN = 6;
inline constexpr double LK_FEED_PROB = 0.3;
inline constexpr int LK_FEED_MIN_UNITS = 16;
inline constexpr int LK_FOOD_RADIUS = 2;
inline constexpr int LK_KEEP_LEN = 12;
// Local kings v2: any dragon but the queen can be a king (no draw by id): one that reaches LK_MIN_LEN
// stops splitting, and small dragons feed any ally of LK_FEED_KING_LEN+. (v1 drew LK_PCT% by id and
// let them grow from 4: on the big maps they stayed short, 37-39 against v15.)
inline constexpr bool LK_ANY_KING = false;

// 3. Early queen feeding (tactic 2): where queens survive (Portals, Trauma) the top teams' queens are
// fed from round ~200 (15-20 long by round 300; ours ~5). From QFEED_ROUND until FEED_ROUND, small
// dragons (QFEED_MAX_LEN or less) whose id hashes under QFEED_PULL_PCT head for our queen (the king
// pull) and die next to her when no enemy head is in view, while we have QFEED_MIN_UNITS dragons.
// 999 = off.
inline constexpr int QFEED_ROUND = 254;
inline constexpr int QFEED_MAX_LEN = 3;
inline constexpr int QFEED_PULL_PCT = 13;
inline constexpr int QFEED_MIN_UNITS = 19;

// 4. Local queen hunts (tactic 4): the top teams' queen killers close in from ~12 steps, 2-3 at a time,
// mostly before round 250. With QUEEN_SHARE_AGE and QUEEN_HUNT_APPROACH on (v15 params), hunters
// take part only within QUEEN_HUNT_RADIUS steps of her last known place (0 = any distance, as v15)
// and only before QUEEN_HUNT_UNTIL.
inline constexpr int QUEEN_HUNT_RADIUS = 0;
inline constexpr int QUEEN_HUNT_UNTIL = 999;
// 5. Small maps (tactic 5): below QUEEN_SMALL_AREA tiles the queens hardly matter (top-vs-top
// winners' queens were alive at round 200 in 30% of small-map games); the queen splits like a swarm
// dragon until QUEEN_SPLIT_UNTIL_SMALL (QUEEN_SPLIT_UNTIL elsewhere), and with QUEEN_FOOD_SMALL_OFF the
// others don't leave pearls near her. QUEEN_SPLIT_UNTIL_SMALL = 60 and false = off.
inline constexpr int QUEEN_SMALL_AREA = 1000;
inline constexpr int QUEEN_SPLIT_UNTIL_SMALL = 60;
inline constexpr bool QUEEN_FOOD_SMALL_OFF = false;

// 6. Eat-sprints (tactic 7): a dragon with one free step (2-4 long) may take a second pearl next to the
// first in the same turn, paying a segment (EAT_SPRINT_COST in the score), when an enemy head is within
// EAT_SPRINT_ENEMY_DIST steps of that pearl. false = off.
inline constexpr bool EAT_SPRINT = true;
inline constexpr double EAT_SPRINT_COST = 178.656122;
inline constexpr int EAT_SPRINT_ENEMY_DIST = 1;

// --- Pearl-belief model ----------------------------------------------------------
inline constexpr double ENEMY_CLOSER = 0.173499;     // enemy head reaches the pearl first
inline constexpr double ALLY_CLOSER = 0.205443;      // an ally reaches it first
inline constexpr double REMEMBERED_DECAY = 0.97; // per round a remembered pearl may have been eaten
inline constexpr double UNSEEN_PRIOR = 0.6;      // scale on "has probably spawned by now" for unseen tiles
inline constexpr int UNKNOWN_MAP_GAP = 50;      // assumed mean spawn gap when the map is unknown

} // namespace params
