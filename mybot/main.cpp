// AllRed v1: map knowledge + survival + greedy growth.
//
// Each turn: fold the 7x7 view into long-lived knowledge (edges, pearl timers, the
// embedded map if we recognised it), then score the four moves. A move is fatal if it
// hits kelp or any dragon; otherwise it is scored on pearls reachable from it (a
// belief model over every tile, discounted by distance and by who gets there first),
// on the space left to us (time-aware flood fill, since our own body frees up as we
// move), and on head-to-head risk from adjacent enemy heads.

#include "helper.hpp"
#include "maps_data.hpp"
#include "params.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace unswbc;

namespace {

constexpr int INF = 1 << 29;
constexpr int DX[4] = {0, 1, 0, -1};
constexpr int DY[4] = {-1, 0, 1, 0};
constexpr char DCH[4] = {'N', 'E', 'S', 'W'};

// Results of stepping off a tile, besides a tile index.
constexpr int STEP_KELP = -1;
constexpr int STEP_UNKNOWN_PORTAL = -2;

enum : int8_t { EK_UNKNOWN = -1, EK_OPEN = 0, EK_KELP = 1, EK_PORTAL = 2 };

int W = 0, H = 0, N = 0;

int dirIndex(Direction d) {
    switch (d.value) {
    case Direction::NORTH: return 0;
    case Direction::EAST: return 1;
    case Direction::SOUTH: return 2;
    default: return 3;
    }
}
int wrapX(int x) { return (x % W + W) % W; }
int wrapY(int y) { return (y % H + H) % H; }
int idxOf(int x, int y) { return y * W + x; }
int idxOf(Position p) { return p.y * W + p.x; }

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

bool parseMap(char const* text, MapInfo& m) {
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

    void loadMap(MapInfo const& m) {
        fromMap = true;
        sym = m.sym;
        hKind = m.hKind;
        vKind = m.vKind;
        hPid = m.hPid;
        vPid = m.vPid;
        minGap = m.minGap;
        maxGap = m.maxGap;
        portalEnds.clear();
        for (int t = 0; t < N; t++) {
            if (hKind[t] == EK_PORTAL)
                addPortalEnd(hPid[t], true, t);
            if (vKind[t] == EK_PORTAL)
                addPortalEnd(vPid[t], false, t);
        }
    }
};

Knowledge K;

// The edge crossed leaving tile t in direction d.
void edgeAt(int t, int d, bool& horizontal, int& e) {
    int const x = t % W, y = t / W;
    switch (d) {
    case 0: horizontal = true; e = t; break;
    case 2: horizontal = true; e = idxOf(x, wrapY(y + 1)); break;
    case 3: horizontal = false; e = t; break;
    default: horizontal = false; e = idxOf(wrapX(x + 1), y); break;
    }
}

int computeStep(int t, int d) {
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

int mirrorOf(int t) {
    int const x = t % W, y = t / W;
    switch (K.sym) {
    case 'h': return idxOf(W - 1 - x, y);
    case 'v': return idxOf(x, H - 1 - y);
    case 'r': return idxOf(W - 1 - x, H - 1 - y);
    default: return -1;
    }
}

int8_t kindOf(Edge const& edge) {
    switch (edge.get_edge_type()) {
    case EdgeType::KELP: return EK_KELP;
    case EdgeType::PORTAL: return EK_PORTAL;
    default: return EK_OPEN;
    }
}

// ---------------------------------------------------------------------------
// The brain

struct Brain {
    Controller& ct;
    Game& game;
    int myId;
    char myTeam;
    std::mt19937 rng;

    // Per-tile memory.
    std::vector<int> lastSeen, nextSpawn, visitRound;
    std::vector<double> visitHeat; // how recently and often our head stood here (breaks loops)
    std::vector<uint8_t> pearlSeen;

    // This turn's view.
    std::vector<uint8_t> inView, pearlNow;
    std::vector<int> occId; // dragon id on the tile, -1 if none
    std::vector<char> occTeam;
    std::vector<uint8_t> occHead;
    std::vector<int8_t> occDir;
    std::vector<int> releaseAt; // other dragons' tiles: our move number at which the tile frees (INF unknown)
    std::vector<int> nbr; // nbr[t*4+d]

    struct Head { int id; int tile; };
    std::vector<Head> enemyHeads, allyHeads;

    // Our own body, head first. Tracked across turns so we know it beyond the window.
    std::deque<int> body;
    std::vector<int> plannedPath;
    bool haveBody = false;

    // Scratch.
    std::vector<int> distE, distA, ownerA, block, dist, queue, staticBlock, seen;
    std::vector<std::pair<int, int>> blockSaved;
    int stamp = 0;
    std::vector<int> ownStamp; // == ownScenario for tiles of our own body in the current scenario
    int ownScenario = 0;
    std::vector<double> powBest, powSum, decayPow, heatDecay;

    bool identified = false;
    int round = 0;

    Brain(Controller& c, Game& g) : ct(c), game(g), myId(c.get_id()), myTeam(c.get_team().value),
                                    rng(static_cast<unsigned>(c.get_id()) * 2654435761u + 12345u) {
        W = g.width;
        H = g.height;
        N = W * H;
        K.reset();
        lastSeen.assign(N, -1);
        nextSpawn.assign(N, -1);
        pearlSeen.assign(N, 0);
        visitRound.assign(N, 0);
        visitHeat.assign(N, 0.0);
        inView.assign(N, 0);
        pearlNow.assign(N, 0);
        occId.assign(N, -1);
        occTeam.assign(N, 0);
        occHead.assign(N, 0);
        occDir.assign(N, 0);
        releaseAt.assign(N, INF);
        nbr.assign(N * 4, 0);
        distE.assign(N, INF);
        distA.assign(N, INF);
        ownerA.assign(N, -1);
        block.assign(N, 0);
        staticBlock.assign(N, 0);
        dist.assign(N, INF);
        seen.assign(N, 0);
        ownStamp.assign(N, 0);
        queue.assign(N, 0);
        powBest.resize(N + 2);
        powSum.resize(N + 2);
        for (int k = 0; k <= N + 1; k++) {
            powBest[k] = std::pow(params::GAMMA_BEST, k);
            powSum[k] = std::pow(params::GAMMA_SUM, k);
        }
        decayPow.resize(Constants::MAX_ROUNDS + 2);
        heatDecay.resize(Constants::MAX_ROUNDS + 2);
        for (int k = 0; k <= Constants::MAX_ROUNDS + 1; k++) {
            decayPow[k] = std::pow(params::REMEMBERED_DECAY, k);
            heatDecay[k] = std::pow(params::REVISIT_DECAY, k);
        }
    }

    // -- knowledge -----------------------------------------------------------

    bool mapMatchesView(MapInfo const& m) const {
        for (auto const& tile : ct.get_tiles()) {
            int const t = idxOf(tile.get_position());
            for (int d = 0; d < 4; d++) {
                bool horizontal;
                int e;
                edgeAt(t, d, horizontal, e);
                int8_t const want = horizontal ? m.hKind[e] : m.vKind[e];
                if (want != kindOf(tile.get_edge(Direction(DCH[d]))))
                    return false;
            }
            bool const never = tile.get_pearl_time() < 0;
            if (never != (m.maxGap[t] == 0))
                return false;
        }
        return true;
    }

    void identifyMap() {
        identified = true;
        int matches = 0;
        for (int i = 0; i < maps_data::MAP_COUNT; i++) {
            int w = 0, h = 0;
            if (std::sscanf(maps_data::MAPS[i], "MAP %d %d", &w, &h) != 2 || w != W || h != H)
                continue;
            MapInfo m;
            if (!parseMap(maps_data::MAPS[i], m) || !mapMatchesView(m))
                continue;
            if (matches++ == 0) {
                K.loadMap(m);
                if (game.get_round_num() == 0)
                    adoptSpawnBody(m);
            }
        }
        ct.output_log("map", matches > 0 ? "known" : "unknown", matches);
    }

    void adoptSpawnBody(MapInfo const& m) {
        int const head = idxOf(ct.get_position());
        int const team = myTeam == 'A' ? 0 : 1;
        for (auto const& s : m.spawns) {
            if (s.team == team && !s.body.empty() && s.body.front() == head &&
                static_cast<int>(s.body.size()) == ct.get_length()) {
                body.assign(s.body.begin(), s.body.end());
                haveBody = true;
                plannedPath.clear();
                return;
            }
        }
    }

    void learnEdge(int t, int d, Edge const& edge) {
        bool horizontal;
        int e;
        edgeAt(t, d, horizontal, e);
        int8_t const kind = kindOf(edge);
        int8_t& slot = horizontal ? K.hKind[e] : K.vKind[e];
        int& pid = horizontal ? K.hPid[e] : K.vPid[e];
        if (slot == kind && (kind != EK_PORTAL || pid == edge.get_portal_id()))
            return;
        // What we see always wins over what we assumed.
        slot = kind;
        pid = edge.get_portal_id();
        if (kind == EK_PORTAL)
            K.addPortalEnd(pid, horizontal, e);
    }

    void absorbView() {
        std::fill(inView.begin(), inView.end(), 0);
        std::fill(pearlNow.begin(), pearlNow.end(), 0);
        std::fill(occId.begin(), occId.end(), -1);
        std::fill(occHead.begin(), occHead.end(), 0);
        enemyHeads.clear();
        allyHeads.clear();

        for (auto const& tile : ct.get_tiles()) {
            int const t = idxOf(tile.get_position());
            inView[t] = 1;
            pearlNow[t] = tile.has_pearl() ? 1 : 0;
            lastSeen[t] = round;
            pearlSeen[t] = pearlNow[t];
            int const pt = tile.get_pearl_time();
            if (pt < 0) {
                K.maxGap[t] = 0;
                nextSpawn[t] = -1;
            } else {
                nextSpawn[t] = round + pt;
                if (K.maxGap[t] == 0)
                    K.maxGap[t] = -1; // we were wrong: it does spawn
            }
            for (int d = 0; d < 4; d++)
                learnEdge(t, d, tile.get_edge(Direction(DCH[d])));
            if (auto const* part = tile.get_dragon()) {
                occId[t] = part->get_id();
                occTeam[t] = part->get_team().value;
                occHead[t] = part->is_head() ? 1 : 0;
                occDir[t] = static_cast<int8_t>(dirIndex(part->get_dir()));
                if (part->is_head() && part->get_id() != myId) {
                    if (part->get_team().value == myTeam)
                        allyHeads.push_back({part->get_id(), t});
                    else
                        enemyHeads.push_back({part->get_id(), t});
                }
            }
        }
        // Mirrored tiles share a countdown.
        if (K.sym) {
            for (auto const& tile : ct.get_tiles()) {
                int const t = idxOf(tile.get_position());
                int const m = mirrorOf(t);
                if (m < 0 || inView[m])
                    continue;
                if (tile.get_pearl_time() < 0)
                    K.maxGap[m] = 0;
                else if (lastSeen[m] < 0 || nextSpawn[m] < round)
                    nextSpawn[m] = nextSpawn[t];
            }
        }
        for (int t = 0; t < N; t++)
            for (int d = 0; d < 4; d++)
                nbr[t * 4 + d] = computeStep(t, d);
        computeOthersRelease();
    }

    // For other dragons whose tail we can see, when each of their tiles frees up. A dragon
    // moves once between each of our moves, so the segment j from its tail frees for our
    // move j+2, just like our own body. Segments of dragons whose tail is out of view stay INF.
    void computeOthersRelease() {
        std::fill(releaseAt.begin(), releaseAt.end(), INF);
        auto const towardHead = [&](int t) { return occHead[t] ? -1 : nbr[t * 4 + occDir[t]]; };
        std::vector<int> parts;
        for (auto const& tile : ct.get_tiles()) {
            int const t = idxOf(tile.get_position());
            if (occId[t] >= 0 && occId[t] != myId)
                parts.push_back(t);
        }
        std::vector<uint8_t> hasPrev(parts.size(), 0);
        for (int t : parts) {
            int const n = towardHead(t);
            if (n >= 0 && occId[n] == occId[t])
                for (std::size_t i = 0; i < parts.size(); i++)
                    if (parts[i] == n)
                        hasPrev[i] = 1;
        }
        for (std::size_t i = 0; i < parts.size(); i++) {
            int const rear = parts[i];
            if (hasPrev[i])
                continue;
            // It is the tail only if every tile a predecessor could stand on is in view.
            bool verified = true;
            for (int d = 0; d < 4 && verified; d++) {
                int const n = nbr[rear * 4 + d];
                if (n == STEP_KELP)
                    continue;
                if (n < 0 || !inView[n])
                    verified = false;
            }
            if (!verified)
                continue;
            int const id = occId[rear];
            int cur = rear;
            for (int j = 0; cur >= 0 && occId[cur] == id && j < N; j++) {
                releaseAt[cur] = j + 2;
                cur = towardHead(cur);
            }
        }
    }

    // -- own body ------------------------------------------------------------

    void rebuildBodyFromView() {
        int const head = idxOf(ct.get_position());
        struct Part { int tile; int dir; bool used; };
        std::vector<Part> parts;
        for (auto const& tile : ct.get_tiles()) {
            auto const* part = tile.get_dragon();
            if (part && part->get_id() == myId && !part->is_head())
                parts.push_back({idxOf(tile.get_position()), dirIndex(part->get_dir()), false});
        }
        body.clear();
        body.push_back(head);
        int cur = head;
        for (bool grew = true; grew && static_cast<int>(body.size()) < ct.get_length();) {
            grew = false;
            for (auto& p : parts) {
                if (!p.used && nbr[p.tile * 4 + p.dir] == cur) {
                    p.used = true;
                    body.push_back(p.tile);
                    cur = p.tile;
                    grew = true;
                    break;
                }
            }
        }
        haveBody = true;
    }

    void updateBody() {
        int const head = idxOf(ct.get_position());
        bool ok = false;
        if (haveBody) {
            for (int t : plannedPath)
                body.push_front(t);
            ok = !body.empty() && body.front() == head;
        }
        if (!ok)
            rebuildBodyFromView();
        while (static_cast<int>(body.size()) > ct.get_length())
            body.pop_back();
        plannedPath.clear();
    }

    // -- evaluation ----------------------------------------------------------

    void bfsFrom(std::vector<Head> const& sources, std::vector<int>& out, std::vector<int>* owner) {
        std::fill(out.begin(), out.end(), INF);
        if (owner)
            std::fill(owner->begin(), owner->end(), -1);
        int qh = 0, qt = 0;
        for (auto const& s : sources) {
            if (out[s.tile] != INF)
                continue;
            out[s.tile] = 0;
            if (owner)
                (*owner)[s.tile] = s.id;
            queue[qt++] = s.tile;
        }
        while (qh < qt) {
            int const t = queue[qh++];
            for (int d = 0; d < 4; d++) {
                int const n = nbr[t * 4 + d];
                if (n < 0 || out[n] != INF)
                    continue;
                out[n] = out[t] + 1;
                if (owner)
                    (*owner)[n] = (*owner)[t];
                queue[qt++] = n;
            }
        }
    }

    // Belief that a pearl is on tile t when we arrive at round `arrival`.
    double pearlBelief(int t, int arrival) const {
        int const maxGap = K.maxGap[t];
        if (maxGap == 0)
            return 0.0;
        if (inView[t]) {
            if (pearlNow[t])
                return 1.0;
            if (nextSpawn[t] >= 0 && arrival >= nextSpawn[t])
                return 0.9;
            return 0.0;
        }
        if (lastSeen[t] >= 0) {
            int const age = round - lastSeen[t];
            if (pearlSeen[t])
                return decayPow[std::min(age, Constants::MAX_ROUNDS)];
            if (nextSpawn[t] >= 0 && arrival >= nextSpawn[t])
                return 0.85 * decayPow[std::clamp(round - nextSpawn[t], 0, Constants::MAX_ROUNDS)];
            return 0.0;
        }
        // Never seen: the initial countdown is uniform on [minGap, maxGap].
        double prior;
        if (maxGap > 0) {
            int const lo = std::max<int>(1, K.minGap[t]);
            prior = std::clamp((arrival - lo + 1.0) / (maxGap - lo + 1.0), 0.0, 1.0);
        } else {
            prior = std::min(1.0, static_cast<double>(arrival) / params::UNKNOWN_MAP_GAP);
        }
        double v = params::UNSEEN_PRIOR * prior;
        if (nextSpawn[t] >= 0 && arrival >= nextSpawn[t])
            v = std::max(v, 0.7);
        return v;
    }

    struct Candidate {
        int dir;
        int dest;
        bool fatal = false;
        bool headTrade = false; // moving onto an enemy head: both die
        bool allyHead = false;  // moving onto an ally's head: we would kill it too
        bool eats = false;
        int space = 0;
        bool safe = false; // can reach its own freed body (tail-follow) or has lots of room
        int newLength = 0;
        double score = -1e18;
    };

    void evaluate(Candidate& c, int length) {
        int const head = body.front();
        int const dest = nbr[head * 4 + c.dir];
        c.dest = dest;
        if (dest == STEP_KELP) {
            c.fatal = true;
            return;
        }
        if (dest == STEP_UNKNOWN_PORTAL) {
            c.score = -params::UNKNOWN_PORTAL - params::TRAPPED / 2;
            return;
        }
        if (inView[dest] && occId[dest] >= 0) {
            c.fatal = true;
            c.headTrade = occHead[dest] && occTeam[dest] != myTeam;
            c.allyHead = occHead[dest] && occTeam[dest] == myTeam;
            return;
        }
        for (int t : body) {
            if (t == dest) {
                c.fatal = true;
                return;
            }
        }

        double score = 0.0;
        c.eats = inView[dest] && pearlNow[dest];
        if (c.eats)
            score += params::EAT_NOW;
        if (!inView[dest])
            score -= params::OUT_OF_VIEW;
        score -= params::REVISIT * heat(dest);

        // Our body after the move, and when each of its tiles frees up.
        int const newLength = c.eats ? length + 1 : length;
        c.newLength = newLength;
        int const keep = std::min<int>(static_cast<int>(body.size()), newLength - 1);
        // Index from the tail j frees for move j+2 (collision is checked before the tail moves).
        ++ownScenario;
        for (int i = 0; i < keep; i++) {
            raiseBlock(body[i], newLength - 1 - (i + 1) + 2);
            ownStamp[body[i]] = ownScenario;
        }
        raiseBlock(dest, INF);
        bool reachedOwn = false;

        // Time-aware BFS from dest, scoring pearls as we go.
        newSearch();
        int qh = 0, qt = 0;
        visit(dest, 0);
        queue[qt++] = dest;
        double best = 0.0, sum = 0.0;
        while (qh < qt) {
            int const t = queue[qh++];
            int const k = dist[t];
            if (t != dest && !(inView[t] && occId[t] >= 0)) {
                double v = pearlBelief(t, round + 1 + k);
                if (v > 0.0) {
                    int const moves = 1 + k;
                    if (distE[t] < moves)
                        v *= params::ENEMY_CLOSER;
                    if (distA[t] < moves || (distA[t] == moves && ownerA[t] < myId))
                        v *= params::ALLY_CLOSER;
                    best = std::max(best, v * powBest[k]);
                    sum += v * powSum[k];
                }
            }
            for (int d = 0; d < 4; d++) {
                int const n = nbr[t * 4 + d];
                if (n < 0 || seen[n] == stamp || block[n] > k + 1)
                    continue;
                if (ownStamp[n] == ownScenario)
                    reachedOwn = true;
                visit(n, k + 1);
                queue[qt++] = n;
            }
        }
        restoreBlock();
        c.space = qt;
        c.safe = reachedOwn || qt >= roomyEnough(newLength);
        score += params::FOOD_BEST * best + params::FOOD_SUM * sum;

        if (!c.safe)
            score -= params::TRAPPED - 50.0 * std::min(c.space, newLength);
        else
            score += 0.5 * std::min(c.space, 3 * newLength);

        for (auto const& e : enemyHeads) {
            for (int d = 0; d < 4; d++) {
                if (nbr[e.tile * 4 + d] == dest) {
                    score -= params::HEAD_ADJ_BASE + params::HEAD_ADJ_PER_LEN * length;
                    d = 4;
                }
            }
        }
        score += std::uniform_real_distribution<double>(0.0, 1.0)(rng);
        c.score = score;
    }

    // `block` always holds staticBlock between searches; searches raise tiles and restore them.
    void raiseBlock(int t, int v) {
        blockSaved.emplace_back(t, block[t]);
        block[t] = std::max(block[t], v);
    }
    void restoreBlock() {
        for (auto it = blockSaved.rbegin(); it != blockSaved.rend(); ++it)
            block[it->first] = it->second;
        blockSaved.clear();
    }
    void newSearch() { ++stamp; }
    void visit(int t, int k) {
        seen[t] = stamp;
        dist[t] = k;
    }

    static int roomyEnough(int length) { return 2 * length + 4; }

    // Whether a dragon of `length` with its head on `start` can survive: it reaches its own
    // body after that tile frees (so it can follow its tail forever), or finds lots of room.
    bool floodSafe(int start, int length) {
        newSearch();
        int const enough = roomyEnough(length);
        int qh = 0, qt = 0;
        visit(start, 0);
        queue[qt++] = start;
        while (qh < qt) {
            int const t = queue[qh++];
            int const k = dist[t];
            for (int d = 0; d < 4; d++) {
                int const n = nbr[t * 4 + d];
                if (n < 0 || seen[n] == stamp || block[n] > k + 1)
                    continue;
                if (ownStamp[n] == ownScenario)
                    return true;
                visit(n, k + 1);
                if (++qt >= enough)
                    return true;
                queue[qt - 1] = n;
            }
        }
        return false;
    }

    // Surviving length if we split off the rear `child` segments now. The parent stays
    // put and moves next round; the child is the rear reversed, head at our old tail.
    int splitValue(int child) {
        int const length = static_cast<int>(body.size());
        int const parentLen = length - child;
        // Child's view: its own body frees from its tail (our body[parentLen]) outward,
        // the parent is treated as frozen for one extra round.
        ++ownScenario;
        for (int i = 0; i < parentLen; i++)
            raiseBlock(body[i], (parentLen - 1 - i) + 3);
        for (int i = parentLen; i < length; i++) {
            raiseBlock(body[i], (i - parentLen) + 2);
            ownStamp[body[i]] = ownScenario;
        }
        raiseBlock(body[length - 1], INF);
        bool const childSafe = floodSafe(body[length - 1], child);
        restoreBlock();

        ++ownScenario;
        for (int i = 0; i < parentLen; i++) {
            raiseBlock(body[i], (parentLen - 1 - i) + 2);
            ownStamp[body[i]] = ownScenario;
        }
        for (int i = parentLen; i < length; i++)
            raiseBlock(body[i], (i - parentLen) + 2);
        raiseBlock(body[0], INF);
        bool const parentSafe = floodSafe(body[0], parentLen);
        restoreBlock();

        return (childSafe ? child : 0) + (parentSafe ? parentLen : 0);
    }

    double heat(int t) const {
        int const age = std::clamp(round - visitRound[t], 0, Constants::MAX_ROUNDS);
        return visitHeat[t] * heatDecay[age];
    }
    void markVisited(int t) {
        visitHeat[t] = heat(t) + 1.0;
        visitRound[t] = round;
    }

    bool enemyNear(int tile, int radius) const {
        for (auto const& e : enemyHeads) {
            int dx = std::abs(e.tile % W - tile % W), dy = std::abs(e.tile / W - tile / W);
            dx = std::min(dx, W - dx);
            dy = std::min(dy, H - dy);
            if (dx + dy <= radius)
                return true;
        }
        return false;
    }

    void move(int d) {
        ct.make_move(Direction(DCH[d]));
        int const dest = nbr[body.front() * 4 + d];
        plannedPath.clear();
        if (dest >= 0)
            plannedPath.push_back(dest);
    }

    void split(int child) {
        ct.do_split(child);
        plannedPath.clear(); // the parent stays put
    }

    void turn() {
        round = game.get_round_num();
        absorbView();
        if (!identified)
            identifyMap();
        updateBody();
        markVisited(body.front());

        int const length = ct.get_length();
        bfsFrom(enemyHeads, distE, nullptr);
        std::sort(allyHeads.begin(), allyHeads.end(), [](Head const& a, Head const& b) { return a.id < b.id; });
        bfsFrom(allyHeads, distA, &ownerA);

        std::fill(staticBlock.begin(), staticBlock.end(), 0);
        for (int t = 0; t < N; t++)
            if (inView[t] && occId[t] >= 0 && occId[t] != myId)
                staticBlock[t] = releaseAt[t];
        block = staticBlock;
        blockSaved.clear();

        Candidate cands[4];
        for (int d = 0; d < 4; d++) {
            cands[d].dir = d;
            evaluate(cands[d], length);
        }
        Candidate const* best = nullptr;
        for (auto const& c : cands)
            if (!c.fatal && (!best || c.score > best->score))
                best = &c;
        if (params::DEBUG) {
            std::string s;
            for (auto const& c : cands) {
                s += DCH[c.dir];
                s += c.fatal ? "=X" : "=" + std::to_string(static_cast<long>(c.score)) + "/" + std::to_string(c.space);
                s += " ";
            }
            ct.output_log("R", round, "at", body.front() % W, body.front() / W, "len", length, "body", body.size(),
                          "known", K.fromMap, "|", s);
        }

        bool const canSplit = ct.can_split(length / 2);
        int const head = body.front();

        // Trapped (or worse): compare moving on with every split, by length expected to survive.
        bool const bodyKnown = static_cast<int>(body.size()) == length;
        if ((!best || !best->safe) && canSplit && bodyKnown) {
            // An unsafe move buys time, which is worth a little in case others move away.
            double moveValue = 0.0;
            if (best)
                moveValue = 0.25 * best->newLength * std::min(1.0, static_cast<double>(best->space) / best->newLength);
            int bestChild = 0;
            double bestValue = moveValue;
            std::vector<int> sizes{2, 3, length / 4, length / 3, length / 2, 2 * length / 3, 3 * length / 4,
                                   length - 3, length - 2};
            std::sort(sizes.begin(), sizes.end());
            sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
            for (int child : sizes) {
                if (child < 2 || child > length - 2)
                    continue;
                double const v = splitValue(child);
                if (v > bestValue) {
                    bestValue = v;
                    bestChild = child;
                }
            }
            if (bestChild > 0) {
                split(bestChild);
                ct.set_indicator_string("escape split " + std::to_string(bestChild));
                return;
            }
        }
        if (!best) {
            // Every move kills us. Take an enemy with us if we can.
            for (auto const& c : cands) {
                if (c.headTrade) {
                    move(c.dir);
                    ct.set_indicator_string("trade");
                    return;
                }
            }
            // Otherwise die alone: never onto an ally's head.
            int choice = dirIndex(ct.get_dir());
            for (auto const& c : cands) {
                if (!c.allyHead) {
                    choice = c.dir;
                    break;
                }
            }
            move(choice);
            ct.set_indicator_string("doomed");
            return;
        }

        // Insurance split: a lone dragon dying loses the game, whatever its length.
        if (ct.get_unit_count() < params::MIN_UNITS && length >= params::INSURANCE_MIN_LEN &&
            ct.can_split(params::INSURANCE_CHILD) && bodyKnown && best->safe && !enemyNear(head, 3) &&
            splitValue(params::INSURANCE_CHILD) == length) {
            split(params::INSURANCE_CHILD);
            ct.set_indicator_string("insurance split");
            return;
        }

        // Voluntary split: more collectors and insurance against elimination.
        if (params::SPLIT_MIN_LEN > 0 && length >= params::SPLIT_MIN_LEN && round <= params::SPLIT_LAST_ROUND &&
            canSplit && !enemyNear(head, 3) && best->safe) {
            split(length / 2);
            ct.set_indicator_string("split");
            return;
        }

        move(best->dir);
    }

    // Used if turn() throws: step anywhere the view says is clear.
    void emergency() {
        auto const here = ct.get_position();
        auto const* hereTile = ct.get_tile(here);
        for (auto const direction : Direction::get_direction_list()) {
            if (hereTile && !hereTile->get_edge(direction).is_passable())
                continue;
            auto const* ahead = ct.get_tile(here.add_dir(direction));
            if (ahead && ahead->get_dragon())
                continue;
            ct.make_move(direction);
            haveBody = false;
            plannedPath.clear();
            return;
        }
        ct.make_move(ct.get_dir());
        haveBody = false;
    }
};

} // namespace

int main() {
    auto [ct, game] = unswbc::init();
    Brain brain(ct, game);
    while (true) {
        bool alive;
        try {
            alive = unswbc::update(ct, game);
        } catch (...) {
            return 0; // stdin closed: the game is over
        }
        if (!alive)
            break;
        try {
            brain.turn();
        } catch (...) {
            brain.emergency();
        }
        unswbc::end_turn();
    }
    return 0;
}
