// Sonar messages. A ping carries 64 bits to the first dragon (ally or enemy) in its line, so
// every message is checked and scrambled with a key only our bot knows:
//
//   bits 0-2   type
//   bits 3-11  check: a keyed hash of type and payload (enemy pings fail it)
//   bits 12-63 payload, XOR-ed with a keyed mask (enemies can't read it)

#pragma once

#include <cstdint>

namespace sonar {

enum Type : std::uint64_t { BODY = 1, KING = 2 };

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
    if (t != BODY && t != KING)
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
};
inline std::uint64_t packKing(King const& k) {
    std::uint64_t p = static_cast<std::uint64_t>(k.id & 0xFFF);
    p |= static_cast<std::uint64_t>(k.len & 0xFF) << 12;
    p |= static_cast<std::uint64_t>(k.x & 0x7F) << 20;
    p |= static_cast<std::uint64_t>(k.y & 0x7F) << 27;
    p |= static_cast<std::uint64_t>(k.round & 0x1FF) << 34;
    return pack(KING, p);
}
inline King unpackKing(std::uint64_t p) {
    King k;
    k.id = static_cast<int>(p & 0xFFF);
    k.len = static_cast<int>((p >> 12) & 0xFF);
    k.x = static_cast<int>((p >> 20) & 0x7F);
    k.y = static_cast<int>((p >> 27) & 0x7F);
    k.round = static_cast<int>((p >> 34) & 0x1FF);
    return k;
}

} // namespace sonar
