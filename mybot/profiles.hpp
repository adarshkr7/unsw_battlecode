// Per-map strategy. Every dragon recognises its map on its first turn (maps_data.hpp) and plays
// with that map's profile; maps we do not know use DEFAULT_PROFILE.
//
// The two profiles are the two bots v5 merges. They win on different maps (v4 vs v2d head to head):
//   V4  - the whole team agrees on one king over sonar and rallies to it from kingPullRound,
//         leaving pearls near the king for the king; careful, priced dives.
//   V2D - feed only a king you can see, from round 380; looser dives (up to length 8).
// Edit the table below to move a map from one to the other, or give it its own profile.

#pragma once

#include <cstring>

struct Profile {
    bool kingSonar;    // agree on one king over sonar and rally to it (else: feed a king in view)
    int kingPullRound; // with kingSonar, start heading for the king this round
    bool feedYield;    // leave pearls near the king for the king
    bool careDive;     // v4 dives: small dragons, spare units, split anyway when doomed
};

inline constexpr Profile V4 = {true, 340, true, true};
inline constexpr Profile V2D = {false, 340, false, false};

struct MapProfile {
    char const* map; // file name without .map
    Profile profile;
};

// From v4 vs v2d head to head (which bot won the map). Maps with no clear winner stay on V4.
inline constexpr MapProfile MAP_PROFILES[] = {
    {"arena", V2D},
    {"autarky", V4},
    {"big_empty", V4},
    {"Colosseum", V4},
    {"default", V2D},
    {"default_small", V2D},
    {"devil", V4},
    {"dilemma", V4},
    {"portals", V2D},
    {"queen_of_spades", V4},
    {"schooltime", V2D},
    {"slithery_fight", V4},
    {"stronghold", V4},
    {"trauma", V4},
    {"trophy", V2D},
};

inline constexpr Profile DEFAULT_PROFILE = V4;

inline Profile profileFor(char const* map) {
    for (auto const& mp : MAP_PROFILES)
        if (std::strcmp(mp.map, map) == 0)
            return mp.profile;
    return DEFAULT_PROFILE;
}

// The profile this dragon plays with (set when it recognises the map).
inline Profile prof = DEFAULT_PROFILE;
