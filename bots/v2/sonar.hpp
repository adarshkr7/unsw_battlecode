// Sonar messages. A ping carries 64 bits to the first dragon (ally or enemy) in its line, so
// every message is checked and scrambled with a key only our bot knows:
//
//   bits 0-2   type
//   bits 3-11  check: a keyed hash of type and payload (enemy pings fail it)
//   bits 12-63 payload, XOR-ed with a keyed mask (enemies can't read it)

#pragma once

#include <cstdint>

namespace sonar {

enum Type : std::uint64_t { BODY = 1, KING = 2, FOOD = 3, ENEMY_QUEEN = 4 };

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
    if (t != BODY && t != KING && t != FOOD && t != ENEMY_QUEEN)
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

// KING: the longest dragon of ours the sender knows of.
struct King {
    int id = -1, len = 0, x = 0, y = 0, round = -1000;
    int enemyMax = 0; // the sender's longest enemy seen lately (spare bits: no extra pings)
    bool crowded = false; // the king has a feeder next to it: others wait outside the ring
};
inline std::uint64_t packKing(King const& k) {
    std::uint64_t p = static_cast<std::uint64_t>(k.id & 0xFFF);
    p |= static_cast<std::uint64_t>(k.len & 0xFF) << 12;
    p |= static_cast<std::uint64_t>(k.x & 0x7F) << 20;
    p |= static_cast<std::uint64_t>(k.y & 0x7F) << 27;
    p |= static_cast<std::uint64_t>(k.round & 0x1FF) << 34;
    p |= static_cast<std::uint64_t>(k.enemyMax & 0xFF) << 43;
    p |= static_cast<std::uint64_t>(k.crowded ? 1 : 0) << 51;
    return pack(KING, p);
}
inline King unpackKing(std::uint64_t p) {
    King k;
    k.id = static_cast<int>(p & 0xFFF);
    k.len = static_cast<int>((p >> 12) & 0xFF);
    k.x = static_cast<int>((p >> 20) & 0x7F);
    k.y = static_cast<int>((p >> 27) & 0x7F);
    k.round = static_cast<int>((p >> 34) & 0x1FF);
    k.enemyMax = static_cast<int>((p >> 43) & 0xFF);
    k.crowded = ((p >> 51) & 1) != 0;
    return k;
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

// ENEMY_QUEEN: where one of ours last saw the enemy queen's head, relayed like KING so the
// hunters converge on her.
struct EnemyQueen {
    int x = 0, y = 0, round = -1000, len = 0;
};
inline std::uint64_t packEnemyQueen(EnemyQueen const& q) {
    std::uint64_t p = static_cast<std::uint64_t>(q.x & 0x7F);
    p |= static_cast<std::uint64_t>(q.y & 0x7F) << 7;
    p |= static_cast<std::uint64_t>(q.round & 0x1FF) << 14;
    p |= static_cast<std::uint64_t>(q.len & 0xFF) << 23;
    return pack(ENEMY_QUEEN, p);
}
inline EnemyQueen unpackEnemyQueen(std::uint64_t p) {
    EnemyQueen q;
    q.x = static_cast<int>(p & 0x7F);
    q.y = static_cast<int>((p >> 7) & 0x7F);
    q.round = static_cast<int>((p >> 14) & 0x1FF);
    q.len = static_cast<int>((p >> 23) & 0xFF);
    return q;
}

} // namespace sonar
