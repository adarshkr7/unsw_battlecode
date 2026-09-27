#pragma once

// Tunables. The tournament script compares copies of the bot that differ only here.
namespace params {

// Log every decision (local debugging only: output costs points in the judge).
inline constexpr bool DEBUG = false;

// --- Phases ---------------------------------------------------------------
// Swarm phase: split in half whenever this long, up to the team's dragon cap. Many small
// dragons gather pearls in parallel, cannot all be caught, and trade heads cheaply.
inline constexpr int SWARM_SPLIT_LEN = 4;
// How eager swarm splits are: split even on a turn we could eat, even where an enemy head can
// reach the (standing) parent, and with safety 2 (both halves safe) or 1 (may lose 2 segments).
inline constexpr bool SPLIT_WHILE_EATING = true;
inline constexpr bool SPLIT_THREAT_OK = false;
inline constexpr int SPLIT_SAFETY = 2;
// From this round on: no more swarm splits; small dragons feed pearls to a big ally, so we
// finish with one long dragon (the winner at round 500 is the team with the longest one).
inline constexpr int CONSOLIDATE_ROUND = 380;

// Insurance: losing every dragon loses the game outright, so keep at least this many alive
// by splitting a small child off any dragon long enough to spare it.
inline constexpr int MIN_UNITS = 2;
inline constexpr int INSURANCE_MIN_LEN = 8;
inline constexpr int INSURANCE_CHILD = 2;

// --- Head trades (moving onto an enemy head kills both dragons) -------------
// Take an even trade (same length) only while we have at least this many dragons.
inline constexpr int TRADE_MIN_UNITS = 6;
// Longest sprint we plan in order to reach an enemy head (a length-L dragon can do L-1 steps).
inline constexpr int STRIKE_MAX_STEPS = 6;
// Standing where an enemy head can reach us next turn: per segment we would lose more than it.
inline constexpr double THREAT_PER_LEN = 60.0;
// ... and a small penalty when the trade it could take would be even.
inline constexpr double THREAT_EVEN = 15.0;
// With this few dragons left, stay out of reach of every enemy head and trade only for a big gain.
inline constexpr int LOW_UNITS = 0; // 0 = off (tested neutral)
inline constexpr double THREAT_LOW_UNITS = 800.0;
inline constexpr int LOW_UNITS_TRADE_GAIN = 4;
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
inline constexpr double FEED_ATTRACT = 400.0; // pull towards a king we could feed...
inline constexpr bool FEED_PULL_LINEAR = false;    // pull falls off linearly (else as 1/(1+steps))
inline constexpr double FEED_PULL_PER_STEP = 40.0; // ...minus this per step away, so far kings still pull

// --- Sonar ------------------------------------------------------------------------
// From BEACON_LEAD rounds before consolidation, the dragon that knows of no bigger ally
// announces its head and length every turn; everyone else relays the best king it knows.
inline constexpr bool SONAR = false; // king beacons: off (tested neutral)
// On a split, hand the child its body over sonar (out of our tail, into its tail).
inline constexpr bool HANDOFF = true;
// Share pearl sightings over sonar, and relay what we heard for this many rounds.
inline constexpr bool PEARL_SONAR = false; // off: tested neutral vs v2d (49-54%)
inline constexpr int PEARL_RELAY_AGE = 20;
inline constexpr bool PEARL_RELAY = true;   // also pass on pearls we only heard about
inline constexpr double HEARD_VALUE = 1.0; // belief scale for pearls we only heard about
inline constexpr int BEACON_LEAD = 20;
inline constexpr int KING_FRESH = 12; // rounds heard news stays usable
inline constexpr unsigned long long SONAR_KEY = 0x5DEECE66DA11E4EDULL;

// --- Move scoring weights -------------------------------------------------------
inline constexpr double EAT_NOW = 400.0;         // stepping onto a pearl this turn
inline constexpr double FOOD_BEST = 300.0;       // best discounted pearl reachable
inline constexpr double FOOD_SUM = 3.0;          // sum of discounted pearl value (local richness tie-break)
inline constexpr double GAMMA_BEST = 0.85;       // per-step discount for the best target
inline constexpr double GAMMA_SUM = 0.8;         // per-step discount for the sum
inline constexpr double TRAPPED = 20000.0;       // cannot reach our own tail and little room
// Count as our room only tiles we reach no later than any other dragon's head.
inline constexpr bool VORONOI = true;
// Diving: a dragon this short may enter a dead end holding at least this much pearl belief,
// if it will be long enough (4) to split its way out. Costs about the stuck parent.
inline constexpr int DIVE_MAX_LEN = 8;
inline constexpr double DIVE_MIN_POCKET = 1.0;
inline constexpr double DIVE_PENALTY = 500.0;
// Long dragons value room: per tile reachable, up to this many tiles per segment.
inline constexpr int KING_ROOM_LEN = 10;
inline constexpr double KING_ROOM = 6.0;
inline constexpr int KING_ROOM_TILES_PER_LEN = 3;
inline constexpr double UNKNOWN_PORTAL = 3000.0; // stepping into a portal whose far end we do not know
inline constexpr double OUT_OF_VIEW = 1500.0;    // landing on a tile we cannot see (through a portal)
inline constexpr double REVISIT = 40.0;          // per unit of recent-visit heat on the destination
inline constexpr double REVISIT_DECAY = 0.95;    // heat decay per round

// --- Pearl-belief model ----------------------------------------------------------
inline constexpr double ENEMY_CLOSER = 0.15;     // enemy head reaches the pearl first
inline constexpr double ALLY_CLOSER = 0.25;      // an ally reaches it first
inline constexpr double REMEMBERED_DECAY = 0.97; // per round a remembered pearl may have been eaten
inline constexpr double UNSEEN_PRIOR = 0.6;      // scale on "has probably spawned by now" for unseen tiles
inline constexpr int UNKNOWN_MAP_GAP = 150;      // assumed mean spawn gap when the map is unknown
// A known spawn we would reach early still counts, discounted per round of waiting (0 = off).
inline constexpr double FUTURE_WAIT_GAMMA = 0.0; // 0.7 and 0.85 both lost 42% to v2d

} // namespace params
