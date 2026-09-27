// AllRed v2: a swarm that consolidates.
//
// Each turn: fold the 7x7 view into long-lived knowledge (edges, pearl timers, the
// embedded map if we recognised it), then decide, in order:
//   1. strike: a head trade worth taking, by a move or a sprint (both dragons die);
//   2. survive: when every move traps us, split so the most length lives on;
//   3. feed: late in the game a small dragon dies next to a much longer ally, whose
//      pearl-seeking then eats what it drops, so we finish with one long dragon;
//   4. swarm: early on, split in half whenever long enough, up to the dragon cap;
//   5. otherwise the best-scoring move: pearls reachable (a belief over every tile,
//      discounted by distance and by who gets there first), room to keep moving
//      (a timed flood fill that knows when every visible body frees up), and the
//      head trades enemies could force on us next turn.

#include "board.hpp"
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
using namespace bc;

namespace {

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
    std::vector<uint8_t> heard; // our pearl knowledge of the tile came over sonar, not our eyes

    // This turn's view.
    std::vector<uint8_t> inView, pearlNow;
    std::vector<int> occId; // dragon id on the tile, -1 if none
    std::vector<char> occTeam;
    std::vector<uint8_t> occHead;
    std::vector<int8_t> occDir;
    std::vector<int> releaseAt; // other dragons' tiles: our move number at which the tile frees (INF unknown)
    std::vector<int> nbr; // nbr[t*4+d]

    // Other dragons in view this turn.
    struct Seen {
        int id;
        char team;
        int head = -1;     // head tile, if in view
        int visible = 0;   // segments in view
        int knownLen = -1; // exact length, when both ends are in view
        int len() const { return knownLen > 0 ? knownLen : std::max(2, visible); }
    };
    std::vector<Seen> dragons;
    struct Head { int id; int tile; int len; };
    std::vector<Head> enemyHeads, allyHeads;
    std::vector<int> threatLen; // shortest enemy able to put its head on this tile next turn (INF: none)

    // Consolidation: the ally we would feed, if any.
    int kingTile = -1, kingLen = 0;
    bool kingVisible = false; // kingTile is its head this turn (else: where we last heard it was)
    std::vector<int> kingDist;

    // Sonar: the best king we have seen or heard of, passed from dragon to dragon.
    struct KingNews { int id = -1; int tile = -1; int len = 0; int round = -1; };
    KingNews news;
    bool amKing = false;

    // Hunting: enemy heads we saw recently (a big dragon stays worth chasing for a while).
    struct Sighting { int id; int tile; int len; int round; };
    std::vector<Sighting> sightings;
    std::vector<int> preyDist; // steps to the nearest enemy head worth trading with
    std::vector<int> othersDist; // moves any other dragon's head needs to reach a tile (bodies block)
    bool aggressive = false;     // we clearly outnumber the enemies around us
    int unitsNow = 1;            // our team's living dragons this turn
    double preyGain = 0.0;     // what the best such trade is worth

    // Our own body, head first. Tracked across turns so we know it beyond the window.
    std::deque<int> body;
    std::vector<int> plannedPath;
    bool haveBody = false;

    // Scratch.
    std::vector<int> distE, distA, ownerA, block, dist, queue, staticBlock, seen, from, fromDir;
    std::vector<std::pair<int, int>> blockSaved;
    int stamp = 0;
    std::vector<uint8_t> owned; // during evaluate's search: tile is in our Voronoi room
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
        heard.assign(N, 0);
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
        from.assign(N, -1);
        fromDir.assign(N, 0);
        threatLen.assign(N, INF);
        kingDist.assign(N, INF);
        preyDist.assign(N, INF);
        othersDist.assign(N, INF);
        ownStamp.assign(N, 0);
        owned.assign(N, 0);
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
        dragons.clear();
        enemyHeads.clear();
        allyHeads.clear();

