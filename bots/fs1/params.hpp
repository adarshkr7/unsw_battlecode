#pragma once

// fs series (fast swarm). Forked from the v4.x bot at v4.45 (these defaults = v4.45);
// see fs_series_design.md. Tunables: benchmarks compare copies that differ only here.
namespace params {

// Log every decision (local debugging only: output costs points in the judge).
inline constexpr bool DEBUG = false;
// Use the built-in ladder maps (walls, portals, spawn rates, start bodies). Off = play as on an
// unseen tournament map, learning only from what we see.
inline constexpr bool USE_MAP_DATA = true;
// Recognise a map by walls and portals; tolerate this many visible tiles whose pearl spawning
// differs from our copy (the ladder changes maps). 0 = the old strict match.
inline constexpr int SPAWN_MISMATCH_OK = 3;
// Trust our copy's pearl spawns (tiles and gaps). Off: recognise the map by walls and portals
// only and learn spawns from view, as on an unknown map. The ladder now plays "variants": the
// same walls with different, unannounced pearl spawns.
inline constexpr bool USE_SPAWN_DATA = false;
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
// From this round on: no more swarm splits; small dragons feed pearls to a big ally, so we
// finish with one long dragon (the winner at round 500 is the team with the longest one).
inline constexpr int CONSOLIDATE_ROUND = 440;
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
inline constexpr int TRADE_MIN_UNITS = 6;
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
inline constexpr int KEEP_WHOLE_MIN_SPACE = 999; // endgame king: no escape split while it has this much room (999 = off)
inline constexpr bool DOOMED_BLIND_SPLIT = true;  // doomed with a partly known body: split anyway
inline constexpr bool FINAL_MERGE = true;     // from FINAL_MERGE_ROUND, feed down to one dragon
inline constexpr int FINAL_MERGE_ROUND = 450;
inline constexpr double FEED_ATTRACT = 400.0; // pull towards a king we could feed

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
// ... for a dragon of DIVE_MAX_LEN or less, whose far end we know: Portals hides every food box
// behind a portal, and the 2000-rated team out-ate us 106 to 12 by round 50 there. 1500 = off.
inline constexpr double OUT_OF_VIEW_SMALL = 150.0;
inline constexpr double REVISIT = 40.0;          // per unit of recent-visit heat on the destination
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
inline constexpr double DIVE_SEG_COST = 350.0;   // per segment of a dragon that dies in the pocket

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
// A tile only counts as new if its mirror is unseen too (the map is symmetric, so the enemy half
// teaches nothing our half hasn't), and never where an enemy head can reach next turn.
inline constexpr bool EXPLORE_SMART = true;
// Density-gated even trades: take an even head trade (outside aggressive mode) only where our other
// heads near the target outnumber theirs, i.e. where we will eat the pearls the two dead dragons
// drop. On the ladder fast swarms ate 60% of those drops (+86 vs our 58 pearls a game). Off: v4.x
// (any even trade while we have TRADE_MIN_UNITS dragons).
inline constexpr bool TRADE_DENSITY = true;
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
inline constexpr int KING_PULL_ROUND = 400;   // from here, head for the king we know of
inline constexpr double KING_APPROACH = 40.0; // per step closer to that king
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
inline constexpr bool SPLIT_RELAXED = false;     // swarm split if one half is safe and the child has an exit

// --- Pearl-belief model ----------------------------------------------------------
inline constexpr double ENEMY_CLOSER = 0.15;     // enemy head reaches the pearl first
inline constexpr double ALLY_CLOSER = 0.25;      // an ally reaches it first
inline constexpr double REMEMBERED_DECAY = 0.97; // per round a remembered pearl may have been eaten
inline constexpr double UNSEEN_PRIOR = 0.6;      // scale on "has probably spawned by now" for unseen tiles
inline constexpr int UNKNOWN_MAP_GAP = 50;      // assumed mean spawn gap when the map is unknown

} // namespace params
