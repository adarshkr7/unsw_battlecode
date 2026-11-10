// Sonar messages. A ping carries 64 bits to the first dragon (ally or enemy) in its line, so
// every message is checked and scrambled with a key only our bot knows:
//
//   bits 0-2   type
//   bits 3-11  check: a keyed hash of type and payload (enemy pings fail it)
//   bits 12-63 payload, XOR-ed with a keyed mask (enemies can't read it)

#pragma once

#include <algorithm>
#include <cstdint>

namespace sonar {

// The three type bits are all taken: the enemy queen's sightings travel as PREY (see EnemyQueen).
enum Type : std::uint64_t { BODY = 1, KING = 2, FOOD = 3, PORTAL = 4, PREY = 5, THREAT = 6, HOME = 7 };

inline constexpr std::uint64_t KEY = 0xA11C0DEDB0A7F00Dull;
inline constexpr int PAYLOAD_BITS = 52;
inline constexpr std::uint64_t PAYLOAD_MASK = (1ull << PAYLOAD_BITS) - 1;

inline std::uint64_t mix(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

inline std::uint64_t check(std::uint64_t type, std::uint64_t payload) {
    return mix(KEY ^ (type << 56) ^ payload) & 0x1FF;
}

inline std::uint64_t pack(Type type, std::uint64_t payload) {
    payload &= PAYLOAD_MASK;
    std::uint64_t const scrambled = payload ^ (mix(KEY + type) & PAYLOAD_MASK);
    return static_cast<std::uint64_t>(type) | (check(type, payload) << 3) | (scrambled << 12);
}

inline bool unpack(std::uint64_t msg, Type& type, std::uint64_t& payload) {
    std::uint64_t const t = msg & 7;
    if (t != BODY && t != KING && t != FOOD && t != PORTAL && t != PREY && t != THREAT && t != HOME)
        return false;
    payload = (msg >> 12) ^ (mix(KEY + t) & PAYLOAD_MASK);
    if (((msg >> 3) & 0x1FF) != check(t, payload))
        return false;
    type = static_cast<Type>(t);
    return true;
}

// BODY: a new child's shape, from its head: 5 bits of length, then 2 bits per link (the
// direction from segment i to segment i+1), so up to 24 segments.
inline constexpr int BODY_MAX_LEN = 1 + (PAYLOAD_BITS - 5) / 2;

// KING: the longest dragon of ours the sender knows of. 12 bits id, 8 length, 7+7 x, y, 9 round,
// 6 longest enemy seen (capped at 63), 1 crowded, 2 zone mode.
struct King {
    int id = -1, len = 0, x = 0, y = 0, round = -1000;
    int enemyMax = 0; // the sender's longest enemy seen lately (spare bits: no extra pings)
    bool crowded = false; // the king has a feeder next to it: others wait outside the ring
    int mode = 0;     // anchored king's zone: 0 feed, 1 hold, 2 fight, 3 flee
};
inline std::uint64_t packKing(King const& k) {
    std::uint64_t p = static_cast<std::uint64_t>(k.id & 0xFFF);
    p |= static_cast<std::uint64_t>(k.len & 0xFF) << 12;
    p |= static_cast<std::uint64_t>(k.x & 0x7F) << 20;
    p |= static_cast<std::uint64_t>(k.y & 0x7F) << 27;
    p |= static_cast<std::uint64_t>(k.round & 0x1FF) << 34;
    p |= static_cast<std::uint64_t>(std::min(k.enemyMax, 63) & 0x3F) << 43;
    p |= static_cast<std::uint64_t>(k.crowded ? 1 : 0) << 49;
    p |= static_cast<std::uint64_t>(k.mode & 3) << 50;
    return pack(KING, p);
}
inline King unpackKing(std::uint64_t p) {
    King k;
    k.id = static_cast<int>(p & 0xFFF);
    k.len = static_cast<int>((p >> 12) & 0xFF);
    k.x = static_cast<int>((p >> 20) & 0x7F);
    k.y = static_cast<int>((p >> 27) & 0x7F);
    k.round = static_cast<int>((p >> 34) & 0x1FF);
    k.enemyMax = static_cast<int>((p >> 43) & 0x3F);
    k.crowded = ((p >> 49) & 1) != 0;
    k.mode = static_cast<int>((p >> 50) & 3);
    return k;
}

// THREAT: enemy heads a dragon near our king sees (radar): 7+7 their centroid, 4 how many (capped
// at 15), 4 our heads it sees including itself, 9 round, 6 longest of them (capped at 63).
struct Threat {
    int cx = 0, cy = 0, e = 0, a = 0, round = -1000, maxlen = 0;
};
inline std::uint64_t packThreat(Threat const& t) {
    std::uint64_t p = static_cast<std::uint64_t>(t.cx & 0x7F);
    p |= static_cast<std::uint64_t>(t.cy & 0x7F) << 7;
    p |= static_cast<std::uint64_t>(std::min(t.e, 15) & 0xF) << 14;
    p |= static_cast<std::uint64_t>(std::min(t.a, 15) & 0xF) << 18;
    p |= static_cast<std::uint64_t>(t.round & 0x1FF) << 22;
    p |= static_cast<std::uint64_t>(std::min(t.maxlen, 63) & 0x3F) << 31;
    return pack(THREAT, p);
}
// HOME: where our team started (7+7 x, y). A child is a new process that knows nothing: the starting
// dragons know it, everyone relays it.
inline std::uint64_t packHome(int x, int y) {
    return pack(HOME, static_cast<std::uint64_t>(x & 0x7F) | (static_cast<std::uint64_t>(y & 0x7F) << 7));
}
inline Threat unpackThreat(std::uint64_t p) {
    Threat t;
    t.cx = static_cast<int>(p & 0x7F);
    t.cy = static_cast<int>((p >> 7) & 0x7F);
    t.e = static_cast<int>((p >> 14) & 0xF);
    t.a = static_cast<int>((p >> 18) & 0xF);
    t.round = static_cast<int>((p >> 22) & 0x1FF);
    t.maxlen = static_cast<int>((p >> 31) & 0x3F);
    return t;
}

// FOOD: what a dragon knows of the map that a newborn child (a fresh process) does not: the
// map's symmetry and up to three rich pearl tiles. 2 bits of symmetry (0 unknown, 1 mirror x,
// 2 mirror y, 3 rotation), then per spot 6 bits x, 6 bits y, 4 bits class (0 = empty slot;
// the largest countdown seen there is about class squared).
struct Food {
    int sym = 0;
    int x[3] = {0, 0, 0}, y[3] = {0, 0, 0}, cls[3] = {0, 0, 0};
};
inline std::uint64_t packFood(Food const& f) {
    std::uint64_t p = static_cast<std::uint64_t>(f.sym & 3);
    for (int i = 0; i < 3; i++) {
        std::uint64_t spot = static_cast<std::uint64_t>(f.x[i] & 0x3F) | (static_cast<std::uint64_t>(f.y[i] & 0x3F) << 6) |
                             (static_cast<std::uint64_t>(f.cls[i] & 0xF) << 12);
        p |= spot << (2 + 16 * i);
    }
    return pack(FOOD, p);
}
inline Food unpackFood(std::uint64_t p) {
    Food f;
    f.sym = static_cast<int>(p & 3);
    for (int i = 0; i < 3; i++) {
        std::uint64_t const spot = p >> (2 + 16 * i);
        f.x[i] = static_cast<int>(spot & 0x3F);
        f.y[i] = static_cast<int>((spot >> 6) & 0x3F);
        f.cls[i] = static_cast<int>((spot >> 12) & 0xF);
    }
    return f;
}

// PORTAL: one connected pair. 14 bits of portal id, then per end 1 bit orientation (1: a
// horizontal edge, the north side of a tile) and 6+6 bits of that tile's x, y.
struct Portal {
    int pid = -1;
    bool h1 = false, h2 = false;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};
inline constexpr int PORTAL_MAX_ID = (1 << 14) - 1;
inline std::uint64_t packPortal(Portal const& p) {
    std::uint64_t v = static_cast<std::uint64_t>(p.pid & PORTAL_MAX_ID);
    auto end = [](bool h, int x, int y) {
        return static_cast<std::uint64_t>(h ? 1 : 0) | (static_cast<std::uint64_t>(x & 0x3F) << 1) |
               (static_cast<std::uint64_t>(y & 0x3F) << 7);
    };
    v |= end(p.h1, p.x1, p.y1) << 14;
    v |= end(p.h2, p.x2, p.y2) << 27;
    return pack(PORTAL, v);
}
inline Portal unpackPortal(std::uint64_t v) {
    Portal p;
    p.pid = static_cast<int>(v & PORTAL_MAX_ID);
    std::uint64_t const a = v >> 14, b = v >> 27;
    p.h1 = (a & 1) != 0;
    p.x1 = static_cast<int>((a >> 1) & 0x3F);
    p.y1 = static_cast<int>((a >> 7) & 0x3F);
    p.h2 = (b & 1) != 0;
    p.x2 = static_cast<int>((b >> 1) & 0x3F);
    p.y2 = static_cast<int>((b >> 7) & 0x3F);
    return p;
}

// PREY: the longest enemy dragon the sender knows of (pack hunting): 12 bits id, 8 length,
// 7+7 x, y, 9 round seen.
struct Prey {
    int id = -1, len = 0, x = 0, y = 0, round = -1000;
};
inline std::uint64_t packPrey(Prey const& k) {
    std::uint64_t p = static_cast<std::uint64_t>(k.id & 0xFFF);
    p |= static_cast<std::uint64_t>(k.len & 0xFF) << 12;
    p |= static_cast<std::uint64_t>(k.x & 0x7F) << 20;
    p |= static_cast<std::uint64_t>(k.y & 0x7F) << 27;
    p |= static_cast<std::uint64_t>(k.round & 0x1FF) << 34;
    return pack(PREY, p);
}
inline Prey unpackPrey(std::uint64_t p) {
    Prey k;
    k.id = static_cast<int>(p & 0xFFF);
    k.len = static_cast<int>((p >> 12) & 0xFF);
    k.x = static_cast<int>((p >> 20) & 0x7F);
    k.y = static_cast<int>((p >> 27) & 0x7F);
    k.round = static_cast<int>((p >> 34) & 0x1FF);
    return k;
}

// The enemy queen (v9's ENEMY_QUEEN): where one of ours last saw her head, relayed like KING so
// the hunters converge on her. It travels as a PREY record carrying her id (0 or 1).
struct EnemyQueen {
    int x = 0, y = 0, round = -1000, len = 0, id = -1;
};
inline std::uint64_t packEnemyQueen(EnemyQueen const& q) {
    return packPrey({q.id, q.len, q.x, q.y, q.round});
}
inline EnemyQueen enemyQueenOf(Prey const& k) {
    return {k.x, k.y, k.round, k.len, k.id};
}

} // namespace sonar