        for (auto const& tile : ct.get_tiles()) {
            int const t = idxOf(tile.get_position());
            inView[t] = 1;
            pearlNow[t] = tile.has_pearl() ? 1 : 0;
            lastSeen[t] = round;
            pearlSeen[t] = pearlNow[t];
            heard[t] = 0;
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
                if (part->get_id() != myId) {
                    Seen& d = dragonEntry(part->get_id(), part->get_team().value);
                    d.visible++;
                    if (part->is_head())
                        d.head = t;
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
        for (auto const& d : dragons)
            if (d.head >= 0)
                (d.team == myTeam ? allyHeads : enemyHeads).push_back({d.id, d.head, d.len()});
    }

    Seen& dragonEntry(int id, char team) {
        for (auto& d : dragons)
            if (d.id == id)
                return d;
        dragons.push_back({id, team});
        return dragons.back();
    }

    int lengthOf(int id) const {
        for (auto const& d : dragons)
            if (d.id == id)
                return d.len();
        return 2;
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
                if (occHead[cur]) {
                    // Tail to head all in view: we know its exact length.
                    for (auto& d : dragons)
                        if (d.id == id)
                            d.knownLen = j + 1;
                    break;
                }
                cur = towardHead(cur);
            }
        }
    }

    // -- own body ------------------------------------------------------------

    bool bodyFromHandoff() {
        if (!params::HANDOFF)
            return false;
        std::vector<int> dirs;
        for (std::uint64_t m : ct.get_sonar_messages()) {
            if (!decodeBody(m, dirs) || static_cast<int>(dirs.size()) + 1 > ct.get_length())
                continue;
            std::deque<int> built{idxOf(ct.get_position())};
            bool ok = true;
            for (int d : dirs) {
                int const next = nbr[built.back() * 4 + d];
                if (next < 0 || (inView[next] && occId[next] != myId)) {
                    ok = false;
                    break;
                }
                built.push_back(next);
            }
            if (ok) {
                body = std::move(built);
                haveBody = true;
                return true;
            }
        }
        return false;
    }

    void rebuildBodyFromView() {
        if (!haveBody && bodyFromHandoff())
            return;
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

    void bfsFrom(std::vector<Head> const& sources, std::vector<int>& out, std::vector<int>* owner,
                 bool throughBodies = true) {
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
                if (n < 0 || out[n] != INF || (!throughBodies && inView[n] && occId[n] >= 0))
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
            // Early for a known spawn: worth hanging around for, discounted by the wait.
            if (nextSpawn[t] >= 0 && params::FUTURE_WAIT_GAMMA > 0.0)
                return 0.9 * std::pow(params::FUTURE_WAIT_GAMMA, nextSpawn[t] - arrival);
            return 0.0;
        }
        if (lastSeen[t] >= 0) {
            int const age = round - lastSeen[t];
            if (pearlSeen[t])
                return decayPow[std::min(age, Constants::MAX_ROUNDS)] * (heard[t] ? params::HEARD_VALUE : 1.0);
            if (nextSpawn[t] >= 0 && arrival >= nextSpawn[t])
                return 0.85 * decayPow[std::clamp(round - nextSpawn[t], 0, Constants::MAX_ROUNDS)];
            if (nextSpawn[t] >= 0 && params::FUTURE_WAIT_GAMMA > 0.0)
                return 0.85 * std::pow(params::FUTURE_WAIT_GAMMA, nextSpawn[t] - arrival);
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
        bool dive = false; // unsafe, but into a pocket with pearls we can split our way out of
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

        // Time-aware BFS from dest, scoring pearls as we go. Every reachable tile counts for
        // pearls and for reaching our own tail; only tiles we reach no later than any other
        // head (and through such tiles) count as our room.
        newSearch();
        int qh = 0, qt = 0, room = 1;
        visit(dest, 0);
        owned[dest] = 1;
        queue[qt++] = dest;
        double best = 0.0, sum = 0.0, pocket = 0.0;
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
                    pocket += v;
                }
            }
            for (int d = 0; d < 4; d++) {
                int const n = nbr[t * 4 + d];
                if (n < 0 || seen[n] == stamp || block[n] > k + 1)
                    continue;
                if (ownStamp[n] == ownScenario)
                    reachedOwn = true;
                visit(n, k + 1);
                // We are on move, so ties with another head are ours.
                owned[n] = owned[t] && (!params::VORONOI || othersDist[n] >= k + 2);
                room += owned[n];
                queue[qt++] = n;
            }
        }
        restoreBlock();
        c.space = room;
        c.safe = reachedOwn || room >= roomyEnough(newLength);
        score += params::FOOD_BEST * best + params::FOOD_SUM * sum;

        // A small dragon may dive into a dead end for its pearls: once it is at least 4 long it
        // splits its way out (the child's head starts at the tail, facing the exit), losing only
        // the stuck parent. Bigger dragons and empty pockets stay off limits.
        c.dive = !c.safe && newLength <= params::DIVE_MAX_LEN && pocket >= params::DIVE_MIN_POCKET &&
                 newLength + pocket >= 4.0;
        if (c.dive)
            score -= params::DIVE_PENALTY;
        else if (!c.safe)
            score -= params::TRAPPED - 50.0 * std::min(c.space, newLength);
        else
            score += 0.5 * std::min(c.space, 3 * newLength);
        // A long dragon (our king, usually) dies or has to split when boxed in: keep it in the open.
        if (newLength >= params::KING_ROOM_LEN)
            score += params::KING_ROOM * std::min(c.space, params::KING_ROOM_TILES_PER_LEN * newLength);

        // An enemy that can reach dest next turn may trade heads with us.
        if (threatLen[dest] < INF) {
            int const disadvantage = newLength - threatLen[dest];
            double penalty = disadvantage > 0 ? params::THREAT_PER_LEN * disadvantage : params::THREAT_EVEN;
            if (unitsNow <= params::LOW_UNITS)
                penalty = std::max(penalty, params::THREAT_LOW_UNITS);
            score -= penalty;
        }
        if (kingTile >= 0 && kingDist[dest] < INF)
            score += params::FEED_PULL_LINEAR
                         ? params::FEED_ATTRACT - params::FEED_PULL_PER_STEP * std::min(kingDist[dest], 60)
                         : params::FEED_ATTRACT / (1.0 + kingDist[dest]);
        if (preyGain > 0.0 && preyDist[dest] < INF)
            score += (aggressive ? params::HUNT_ATTRACT_AGGRO : params::HUNT_ATTRACT) *
                     std::min(preyGain, params::HUNT_GAIN_CAP) / (1.0 + preyDist[dest]);
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

        // A stuck child of 4+ can split again on its own first turn: that grandchild is the
        // child's rear, head at the child's tail (our body[parentLen]), so a chain of splits
        // lets a piece out from the middle of a body whose both ends sit in dead ends.
        int chained = 0;
        if (!childSafe && child >= 4) {
            int const grand = child - 2;
            ++ownScenario;
            for (int i = 0; i < length; i++) {
                bool const inGrand = i >= parentLen && i < parentLen + grand;
                // The grandchild frees from its tail (body[parentLen + grand - 1]); the rest waits.
                int const release = inGrand ? (parentLen + grand - 1 - i) + 2 : length + 3;
                raiseBlock(body[i], release);
                if (inGrand)
                    ownStamp[body[i]] = ownScenario;
            }
            raiseBlock(body[parentLen], INF);
            if (floodSafe(body[parentLen], grand))
                chained = grand;
            restoreBlock();
        }

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

        return (childSafe ? child : chained) + (parentSafe ? parentLen : 0);
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

    // For every tile, the shortest enemy that could put its head there before our next turn
    // (a length-L dragon can sprint L-1 steps, through tiles that are free now).
    void computeThreats() {
        std::fill(threatLen.begin(), threatLen.end(), INF);
        for (auto const& e : enemyHeads) {
            int const reach = std::clamp(e.len - 1, 1, params::STRIKE_MAX_STEPS);
            newSearch();
            int qh = 0, qt = 0;
            visit(e.tile, 0);
            queue[qt++] = e.tile;
            while (qh < qt) {
                int const t = queue[qh++];
                int const k = dist[t];
                if (k >= reach)
                    continue;
                for (int d = 0; d < 4; d++) {
                    int const n = nbr[t * 4 + d];
                    if (n < 0 || seen[n] == stamp || (inView[n] && occId[n] >= 0))
                        continue;
                    visit(n, k + 1);
                    queue[qt++] = n;
                    threatLen[n] = std::min(threatLen[n], e.len);
                }
            }
        }
    }

    // What a head trade is worth to us: positive means take it.
    double tradeGain(int enemyLen, int myLen, int units) const {
        if (units <= 1)
            return -1.0; // our last dragon: trading it loses the game
        int const gain = enemyLen - myLen;
        if (units <= params::LOW_UNITS)
            return gain >= params::LOW_UNITS_TRADE_GAIN ? gain : -1.0; // elimination is close
        if (gain > 0)
            return gain;
        // Even trades thin both swarms; worth it while we have dragons to spare, or when we
        // outnumber the enemies around us (their losses hurt them more).
        if (gain == 0 && (aggressive || (round < params::CONSOLIDATE_ROUND && units >= params::TRADE_MIN_UNITS)))
            return aggressive ? 1.0 : 0.5;
        return -1.0;
    }

    // Reach an enemy head this turn, by one move or a sprint through free tiles in view.
    bool tryStrike(int length, int units) {
        int const maxSteps = std::min(length - 1, params::STRIKE_MAX_STEPS);
        if (enemyHeads.empty() || maxSteps < 1)
            return false;
        int const head = body.front();
        int target = -1, lastFrom = -1, lastDir = -1, steps = INF;
        double bestGain = 0.0;
        newSearch();
        int qh = 0, qt = 0;
        visit(head, 0);
        queue[qt++] = head;
        while (qh < qt) {
            int const t = queue[qh++];
            int const k = dist[t];
            if (k >= maxSteps)
                continue;
            for (int d = 0; d < 4; d++) {
                int const n = nbr[t * 4 + d];
                if (n < 0 || !inView[n])
                    continue;
                if (occId[n] >= 0) {
                    if (occHead[n] && occTeam[n] != myTeam) {
                        double const g = tradeGain(lengthOf(occId[n]), length, units);
                        if (g > 0.0 && (g > bestGain || (g == bestGain && k + 1 < steps))) {
                            target = n;
                            lastFrom = t;
                            lastDir = d;
                            steps = k + 1;
                            bestGain = g;
                        }
                    }
                    continue;
                }
                if (seen[n] == stamp)
                    continue;
                visit(n, k + 1);
                from[n] = t;
                fromDir[n] = d;
                queue[qt++] = n;
            }
        }
        if (target < 0)
            return false;
        std::vector<Direction> dirs{Direction(DCH[lastDir])};
        for (int cur = lastFrom; cur != head; cur = from[cur])
            dirs.push_back(Direction(DCH[fromDir[cur]]));
        std::reverse(dirs.begin(), dirs.end());
        ct.make_moves(dirs);
        plannedPath.clear();
        ct.set_indicator_string("strike " + std::to_string(steps));
        return true;
    }

    // Remember enemy heads, and aim at those worth a head trade (seen now or very recently).
    void choosePrey(int length, int units) {
        for (auto const& e : enemyHeads) {
            bool updated = false;
            for (auto& sg : sightings) {
                if (sg.id == e.id) {
                    sg = {e.id, e.tile, e.len, round};
                    updated = true;
                }
            }
            if (!updated)
                sightings.push_back({e.id, e.tile, e.len, round});
        }
        std::erase_if(sightings, [&](Sighting const& sg) {
            // Forget what is stale, and what we can see is no longer there.
            return round - sg.round > params::HUNT_MEMORY || (sg.round < round && inView[sg.tile]);
        });
        preyGain = 0.0;
        std::vector<Head> prey;
        for (auto const& sg : sightings) {
            double const g = tradeGain(sg.len, length, units);
            if (g <= 0.0)
                continue;
            prey.push_back({sg.id, sg.tile, sg.len});
            preyGain = std::max(preyGain, g);
        }
        if (prey.empty())
            std::fill(preyDist.begin(), preyDist.end(), INF);
        else
            bfsFrom(prey, preyDist, nullptr);
    }

    // -- sonar ---------------------------------------------------------------
    // A king beacon is 64 bits: [check 16][type 3][x 7][y 7][len 10][round 9][id 12]. The check
    // is keyed, so enemies who hear our pings (they stop at the first dragon, of either team)
    // cannot forge them without the key.
    static std::uint64_t mix(std::uint64_t z) {
        z += 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    static std::uint64_t checkOf(std::uint64_t payload) { return mix(payload ^ params::SONAR_KEY) & 0xFFFF; }

    std::uint64_t encodeKing(KingNews const& k) const {
        std::uint64_t const payload =
            (1ULL << 45) | (static_cast<std::uint64_t>(k.tile % W) << 38) |
            (static_cast<std::uint64_t>(k.tile / W) << 31) | (static_cast<std::uint64_t>(std::min(k.len, 1023)) << 21) |
            (static_cast<std::uint64_t>(k.round & 511) << 12) | static_cast<std::uint64_t>(k.id & 4095);
        return (checkOf(payload) << 48) | payload;
    }

    bool decodeKing(std::uint64_t m, KingNews& k) const {
        std::uint64_t const payload = m & ((1ULL << 48) - 1);
        if ((m >> 48) != checkOf(payload) || ((payload >> 45) & 7) != 1)
            return false;
        int const x = static_cast<int>((payload >> 38) & 127), y = static_cast<int>((payload >> 31) & 127);
        if (x >= W || y >= H)
            return false;
        k.tile = idxOf(x, y);
        k.len = static_cast<int>((payload >> 21) & 1023);
        k.round = static_cast<int>((payload >> 12) & 511);
        k.id = static_cast<int>(payload & 4095);
        return k.round <= round;
    }

    // Split handoff: [check 16][type 3 = 2][count 5][2 bits per segment, up to 20]. The parent
    // sends it out of its tail on the split turn; it lands on the child's tail, so the child
    // knows its whole body on its first turn, even the part out of view.
    static constexpr int HANDOFF_MAX = 20;

    std::uint64_t encodeBody(std::vector<int> const& dirs) const {
        int const count = std::min<int>(static_cast<int>(dirs.size()), HANDOFF_MAX);
        std::uint64_t payload = (2ULL << 45) | (static_cast<std::uint64_t>(count) << 40);
        for (int i = 0; i < count; i++)
            payload |= static_cast<std::uint64_t>(dirs[i] & 3) << (2 * i);
        return (checkOf(payload) << 48) | payload;
    }

    bool decodeBody(std::uint64_t m, std::vector<int>& dirs) const {
        std::uint64_t const payload = m & ((1ULL << 48) - 1);
        if ((m >> 48) != checkOf(payload) || ((payload >> 45) & 7) != 2)
            return false;
        int const count = static_cast<int>((payload >> 40) & 31);
        if (count > HANDOFF_MAX)
            return false;
        dirs.clear();
        for (int i = 0; i < count; i++)
            dirs.push_back(static_cast<int>((payload >> (2 * i)) & 3));
        return true;
    }

    // Pearl news: [check 16][type 3 = 3][round 9][three tiles, 12 bits each as x<<6|y]. Empty
    // slots repeat the first tile. Only maps up to 64x64, which is every legal map.
    std::uint64_t encodePearls(int seenRound, int const* tiles, int count) const {
        auto pos = [&](int t) { return static_cast<std::uint64_t>(((t % W) << 6) | (t / W)); };
        std::uint64_t payload = (3ULL << 45) | (static_cast<std::uint64_t>(seenRound & 511) << 36);
        for (int i = 0; i < 3; i++)
            payload |= pos(tiles[i < count ? i : 0]) << (24 - 12 * i);
        return (checkOf(payload) << 48) | payload;
    }

    bool decodePearls(std::uint64_t m, int& seenRound, int* tiles) const {
        std::uint64_t const payload = m & ((1ULL << 48) - 1);
        if ((m >> 48) != checkOf(payload) || ((payload >> 45) & 7) != 3)
            return false;
        seenRound = static_cast<int>((payload >> 36) & 511);
        if (seenRound > round)
            return false;
        for (int i = 0; i < 3; i++) {
            int const v = static_cast<int>((payload >> (24 - 12 * i)) & 4095);
            int const x = v >> 6, y = v & 63;
            if (x >= W || y >= H)
                return false;
            tiles[i] = idxOf(x, y);
        }
        return true;
    }

    // Share pearls: what we see now first, then the freshest we have heard of. Each ping reaches
    // the first dragon in its line; allies pass it on, so news crosses the map in a few turns.
    void pearlSonar() {
        if (!params::PEARL_SONAR)
            return;
        for (std::uint64_t m : ct.get_sonar_messages()) {
            int seenRound, tiles[3];
            if (!decodePearls(m, seenRound, tiles))
                continue;
            for (int t : tiles) {
                // What we saw ourselves is never overridden by older news.
                if (lastSeen[t] < seenRound && K.maxGap[t] != 0) {
                    lastSeen[t] = seenRound;
                    pearlSeen[t] = 1;
                    heard[t] = 1;
                }
            }
        }
        std::vector<int> share;
        for (int t = 0; t < N; t++)
            if (pearlSeen[t] && lastSeen[t] >= 0 && round - lastSeen[t] <= params::PEARL_RELAY_AGE &&
                (params::PEARL_RELAY || !heard[t]))
                share.push_back(t);
        if (share.empty())
            return;
        std::sort(share.begin(), share.end(), [&](int a, int b) { return lastSeen[a] > lastSeen[b]; });
        int const n = std::min<int>(static_cast<int>(share.size()), 12);
        int const directions = (n + 2) / 3;
        for (int k = 0; k < directions; k++) {
            int const count = std::min(3, n - 3 * k);
            int const* tiles = share.data() + 3 * k;
            int oldest = round;
            for (int i = 0; i < count; i++)
                oldest = std::min(oldest, lastSeen[tiles[i]]);
            ct.send_sonar(Direction(DCH[k]), encodePearls(oldest, tiles, count));
        }
    }

    static bool bigger(KingNews const& a, KingNews const& b) {
        return a.len > b.len || (a.len == b.len && a.id < b.id);
    }

    void consider(KingNews const& k) {
        if (k.id == (myId & 4095) || round - k.round > params::KING_FRESH)
            return;
        if (news.id < 0 || (k.id == news.id && k.round >= news.round) || bigger(k, news))
            news = k;
    }

    // Take in what we heard and saw, decide whether we are the king, and pass the news on.
    void sonarTurn(int length) {
        if (!params::SONAR)
            return;
        if (news.id >= 0 && round - news.round > params::KING_FRESH)
            news = {};
        for (std::uint64_t m : ct.get_sonar_messages()) {
            KingNews k;
            if (decodeKing(m, k))
                consider(k);
        }
        for (auto const& a : allyHeads)
            consider({a.id & 4095, a.tile, a.len, round});
        if (round < params::CONSOLIDATE_ROUND - params::BEACON_LEAD)
            return;
        KingNews const me{myId & 4095, body.front(), length, round};
        amKing = news.id < 0 || bigger(me, news);
        KingNews const& say = amKing ? me : news;
        std::uint64_t const message = encodeKing(say);
        for (auto const direction : Direction::get_direction_list())
            ct.send_sonar(direction, message);
    }

    // Late game: the longest ally in view that we should feed, if any.
    void chooseKing(int length, int units) {
        kingTile = -1;
        kingLen = 0;
        kingVisible = false;
        if (round < params::CONSOLIDATE_ROUND || units <= params::FEED_KEEP_UNITS)
            return;
        // Any ally bigger than us (ties to the lower id) is a king: feeding it makes it bigger
        // still, so feeders converge on one dragon instead of all waiting for a big one.
        int kingId = myId;
        for (auto const& a : allyHeads) {
            bool const bigger = a.len > length || (a.len == length && a.id < myId);
            bool const better = a.len > kingLen || (a.len == kingLen && a.id < kingId);
            if (bigger && better && a.len >= length + params::FEED_MARGIN) {
                kingTile = a.tile;
                kingLen = a.len;
                kingId = a.id;
            }
        }
        kingVisible = kingTile >= 0;
        if (!kingVisible && params::SONAR && news.id >= 0 && !amKing && news.len >= length + params::FEED_MARGIN &&
            (news.len > length || news.id < (myId & 4095))) {
            kingTile = news.tile;
            kingLen = news.len;
        }
        if (kingTile >= 0)
            bfsFrom({{-1, kingTile, kingLen}}, kingDist, nullptr);
    }

    // Die next to the king: our body becomes pearls right where it is about to go.
    bool tryFeed() {
        if (kingTile < 0 || !kingVisible || kingDist[body.front()] > params::FEED_DIST)
            return false;
        ct.do_split(1); // an illegal split: we die with no valid action
        plannedPath.clear();
        ct.set_indicator_string("feed");
        return true;
    }

    // Swarm phase: split in half whenever long enough and both halves are safe.
    bool trySwarmSplit(int length, int units, Candidate const& best, bool bodyKnown) {
        if (round >= params::CONSOLIDATE_ROUND || length < params::SWARM_SPLIT_LEN || !bodyKnown ||
            units >= game.get_unit_limit() || (best.eats && !params::SPLIT_WHILE_EATING))
            return false;
        int const child = length / 2;
        if (!ct.can_split(child))
            return false;
        // The parent stands still for a turn: by default not where an enemy head can reach it.
        if (threatLen[body.front()] < INF && !params::SPLIT_THREAT_OK)
            return false;
        // Safety 2: both halves safe. Safety 1: at most a 2-segment piece may be lost.
        int const needed = params::SPLIT_SAFETY >= 2 ? length : length - 2;
        if (splitValue(child) < needed)
            return false;
        split(child);
        ct.set_indicator_string("swarm split");
        return true;
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
        // Hand the child its body: from its head (our tail) towards our new tail.
        int const length = static_cast<int>(body.size());
        if (params::HANDOFF && length == ct.get_length() && child <= length && length - child >= 1) {
            std::vector<int> dirs;
            for (int i = length - 1; i > length - child; i--) {
                int found = -1;
                for (int d = 0; d < 4; d++)
                    if (nbr[body[i] * 4 + d] == body[i - 1])
                        found = d;
                if (found < 0)
                    return;
                dirs.push_back(found);
            }
            // Sent against our heading, a ping leaves through our tail, straight into the child.
            ct.send_sonar(ct.get_dir().get_opposite(), encodeBody(dirs));
        }
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
        if (params::VORONOI) {
            std::vector<Head> others(enemyHeads);
            others.insert(others.end(), allyHeads.begin(), allyHeads.end());
            bfsFrom(others, othersDist, nullptr, false);
        }

        std::fill(staticBlock.begin(), staticBlock.end(), 0);
        for (int t = 0; t < N; t++)
            if (inView[t] && occId[t] >= 0 && occId[t] != myId)
                staticBlock[t] = releaseAt[t];
        block = staticBlock;
        blockSaved.clear();

        int const units = ct.get_unit_count();
        unitsNow = units;
        aggressive = units >= params::AGGRO_MIN_UNITS && !enemyHeads.empty() &&
                     static_cast<double>(allyHeads.size() + 1) >= params::AGGRO_RATIO * enemyHeads.size();
        computeThreats();
        sonarTurn(length);
        pearlSonar();
        chooseKing(length, units);
        choosePrey(length, units);

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
                          "units", units, "king", kingLen, "|", s);
        }

        if (tryStrike(length, units))
            return;

        bool const canSplit = ct.can_split(length / 2);
        int const head = body.front();

        // Trapped (or worse): compare moving on with every split, by length expected to survive.
        bool const bodyKnown = static_cast<int>(body.size()) == length;
        if ((!best || (!best->safe && !best->dive)) && canSplit && bodyKnown) {
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

        if (tryFeed())
            return;

        // Insurance split: a lone dragon dying loses the game, whatever its length.
        if (ct.get_unit_count() < params::MIN_UNITS && length >= params::INSURANCE_MIN_LEN &&
            ct.can_split(params::INSURANCE_CHILD) && bodyKnown && best->safe && !enemyNear(head, 3) &&
            splitValue(params::INSURANCE_CHILD) == length) {
            split(params::INSURANCE_CHILD);
            ct.set_indicator_string("insurance split");
            return;
        }

        if (trySwarmSplit(length, units, *best, bodyKnown))
            return;

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
