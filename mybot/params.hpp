#pragma once

// Tunables. The tournament script compares copies of the bot that differ only here.
namespace params {

// Log every decision (local debugging only: output costs points in the judge).
inline constexpr bool DEBUG = false;

// --- Phases ---------------------------------------------------------------
// Swarm phase: split in half whenever this long, up to the team's dragon cap. Many small
// dragons gather pearls in parallel, cannot all be caught, and trade heads cheaply.
inline constexpr int SWARM_SPLIT_LEN = 4;
// Split even on a turn we could eat (the parent then waits a turn). v2d found ~20% more
// dragons and ~15% more total length by round 150 this way.
inline constexpr bool SPLIT_WHILE_EATING = true;
// From this round on: no more swarm splits; small dragons feed pearls to a big ally, so we
// finish with one long dragon (the winner at round 500 is the team with the longest one).
inline constexpr int CONSOLIDATE_ROUND = 380;

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
inline constexpr double REVISIT = 40.0;          // per unit of recent-visit heat on the destination
inline constexpr double REVISIT_DECAY = 0.95;    // heat decay per round

// --- Diving into lanes and dead ends (where most pearls spawn on maze maps) ----------
inline constexpr bool DIVE = true;
inline constexpr double DIVE_MIN_FOOD = 1.0;     // expected pearls in the pocket before we bother
inline constexpr int DIVE_MAX_LEN = 5;
// v2d-style dives (profiles with careDive off): longer dragons, one flat price.
inline constexpr int V2D_DIVE_MAX_LEN = 8;
inline constexpr double V2D_DIVE_PENALTY = 500.0;           // longer dragons are worth too much to risk
inline constexpr int DIVE_ESCAPE_LEN = 4;        // this long after eating: we can split our way out
inline constexpr double DIVE_ESCAPE_COST = 400.0; // the 2-long parent we leave behind, roughly
inline constexpr int DIVE_MAX_DEATH_LEN = 3;     // dragons this small may die in there...
inline constexpr int DIVE_MIN_UNITS = 8;         // ...while we have at least this many
inline constexpr double DIVE_SEG_COST = 350.0;   // per segment of a dragon that dies in the pocket

// --- Sonar ------------------------------------------------------------------------
inline constexpr bool SONAR = true;
inline constexpr bool DEBUG_SONAR = false;    // log body handoffs (local debugging only)
inline constexpr int KING_STALE = 30;         // rounds a king record lives without a refresh
inline constexpr int KING_PULL_ROUND = 340;   // from here, head for the king we know of
inline constexpr double KING_APPROACH = 40.0; // per step closer to that king
inline constexpr int KING_FOOD_RADIUS = 3;         // pearls this close to the king are its own
inline constexpr double KING_FOOD_PENALTY = 300.0; // for a feeder that eats one anyway

// --- Pearl-belief model ----------------------------------------------------------
inline constexpr double ENEMY_CLOSER = 0.15;     // enemy head reaches the pearl first
inline constexpr double ALLY_CLOSER = 0.25;      // an ally reaches it first
inline constexpr double REMEMBERED_DECAY = 0.97; // per round a remembered pearl may have been eaten
inline constexpr double UNSEEN_PRIOR = 0.6;      // scale on "has probably spawned by now" for unseen tiles
inline constexpr int UNKNOWN_MAP_GAP = 150;      // assumed mean spawn gap when the map is unknown

} // namespace params
