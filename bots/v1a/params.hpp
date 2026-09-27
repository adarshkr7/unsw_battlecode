#pragma once

// Tunables. The tournament script compares copies of the bot that differ only here.
namespace params {

// Log every decision (local debugging only: output costs points in the judge).
inline constexpr bool DEBUG = true;

// Voluntary splitting: split off half the body once this long (0 = never split voluntarily).
inline constexpr int SPLIT_MIN_LEN = 0;
// No voluntary splits after this round, so the survivors have time to grow.
inline constexpr int SPLIT_LAST_ROUND = 300;

// Insurance: losing every dragon loses the game outright, so keep at least this many alive
// by splitting a small child off any dragon long enough to spare it.
inline constexpr int MIN_UNITS = 3;
inline constexpr int INSURANCE_MIN_LEN = 8;
inline constexpr int INSURANCE_CHILD = 3;

// Move scoring weights.
inline constexpr double EAT_NOW = 400.0;         // stepping onto a pearl this turn
inline constexpr double FOOD_BEST = 300.0;       // best discounted pearl reachable
inline constexpr double FOOD_SUM = 25.0;         // sum of discounted pearl value (pulls towards rich areas)
inline constexpr double GAMMA_BEST = 0.85;       // per-step discount for the best target
inline constexpr double GAMMA_SUM = 0.93;        // per-step discount for the sum
inline constexpr double TRAPPED = 20000.0;       // flood fill smaller than our length
inline constexpr double HEAD_ADJ_BASE = 150.0;   // next to an enemy head, which may trade heads with us
inline constexpr double HEAD_ADJ_PER_LEN = 25.0;
inline constexpr double UNKNOWN_PORTAL = 3000.0; // stepping into a portal whose far end we do not know
inline constexpr double OUT_OF_VIEW = 1500.0;    // landing on a tile we cannot see (through a portal)

// Pearl-belief model.
inline constexpr double ENEMY_CLOSER = 0.15;     // enemy head reaches the pearl first
inline constexpr double ALLY_CLOSER = 0.25;      // an ally reaches it first
inline constexpr double REMEMBERED_DECAY = 0.97; // per round a remembered pearl may have been eaten
inline constexpr double UNSEEN_PRIOR = 0.6;      // scale on "has probably spawned by now" for unseen tiles
inline constexpr int UNKNOWN_MAP_GAP = 150;      // assumed mean spawn gap when the map is unknown

} // namespace params
