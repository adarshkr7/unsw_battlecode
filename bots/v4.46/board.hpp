// Board knowledge shared by every part of the bot: the embedded maps, what we have
// learned about edges and spawn rates, and how a step off a tile resolves (kelp, portal, wrap).

#pragma once

#include "helper.hpp"

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bc {

using namespace unswbc;

inline constexpr int INF = 1 << 29;
inline constexpr int DX[4] = {0, 1, 0, -1};
inline constexpr int DY[4] = {-1, 0, 1, 0};
inline constexpr char DCH[4] = {'N', 'E', 'S', 'W'};

// Results of stepping off a tile, besides a tile index.
inline constexpr int STEP_KELP = -1;
inline constexpr int STEP_UNKNOWN_PORTAL = -2;

enum : int8_t { EK_UNKNOWN = -1, EK_OPEN = 0, EK_KELP = 1, EK_PORTAL = 2 };

inline int W = 0, H = 0, N = 0;

inline int dirIndex(Direction d) {
    switch (d.value) {
    case Direction::NORTH: return 0;
    case Direction::EAST: return 1;
    case Direction::SOUTH: return 2;
    default: return 3;
    }
}
inline int wrapX(int x) { return (x % W + W) % W; }
inline int wrapY(int y) { return (y % H + H) % H; }
inline int idxOf(int x, int y) { return y * W + x; }
inline int idxOf(Position p) { return p.y * W + p.x; }

// ---------------------------------------------------------------------------
// Embedded maps

struct Spawn {
    int team; // 0 = A, 1 = B
    std::vector<int> body; // head first
};

struct MapInfo {
    int w = 0, h = 0;
    char sym = 0; // 'h' mirrors x, 'v' mirrors y, 'r' rotates 180, 0 none
    std::vector<int8_t> hKind, vKind; // hKind[t]: north side of tile t; vKind[t]: west side
    std::vector<int> hPid, vPid;
    std::vector<int16_t> minGap, maxGap;
    std::vector<Spawn> spawns;
};

struct Reader {
    char const* p;
    static bool space(char c) { return c == ' ' || c == 10 || c == 13 || c == 9; }
    std::string_view token() {
        while (space(*p))
            p++;
        char const* start = p;
        while (*p && !space(*p))
            p++;
        return {start, static_cast<std::size_t>(p - start)};
    }
    int integer() {
        while (space(*p))
            p++;
        bool const negative = *p == '-';
        if (negative)
            p++;
        int value = 0;
        while (*p >= '0' && *p <= '9')
            value = value * 10 + (*p++ - '0');
        return negative ? -value : value;
    }
};

inline bool parseMap(char const* text, MapInfo& m) {
    Reader r{text};
    for (std::string_view tok = r.token(); !tok.empty(); tok = r.token()) {
        if (tok == "MAP") {
            m.w = r.integer();
            m.h = r.integer();
            int const n = m.w * m.h;
            m.hKind.assign(n, EK_OPEN);
            m.vKind.assign(n, EK_OPEN);
            m.hPid.assign(n, -1);
            m.vPid.assign(n, -1);
            m.minGap.assign(n, 0);
            m.maxGap.assign(n, 0);
        } else if (tok == "SYMMETRY") {
            std::string_view const s = r.token();
            m.sym = s == "y" ? 'h' : s == "x" ? 'v' : s == "xy" ? 'r' : 0;
        } else if (tok == "TILE") {
            int const x = r.integer(), y = r.integer(), a = r.integer(), b = r.integer();
            if (x >= 0 && y >= 0 && x < m.w && y < m.h) {
                m.minGap[y * m.w + x] = static_cast<int16_t>(a);
                m.maxGap[y * m.w + x] = static_cast<int16_t>(b);
            }
        } else if (tok == "EDGE") {
            int const index = r.integer(), kind = r.integer(), pid = r.integer();
            int const row = index / (m.w + 1), col = index % (m.w + 1);
            if (row % 2 == 0) {
                int y = row / 2;
                if (y >= m.h)
                    y -= m.h;
                if (col < m.w && y < m.h) {
                    m.hKind[y * m.w + col] = static_cast<int8_t>(kind);
                    m.hPid[y * m.w + col] = pid;
                }
            } else {
                int const y = (row - 1) / 2;
                int x = col;
                if (x >= m.w)
                    x -= m.w;
                if (y < m.h) {
                    m.vKind[y * m.w + x] = static_cast<int8_t>(kind);
                    m.vPid[y * m.w + x] = pid;
                }
            }
        } else if (tok == "DRAGON") {
            Spawn s;
            s.team = r.integer();
            int const count = r.integer();
            for (int i = 0; i < count; i++) {
                int const x = r.integer(), y = r.integer();
                s.body.push_back(y * m.w + x);
            }
            m.spawns.push_back(std::move(s));
        } else if (tok.size() > 6 && tok.compare(tok.size() - 6, 6, "_COUNT") == 0) {
            r.integer();
        }
    }
    return m.w > 0 && m.h > 0;
}

// ---------------------------------------------------------------------------
// Long-lived knowledge of the board

struct Knowledge {
    bool fromMap = false;
    char sym = 0;
    std::vector<int8_t> hKind, vKind;
    std::vector<int> hPid, vPid;
    std::vector<int16_t> minGap, maxGap; // -1 when unknown
    std::unordered_map<int, std::vector<std::pair<bool, int>>> portalEnds; // pid -> (horizontal, tile)

    void reset() {
        hKind.assign(N, EK_UNKNOWN);
        vKind.assign(N, EK_UNKNOWN);
        hPid.assign(N, -1);
        vPid.assign(N, -1);
        minGap.assign(N, -1);
        maxGap.assign(N, -1);
        portalEnds.clear();
    }

    void addPortalEnd(int pid, bool horizontal, int tile) {
        auto& ends = portalEnds[pid];
        for (auto const& e : ends)
            if (e.first == horizontal && e.second == tile)
                return;
        ends.emplace_back(horizontal, tile);
    }

    void loadMap(MapInfo const& m, bool spawns) {
        fromMap = true;
        sym = m.sym;
        hKind = m.hKind;
        vKind = m.vKind;
        hPid = m.hPid;
        vPid = m.vPid;
        if (spawns) {
            minGap = m.minGap;
            maxGap = m.maxGap;
        }
        portalEnds.clear();
        for (int t = 0; t < N; t++) {
            if (hKind[t] == EK_PORTAL)
                addPortalEnd(hPid[t], true, t);
            if (vKind[t] == EK_PORTAL)
                addPortalEnd(vPid[t], false, t);
        }
    }
};

inline Knowledge K;

// The edge crossed leaving tile t in direction d.
inline void edgeAt(int t, int d, bool& horizontal, int& e) {
    int const x = t % W, y = t / W;
    switch (d) {
    case 0: horizontal = true; e = t; break;
    case 2: horizontal = true; e = idxOf(x, wrapY(y + 1)); break;
    case 3: horizontal = false; e = t; break;
    default: horizontal = false; e = idxOf(wrapX(x + 1), y); break;
    }
}

inline int computeStep(int t, int d) {
    bool horizontal;
    int e;
    edgeAt(t, d, horizontal, e);
    int const kind = horizontal ? K.hKind[e] : K.vKind[e];
    if (kind == EK_KELP)
        return STEP_KELP;
    if (kind == EK_PORTAL) {
        int const pid = horizontal ? K.hPid[e] : K.vPid[e];
        auto const it = K.portalEnds.find(pid);
        if (it == K.portalEnds.end())
            return STEP_UNKNOWN_PORTAL;
        for (auto const& [ph, pt] : it->second) {
            if (ph == horizontal && pt == e)
                continue;
            int const px = pt % W, py = pt / W;
            // Direction is kept: we come out on the far side of the partner edge.
            if (ph)
                return d == 2 ? pt : idxOf(px, wrapY(py - 1));
            return d == 1 ? pt : idxOf(wrapX(px - 1), py);
        }
        return STEP_UNKNOWN_PORTAL;
    }
    return idxOf(wrapX(t % W + DX[d]), wrapY(t / W + DY[d]));
}

inline int mirrorOf(int t) {
    int const x = t % W, y = t / W;
    switch (K.sym) {
    case 'h': return idxOf(W - 1 - x, y);
    case 'v': return idxOf(x, H - 1 - y);
    case 'r': return idxOf(W - 1 - x, H - 1 - y);
    default: return -1;
    }
}

// The mirror of an edge (h: the north side of tile e, else its west side) under the map's symmetry.
inline int mirrorEdge(int e, bool horizontal) {
    int const x = e % W, y = e / W;
    switch (K.sym) {
    case 'h': return horizontal ? idxOf(W - 1 - x, y) : idxOf(wrapX(W - x), y);
    case 'v': return horizontal ? idxOf(x, wrapY(H - y)) : idxOf(x, H - 1 - y);
    case 'r': return horizontal ? idxOf(W - 1 - x, wrapY(H - y)) : idxOf(wrapX(W - x), H - 1 - y);
    default: return -1;
    }
}

inline int8_t kindOf(Edge const& edge) {
    switch (edge.get_edge_type()) {
    case EdgeType::KELP: return EK_KELP;
    case EdgeType::PORTAL: return EK_PORTAL;
    default: return EK_OPEN;
    }
}

} // namespace bc
