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
#include "sonar.hpp"

#include <algorithm>
#include <array>
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
    std::vector<int> kingDist;

    // Hunting: enemy heads we saw recently (a big dragon stays worth chasing for a while).
    struct Sighting { int id; int tile; int len; int round; };
    std::vector<Sighting> sightings;
    std::vector<int> preyDist; // steps to the nearest enemy head worth trading with
    std::vector<int> othersDist; // moves any other dragon's head needs to reach a tile (bodies block)
    bool aggressive = false;     // we clearly outnumber the enemies around us
    double preyGain = 0.0;     // what the best such trade is worth

    // Our own body, head first. Tracked across turns so we know it beyond the window.
    std::deque<int> body;
    std::vector<int> plannedPath;
    bool haveBody = false;

    // Scratch - pre-allocated to avoid allocations per turn.
    std::vector<int> distE, distA, ownerA, block, dist, queue, staticBlock, seen, from, fromDir;
    std::vector<std::pair<int, int>> blockSaved;
    int stamp = 0;
    std::vector<uint8_t> owned; // during evaluate's search: tile is in our Voronoi room
    std::vector<int> ownStamp; // == ownScenario for tiles of our own body in the current scenario
    int ownScenario = 0;
    std::vector<double> powBest, powSum, decayPow, heatDecay;
    // Cached BFS sources to avoid vector allocation.
    std::vector<Head> bfsSources;

    bool identified = false;
    int unitsNow = 1; // our team's dragon count this turn

    // Sonar: what we send this turn (per direction), BODY payloads we received, and the
    // longest dragon of ours we know of (ourselves, a sighting, or a relayed KING record).
    std::array<std::uint64_t, 4> outbox{};
    std::array<bool, 4> outboxSet{};
    std::vector<std::uint64_t> bodyMsgs;
    sonar::King king;
    bool kingVisible = false; // the dragon we would feed is in view now
    // Their king: the longest enemy head we saw or were told of (HUNT_KING), and our steps to it.
    sonar::Enemy enemyKing;
    std::vector<int> huntDist;
    bool hunting = false;
    // Attack: enemy heads close enough to wall in this turn (SEAL), and steps to long enemy heads (STEAL).
    std::vector<Head> sealTargets;
    std::vector<int> stealDist;

    // Feedback within the game. Numbers war: ally vs enemy heads around us (view plus the echo
    // counts our pings bring back), smoothed over rounds. Length race: our king against the
    // longest enemy anyone of ours has seen lately.
    double allyEma = 0.0, enemyEma = 0.0;
    bool defensive = false;
    int enemyMax = 0, enemyMaxRound = -1000;
    int race = 0; // +1 ahead, -1 behind, 0 level or unknown
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
        from.assign(N, -1);
        fromDir.assign(N, 0);
        threatLen.assign(N, INF);
        kingDist.assign(N, INF);
        preyDist.assign(N, INF);
        huntDist.assign(N, INF);
        stealDist.assign(N, INF);
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
        // Only need decay up to reasonable horizon (max body length ~100), not full 500 rounds.
        int const decayLimit = std::min(Constants::MAX_ROUNDS, 200);
        decayPow.resize(decayLimit + 2);
        heatDecay.resize(decayLimit + 2);
        for (int k = 0; k <= decayLimit + 1; k++) {
            decayPow[k] = std::pow(params::REMEMBERED_DECAY, k);
            heatDecay[k] = std::pow(params::REVISIT_DECAY, k);
        }
        bfsSources.reserve(16);
    }

    // -- knowledge -----------------------------------------------------------

    // Walls and portals must match exactly. Pearl data may differ a little (the ladder has
    // changed maps, e.g. the 10-dragon Dilemma): our copy is only a starting guess, and what we
    // see overrides it (absorbView).
    bool mapMatchesView(MapInfo const& m) const {
        int spawnMismatch = 0;
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
            if (never != (m.maxGap[t] == 0) && ++spawnMismatch > params::SPAWN_MISMATCH_OK)
                return false;
        }
        return true;
    }

    void identifyMap() {
        identified = true;
        if (!params::USE_MAP_DATA)
            return; // behave as on a map we have never seen: learn everything from view
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
        // Match on head and length only: the ladder often swaps which team starts where, so
        // the team label in our copy of the map can be the other side's.
        for (auto const& s : m.spawns) {
            if (!s.body.empty() && s.body.front() == head && static_cast<int>(s.body.size()) == ct.get_length()) {
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
        if (params::DEBUG_SONAR && !ok && haveBody && ct.get_length() >= 8)
            ct.output_log("body lost", ct.get_length(), "planned", plannedPath.size(), "round", round);
        // A new child's parent sends its shape at the split; otherwise rebuild what we can see.
        if (!ok && !bodyFromSonar()) {
            rebuildBodyFromView();
            if (params::STRAIGHT_START && round == 0)
                extendStraight();
        }
        while (static_cast<int>(body.size()) > ct.get_length())
            body.pop_back();
        plannedPath.clear();
    }

    // Round 0 without map data (or on a changed map): every start we have seen is a straight
    // line, so extend what we can see straight back, checked against the view.
    void extendStraight() {
        int const len = ct.get_length();
        if (static_cast<int>(body.size()) >= len)
            return;
        int d;
        if (body.size() >= 2) {
            d = dirTo(body[body.size() - 2], body.back());
            // Only a body that is straight as far as we can see (some starts zigzag).
            for (std::size_t i = 0; i + 1 < body.size(); i++)
                if (dirTo(body[i], body[i + 1]) != d)
                    return;
        } else {
            d = (dirIndex(ct.get_dir()) + 2) % 4; // behind the head
        }
        std::deque<int> b = body;
        while (d >= 0 && static_cast<int>(b.size()) < len) {
            int const t = nbr[b.back() * 4 + d];
            if (t < 0 || (inView[t] && occId[t] != myId) || std::find(b.begin(), b.end(), t) != b.end())
                return; // not a straight line after all: keep what we know
            b.push_back(t);
        }
        body = std::move(b);
        haveBody = true;
    }

    // Our whole body from a BODY message, if one fits: right length, every link a real step,
    // and every segment we can see is ours.
    bool bodyFromSonar() {
        int const head = idxOf(ct.get_position());
        int const len = ct.get_length();
        for (std::uint64_t const p : bodyMsgs) {
            if (static_cast<int>(p & 31) != len || len < 2 || len > sonar::BODY_MAX_LEN)
                continue;
            std::deque<int> b{head};
            bool ok = true;
            for (int i = 0; i + 1 < len && ok; i++) {
                int const t = nbr[b.back() * 4 + static_cast<int>((p >> (5 + 2 * i)) & 3)];
                ok = t >= 0 && !(inView[t] && occId[t] != myId);
                b.push_back(t);
            }
            if (ok) {
                body = std::move(b);
                haveBody = true;
                if (params::DEBUG_SONAR)
                    ct.output_log("body from sonar", len);
                return true;
            }
        }
        if (params::DEBUG_SONAR)
            ct.output_log("no body message", bodyMsgs.size(), "len", len);
        return false;
    }

    // Whether splitting off `child` cuts the body where it runs straight: the BODY message
    // leaves the parent's tail along its last segment, so only then does it hit the child.
    bool straightCut(int child) const {
        int const p = static_cast<int>(body.size()) - child;
        if (p < 2 || child < 1)
            return false;
        int const d = dirTo(body[p - 2], body[p - 1]);
        return d >= 0 && d == dirTo(body[p - 1], body[p]);
    }

    // Whether a child of this size would have its whole body inside its own 7x7 window.
    bool childSeesItself(int child) const {
        int const length = static_cast<int>(body.size());
        int const h = body[length - 1];
        for (int i = 0; i < child; i++) {
            int const t = body[length - 1 - i];
            int const dx = ((t % W) - (h % W) + Constants::VISION_RADIUS + W) % W;
            int const dy = ((t / W) - (h / W) + Constants::VISION_RADIUS + H) % H;
            if (dx >= Constants::VISION_SIZE || dy >= Constants::VISION_SIZE)
                return false;
        }
        return true;
    }

    // The child will know its whole body: it sees all of it, or our message reaches it.
    bool childKnowsBody(int child) const { return childSeesItself(child) || straightCut(child); }

    // Direction d with nbr[from][d] == to, or -1.
    int dirTo(int from, int to) const {
        for (int d = 0; d < 4; d++)
            if (nbr[from * 4 + d] == to)
                return d;
        return -1;
    }

    // -- sonar -----------------------------------------------------------------

    void readSonar() {
        bodyMsgs.clear();
        for (std::uint64_t const msg : ct.get_sonar_messages()) {
            sonar::Type type;
            std::uint64_t payload;
            if (!sonar::unpack(msg, type, payload))
                continue; // an enemy's, or noise
            if (type == sonar::BODY)
                bodyMsgs.push_back(payload);
            else if (type == sonar::KING)
            {
                sonar::King const k = sonar::unpackKing(payload);
                considerKing(k, false);
                noteEnemyLen(k.enemyMax, k.round);
            }
            else if (type == sonar::ENEMY)
                considerEnemyKing(sonar::unpackEnemy(payload));
        }
    }

    // Their longest head: longer wins; the same dragon seen later moves the record along.
    // Our view only bounds its length from below, so keep the longest we have heard.
    void considerEnemyKing(sonar::Enemy r) {
        if (r.id < 0 || r.round > round || round - r.round > params::HUNT_KING_STALE)
            return;
        bool const expired = enemyKing.id < 0 || round - enemyKing.round > params::HUNT_KING_STALE;
        if (r.id == enemyKing.id) {
            if (r.round >= enemyKing.round) {
                r.len = std::max(r.len, enemyKing.len);
                enemyKing = r;
            }
        } else if (expired || r.len > enemyKing.len) {
            enemyKing = r;
        }
    }

    // Keep the longest dragon of ours we know of; a record ages out after KING_STALE rounds
    // unless its dragon (or someone who sees it) refreshes it. Sightings only bound the length.
    void considerKing(sonar::King r, bool lowerBound) {
        if (r.id < 0 || r.round > round || round - r.round > params::KING_STALE)
            return;
        if (r.id == king.id) {
            if (r.round >= king.round) {
                if (lowerBound)
                    r.len = std::max(r.len, king.len);
                king = r;
            }
            return;
        }
        bool const expired = king.id < 0 || round - king.round > params::KING_STALE;
        // With a margin the king only changes hands to a clearly longer dragon, so feeders
        // don't split their length between near-equals.
        bool const better = params::KING_SWITCH_MARGIN > 0
                                ? r.len >= king.len + params::KING_SWITCH_MARGIN
                                : r.len > king.len || (r.len == king.len && r.id < king.id);
        if (expired || better)
            king = r;
    }

    void noteEnemyLen(int len, int r) {
        if (len <= 0 || r > round)
            return;
        bool const stale = round - enemyMaxRound > params::ENEMY_MAX_STALE;
        if (stale || len > enemyMax) {
            enemyMax = len;
            enemyMaxRound = r;
        } else if (len == enemyMax) {
            enemyMaxRound = std::max(enemyMaxRound, r);
        }
    }

    void updateKing(int length) {
        int const head = body.front();
        considerKing({myId, length, head % W, head / W, round}, false);
        for (auto const& a : allyHeads)
            considerKing({a.id, a.len, a.tile % W, a.tile / W, round}, true);
        for (auto const& e : enemyHeads) {
            noteEnemyLen(e.len, round);
            if (params::HUNT_KING)
                considerEnemyKing({e.id, e.len, e.tile % W, e.tile / W, round});
        }
    }

    // After the split the parent stays put: a sonar sent into its own neck comes out of its
    // tail, straight into the child's head, which reads it on its first turn this round.
    void sendChildBody(int child) {
        int const length = static_cast<int>(body.size());
        if (child > sonar::BODY_MAX_LEN || length < child + 2 || !straightCut(child))
            return;
        std::uint64_t p = static_cast<std::uint64_t>(child);
        for (int i = 0; i + 1 < child; i++) {
            int const d = dirTo(body[length - 1 - i], body[length - 2 - i]);
            if (d < 0)
                return;
            p |= static_cast<std::uint64_t>(d) << (5 + 2 * i);
        }
        int const neck = dirTo(body[0], body[1]);
        if (neck < 0)
            return;
        outbox[neck] = sonar::pack(sonar::BODY, p);
        outboxSet[neck] = true;
    }

    // Whatever was queued, and the king we know of in every other direction (not into kelp).
    void sendSonar() {
        if (!params::SONAR)
            return;
        bool const haveKing = king.id >= 0 && round - king.round <= params::KING_STALE;
        // One direction a turn (rotating) carries their king instead of ours.
        int const enemyDir = params::HUNT_KING && enemyKing.id >= 0 && enemyKing.len >= params::HUNT_KING_MIN_LEN &&
                                     round - enemyKing.round <= params::HUNT_KING_STALE
                                 ? round % 4
                                 : -1;
        for (int d = 0; d < 4; d++) {
            if (outboxSet[d])
                ct.send_sonar(Direction(DCH[d]), outbox[d]);
            else if (d == enemyDir && haveBody && nbr[body.front() * 4 + d] != STEP_KELP)
                ct.send_sonar(Direction(DCH[d]), sonar::packEnemy(enemyKing));
            else if (haveKing && haveBody && nbr[body.front() * 4 + d] != STEP_KELP)
            {
                sonar::King k = king;
                k.enemyMax = round - enemyMaxRound <= params::ENEMY_MAX_STALE ? enemyMax : 0;
                ct.send_sonar(Direction(DCH[d]), sonar::packKing(k));
            }
        }
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
            int const d = out[t];
            for (int dir = 0; dir < 4; dir++) {
                int const n = nbr[t * 4 + dir];
                if (n < 0 || out[n] != INF || (!throughBodies && inView[n] && occId[n] >= 0))
                    continue;
                out[n] = d + 1;
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
        bool dive = false; // unsafe, but a pocket with food worth going into (see evaluate)
        int newLength = 0;
        int sealed = 0;    // enemy segments this move leaves without room to live (SEAL)
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
            score += kingsFood(dest) ? -params::KING_FOOD_PENALTY : params::EAT_NOW;
        if (!inView[dest])
            score -= params::OUT_OF_VIEW;
        score -= params::REVISIT * heat(dest);

        // Our body after the move, and when each of its tiles frees up.
        int const newLength = c.eats ? length + 1 : length;
        c.newLength = newLength;

        // A pearl due on dest while our body still covers it (the next newLength rounds) never
        // spawns: the attempt is lost and the countdown starts over.
        if (!c.eats && inView[dest] && nextSpawn[dest] > round && nextSpawn[dest] <= round + newLength)
            score -= params::SPAWN_BLOCK_COST;
        // Swarm phase: spread out, away from ally heads (feeders later come close on purpose).
        if (round < params::CONSOLIDATE_ROUND && distA[dest] < INF) {
            if (distA[dest] <= 1)
                score -= params::ALLY_ADJ_PENALTY;
            score += params::ALLY_SPREAD * std::min(distA[dest], params::ALLY_SPREAD_CAP);
        }
        bool const kingRoomVsEnemies = params::ENDGAME_KEEP_WHOLE && round >= params::CONSOLIDATE_ROUND &&
                                       length >= params::KING_ROOM_LEN;
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
        double best = 0.0, sum = 0.0;
        double pocketFood = c.eats ? 1.0 : 0.0; // expected pearls we can reach, undiscounted
        while (qh < qt) {
            int const t = queue[qh++];
            int const k = dist[t];
            if (t != dest && !(inView[t] && occId[t] >= 0)) {
                double v = kingsFood(t) ? 0.0 : pearlBelief(t, round + 1 + k);
                // Food dropped for their king: ours is +1, and it is one less for the dragon that
                // decides the tiebreak.
                if (params::STEAL_BONUS > 0.0 && stealDist[t] <= params::STEAL_RADIUS)
                    v *= 1.0 + params::STEAL_BONUS;
                if (v > 0.0) {
                    int const moves = 1 + k;
                    if (distE[t] < moves)
                        v *= params::ENEMY_CLOSER;
                    // The king takes what is dropped for it, however many feeders are nearer.
                    if (!amKing() && (distA[t] < moves || (distA[t] == moves && ownerA[t] < myId)))
                        v *= params::ALLY_CLOSER;
                    best = std::max(best, v * powBest[k]);
                    sum += v * powSum[k];
                    pocketFood += v;
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
                // The endgame king counts only enemy heads as rivals for room: the feeders
                // crowding around it are coming to die for it, not to take its space.
                int const rival = kingRoomVsEnemies ? distE[n] : othersDist[n];
                owned[n] = owned[t] && (!params::VORONOI || rival >= k + 2);
                room += owned[n];
                queue[qt++] = n;
            }
        }
        // Pocket seal: with our body where this move puts it, which nearby enemy heads are left
        // without room to live? (Out-of-view tiles count as open, so only pockets we can see.)
        for (auto const& e : sealTargets) {
            int const need = roomyEnough(e.len);
            if (enemyRoom(e.tile, need) < need)
                c.sealed += e.len;
        }
        restoreBlock();
        c.space = room;
        c.safe = reachedOwn || room >= roomyEnough(newLength);
        score += params::FOOD_BEST * best + params::FOOD_SUM * sum;

        // Diving: most pearls on maze maps spawn in 1-wide lanes and dead ends, which are never
        // "safe". Going in is a priced risk rather than a death sentence when the pocket has food:
        //  - if eating it makes us 4+ long, we can split our way out later (the child takes our
        //    tail and faces out of the pocket), losing only the parent we leave behind;
        //  - a small dragon may die in there while we have dragons to spare: it eats first, and
        //    half its body comes back as pearls for the ally that follows it in.
        // Only where kelp makes the pocket: when other dragons are what boxes us in, the old
        // rule (avoid, or split out) stays.
        if (!c.safe && params::DIVE && newLength <= params::DIVE_MAX_LEN && pocketFood >= params::DIVE_MIN_FOOD &&
            (!params::DIVE_WALLS_ONLY || wallRoom(dest, roomyEnough(newLength)) < roomyEnough(newLength))) {
            bool const canSplitLater = unitsNow < game.get_unit_limit();
            if (canSplitLater && newLength + static_cast<int>(pocketFood) >= params::DIVE_ESCAPE_LEN) {
                c.dive = true;
                score -= params::DIVE_ESCAPE_COST;
            } else if (newLength <= params::DIVE_MAX_DEATH_LEN && unitsNow >= params::DIVE_MIN_UNITS) {
                c.dive = true;
                score -= params::DIVE_SEG_COST * newLength;
            }
        }
        if (!c.safe && !c.dive)
            score -= params::TRAPPED - 50.0 * std::min(c.space, newLength);
        else if (c.safe)
            score += 0.5 * std::min(c.space, 3 * newLength);
        // A long dragon (our king, usually) dies or has to split when boxed in: keep it in the open.
        if (newLength >= params::KING_ROOM_LEN)
            score += (race > 0 && king.id == myId ? 2.0 : 1.0) * params::KING_ROOM *
                     std::min(c.space, params::KING_ROOM_TILES_PER_LEN * newLength);

        // An enemy that can reach dest next turn may trade heads with us.
        if (threatLen[dest] < INF) {
            int const disadvantage = newLength - threatLen[dest];
            double scale = defensive ? params::ADAPT_THREAT_SCALE : 1.0;
            if (race != 0 && king.id == myId)
                scale *= race > 0 ? params::RACE_KING_SAFE : params::RACE_KING_BOLD;
            if (king.id == myId && newLength >= params::KING_ROOM_LEN)
                scale *= params::KING_THREAT_MULT;
            score -= scale * (disadvantage > 0 ? params::THREAT_PER_LEN * disadvantage : params::THREAT_EVEN);
        }
        if (kingTile >= 0 && kingDist[dest] < INF &&
            (params::KING_PULL_RADIUS <= 0 || kingDist[head] <= params::KING_PULL_RADIUS)) {
            score += params::FEED_ATTRACT / (1.0 + kingDist[dest]);
            // From afar the pull above is flat: reward every step that closes the distance.
            if (kingDist[head] < INF)
                score += params::KING_APPROACH * (kingDist[head] - kingDist[dest]);
        }
        if (preyGain > 0.0 && preyDist[dest] < INF)
            score += (aggressive ? params::HUNT_ATTRACT_AGGRO : params::HUNT_ATTRACT) *
                     std::min(preyGain, params::HUNT_GAIN_CAP) / (1.0 + preyDist[dest]);
        if (hunting && huntDist[dest] < INF)
            score += params::HUNT_KING_ATTRACT * (huntDist[head] - huntDist[dest]);
        score += std::uniform_real_distribution<double>(0.0, 1.0)(rng);
        c.score = score;
    }

    // In-game feedback (each part behind its own switch).
    void adaptToGame(int units) {
        defensive = false;
        if (params::ADAPT_NUMBERS) {
            auto const e = ct.get_sonar_echoes();
            double const a = static_cast<double>(allyHeads.size()) + e.ally_head;
            double const n = static_cast<double>(enemyHeads.size()) + e.enemy_head;
            allyEma = params::ADAPT_DECAY * allyEma + (1.0 - params::ADAPT_DECAY) * a;
            enemyEma = params::ADAPT_DECAY * enemyEma + (1.0 - params::ADAPT_DECAY) * n;
            double const ratio = (allyEma + 0.5) / (enemyEma + 0.5);
            if (ratio >= params::ADAPT_HI && units >= params::AGGRO_MIN_UNITS)
                aggressive = true; // we outnumber them around here: attrition favours us
            defensive = !aggressive && ratio <= params::ADAPT_LO;
        }
        race = 0;
        if (params::RACE_AWARE && round >= params::RACE_ROUND && king.id >= 0 && enemyMax > 0 &&
            round - enemyMaxRound <= params::ENEMY_MAX_STALE) {
            if (king.len >= enemyMax + params::RACE_MARGIN)
                race = 1;
            else if (enemyMax >= king.len + params::RACE_MARGIN)
                race = -1;
            // Behind: small dragons take risks for kills and trades.
            if (race < 0 && king.id != myId && units >= params::AGGRO_MIN_UNITS)
                aggressive = true;
        }
    }

    // From KING_GROW_ROUND the king we know of (the longest) stops swarm-splitting and grows.
    bool growingKing() const { return params::SONAR && round >= params::KING_GROW_ROUND && king.id == myId; }

    // Endgame with sonar: we are the king everyone is feeding.
    bool amKing() const {
        return params::SONAR && round >= params::KING_PULL_ROUND && king.id == myId;
    }
    // A pearl next to the king we feed is its food: leave it for it rather than eat it and
    // pass on only half when we feed in turn.
    bool kingsFood(int t) const {
        return kingVisible && round >= params::CONSOLIDATE_ROUND && kingDist[t] <= params::KING_FOOD_RADIUS;
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

    // Tiles reachable from `start` through kelp and portals alone (other dragons ignored, our
    // own body blocks), counting up to `cap`. Small means a lane or dead end in the terrain.
    int wallRoom(int start, int cap) {
        newSearch();
        for (int t : body)
            seen[t] = stamp;
        int qh = 0, qt = 0;
        visit(start, 0);
        queue[qt++] = start;
        while (qh < qt && qt < cap) {
            int const t = queue[qh++];
            for (int d = 0; d < 4 && qt < cap; d++) {
                int const n = nbr[t * 4 + d];
                if (n < 0 || seen[n] == stamp)
                    continue;
                visit(n, 0);
                queue[qt++] = n;
            }
        }
        return qt;
    }

    // Tiles an enemy head on `start` can reach, timed like our own room (a tile is open once it frees
    // by the step we would arrive), counting up to `cap`.
    int enemyRoom(int start, int cap) {
        newSearch();
        int qh = 0, qt = 0;
        visit(start, 0);
        queue[qt++] = start;
        while (qh < qt && qt < cap) {
            int const t = queue[qh++];
            int const k = dist[t];
            for (int d = 0; d < 4 && qt < cap; d++) {
                int const n = nbr[t * 4 + d];
                if (n < 0 || seen[n] == stamp || block[n] > k + 1)
                    continue;
                visit(n, k + 1);
                queue[qt++] = n;
            }
        }
        return qt;
    }

    int manhattan(int a, int b) const {
        int dx = std::abs(a % W - b % W), dy = std::abs(a / W - b / W);
        return std::min(dx, W - dx) + std::min(dy, H - dy);
    }

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
    // `longest`: count only the longest surviving piece (what matters at round 500).
    int splitValue(int child, bool longest = false) {
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

        // Chained split (from v2d): a stuck child of 4+ can split again on its own first turn.
        // That grandchild is the child's rear, head at the child's tail (our body[parentLen]),
        // so a piece gets out of the middle of a body whose both ends sit in dead ends.
        int chained = 0;
        if (params::CHAIN_SPLIT && !childSafe && child >= 4) {
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

        int const childPart = childSafe ? child : chained, parentPart = parentSafe ? parentLen : 0;
        return longest ? std::max(childPart, parentPart) : childPart + parentPart;
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
        if (enemyHeads.empty())
            return;
        // Multi-source BFS from all enemy heads at once.
        // Use a vector of (tile, enemy_len, steps_remaining) but we can do it simpler:
        // For each enemy head, do a limited BFS. Since reach is small (max 6), this is OK.
        // But we can optimize by doing a single BFS with priority on shorter enemies.
        newSearch();
        int qh = 0, qt = 0;
        // Initialize queue with all enemy heads.
        // We need to track remaining steps per enemy, so we use a separate queue per enemy
        // or encode remaining steps in the queue. Simpler: process each enemy separately
        // but skip already-visited tiles with better threat.
        for (auto const& e : enemyHeads) {
            int const reach = std::clamp(e.len - 1, 1, params::STRIKE_MAX_STEPS);
            newSearch();
            int lqh = 0, lqt = 0;
            visit(e.tile, 0);
            queue[lqt++] = e.tile;
            while (lqh < lqt) {
                int const t = queue[lqh++];
                int const k = dist[t];
                if (k >= reach)
                    continue;
                for (int d = 0; d < 4; d++) {
                    int const n = nbr[t * 4 + d];
                    if (n < 0 || seen[n] == stamp)
                        continue;
                    if (inView[n] && occId[n] >= 0) {
                        if (params::THREAT_ON_OUR_HEADS && occHead[n] && occTeam[n] == myTeam)
                            threatLen[n] = std::min(threatLen[n], e.len);
                        continue;
                    }
                    visit(n, k + 1);
                    queue[lqt++] = n;
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
        if (gain > 0)
            return gain;
        // Even trades thin both swarms; worth it while we have dragons to spare, or when we
        // outnumber the enemies around us (their losses hurt them more).
        if (gain == 0 && (aggressive || (!defensive && round < params::CONSOLIDATE_ROUND && units >= params::TRADE_MIN_UNITS)))
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

    // Their king, if a small dragon of ours should go after it: one head trade by a 2- or
    // 3-long dragon takes the longest-dragon tiebreak away from them.
    void chooseHunt(int length, int units) {
        hunting = false;
        if (!params::HUNT_KING || round < params::HUNT_KING_ROUND || length > params::HUNT_KING_MAX_LEN ||
            units < params::HUNT_KING_MIN_UNITS || enemyKing.id < 0 ||
            round - enemyKing.round > params::HUNT_KING_STALE || enemyKing.len < params::HUNT_KING_MIN_LEN ||
            enemyKing.len <= length)
            return;
        bfsFrom({{enemyKing.id, idxOf(enemyKing.x, enemyKing.y), enemyKing.len}}, huntDist, nullptr);
        hunting = huntDist[body.front()] <= params::HUNT_KING_RADIUS;
    }

    // This turn's attack targets: the (up to 3) closest enemy heads we might wall in, and the steps
    // to long enemy heads whose food we would steal.
    void chooseAttackTargets() {
        sealTargets.clear();
        if (params::SEAL_BONUS > 0.0) {
            int const head = body.front();
            for (auto const& e : enemyHeads)
                if (manhattan(e.tile, head) <= params::SEAL_RADIUS)
                    sealTargets.push_back(e);
            std::sort(sealTargets.begin(), sealTargets.end(), [&](Head const& a, Head const& b) {
                return manhattan(a.tile, head) < manhattan(b.tile, head);
            });
            if (sealTargets.size() > 3)
                sealTargets.resize(3);
        }
        if (params::STEAL_BONUS > 0.0) {
            std::vector<Head> kings;
            for (auto const& e : enemyHeads)
                if (e.len >= params::STEAL_MIN_LEN)
                    kings.push_back(e);
            if (kings.empty())
                std::fill(stealDist.begin(), stealDist.end(), INF);
            else
                bfsFrom(kings, stealDist, nullptr);
        }
    }

    // Late game: the longest ally in view that we should feed, if any.
    void chooseKing(int length, int units) {
        kingTile = -1;
        kingLen = 0;
        kingVisible = false;
        // Late in the game the last two dragons may merge too (we lost 10-11 and 17-18 holding
        // two that never could); earlier, keep a spare against elimination.
        bool const finalMerge = params::FINAL_MERGE && round >= params::FINAL_MERGE_ROUND;
        int const keep = finalMerge ? 1 : params::FEED_KEEP_UNITS;
        if (units <= keep)
            return;
        // A long dragon forages for itself until the final merge: feeding hands the king only half.
        if (length > params::FEED_MAX_LEN && !finalMerge)
            return;
        // With sonar the whole team agrees on one king: the longest dragon anyone has heard of.
        // Head for it from KING_PULL_ROUND, feed it once it is in view.
        if (params::SONAR && king.id >= 0 && round - king.round <= params::KING_STALE) {
            if (king.id == myId || round < params::KING_PULL_ROUND)
                return;
            kingTile = idxOf(king.x, king.y);
            kingLen = king.len;
            for (auto const& a : allyHeads) {
                if (a.id == king.id) {
                    kingTile = a.tile;
                    kingVisible = true;
                }
            }
            bfsFrom({{-1, kingTile, kingLen}}, kingDist, nullptr);
            return;
        }
        if (round < params::CONSOLIDATE_ROUND)
            return;
        // Any ally bigger than us (ties to the lower id) is a king: feeding it makes it bigger
        // still, so feeders converge on one dragon instead of all waiting for a big one.
        int kingId = myId;
        for (auto const& a : allyHeads) {
            bool const bigger = a.len > length || (a.len == length && a.id < myId);
            bool const better = a.len > kingLen || (a.len == kingLen && a.id < kingId);
            if (bigger && better && a.len >= length + params::FEED_MARGIN && worthFeeding(a.len, length)) {
                kingTile = a.tile;
                kingLen = a.len;
                kingId = a.id;
            }
        }
        if (kingTile >= 0) {
            kingVisible = true;
            bfsFrom({{-1, kingTile, kingLen}}, kingDist, nullptr);
        }
    }

    // Whether a king this long is worth dying for (the top teams feed only clearly long allies).
    bool worthFeeding(int kingLength, int length) const {
        if (params::FINAL_MERGE && round >= params::FINAL_MERGE_ROUND)
            return true;
        return kingLength >= params::FEED_MIN_KING_LEN && kingLength >= params::FEED_KING_RATIO * length;
    }

    // Die next to the king: our body becomes pearls right where it is about to go.
    bool tryFeed(int length) {
        if (kingTile < 0 || !kingVisible || round < params::CONSOLIDATE_ROUND ||
            kingDist[body.front()] > params::FEED_DIST || !worthFeeding(kingLen, length))
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
        // The parent stands still for a turn, so not where an enemy head can reach it.
        if (threatLen[body.front()] < INF)
            return false;
        // Early king (sparring style): from KING_GROW_ROUND the king stops halving itself.
        if (growingKing())
            return false;
        // Halves, or one off, preferring a cut the child's body message can reach. A child that
        // could neither see its whole body nor be told it waits: next turn the body may be straight.
        // Optionally also a 2-long bud off the tail, or the reversal (the head keeps 2).
        std::vector<int> cuts{length / 2, length / 2 + 1, length / 2 - 1};
        if (params::SWARM_SPLIT_CUTS) {
            cuts.push_back(2);
            cuts.push_back(length - 2);
        }
        int child = 0;
        for (int c : cuts) {
            if (ct.can_split(c) && splitValue(c) == length &&
                (!params::SONAR || !params::SPLIT_WAITS_FOR_HANDOFF || childKnowsBody(c))) {
                child = c;
                break;
            }
        }
        if (child == 0)
            return false;
        split(child);
        ct.set_indicator_string("swarm split");
        return true;
    }

    // Shed the head: a shorter enemy can reach our head now and after every move we have, and the
    // swarm teams ram such heads. Leave a 2-long stub at the head (a ram then costs us 2) and send the
    // rest out from the tail, if it is safe there.
    bool tryShed(int length, bool bodyKnown, Candidate const* cands) {
        if (!params::SHED || !bodyKnown || length < params::SHED_MIN_LEN || threatLen[body.front()] >= length)
            return false;
        for (int d = 0; d < 4; d++) {
            auto const& c = cands[d];
            if (!c.fatal && c.dest >= 0 && threatLen[c.dest] >= c.newLength)
                return false; // a move out of reach exists
        }
        int const child = length - 2;
        if (!ct.can_split(child) || splitValue(child) < child)
            return false;
        split(child);
        ct.set_indicator_string("shed");
        return true;
    }

    // An enemy head next to our tail tip: split a 2-long child off the tail. The child is born with
    // its head there and moves last this round, so its own tryStrike rams the enemy (worth it when
    // the enemy is longer). If the enemy moves away first, this was just an ordinary 2-off split.
    bool tryTailStrike(int length, int units, bool bodyKnown) {
        int const child = 2;
        if (!params::TAIL_STRIKE || !bodyKnown || length < 4 || units <= 1 || units >= game.get_unit_limit() ||
            !ct.can_split(child))
            return false;
        // The parent stands still for a turn: not where a shorter enemy could trade heads with it.
        if (threatLen[body.front()] < length - child)
            return false;
        int const minLen = (round >= params::CONSOLIDATE_ROUND || growingKing()) && length >= params::KING_ROOM_LEN
                               ? params::TAIL_STRIKE_KING_MIN
                               : params::TAIL_STRIKE_MIN;
        int const tail = body.back();
        for (auto const& e : enemyHeads) {
            if (e.len < minLen || e.len <= child)
                continue;
            for (int d = 0; d < 4; d++) {
                if (nbr[tail * 4 + d] == e.tile) {
                    split(child);
                    ct.set_indicator_string("tail strike");
                    return true;
                }
            }
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
        if (params::SONAR)
            sendChildBody(child);
        plannedPath.clear(); // the parent stays put
    }

    void turn() {
        outboxSet.fill(false);
        decide();
        sendSonar();
    }

    void decide() {
        round = game.get_round_num();
        absorbView();
        if (!identified)
            identifyMap();
        readSonar();
        updateBody();
        markVisited(body.front());

        int const length = ct.get_length();
        bfsFrom(enemyHeads, distE, nullptr);
        std::sort(allyHeads.begin(), allyHeads.end(), [](Head const& a, Head const& b) { return a.id < b.id; });
        bfsFrom(allyHeads, distA, &ownerA);
        if (params::VORONOI) {
            bfsSources.clear();
            bfsSources.insert(bfsSources.end(), enemyHeads.begin(), enemyHeads.end());
            bfsSources.insert(bfsSources.end(), allyHeads.begin(), allyHeads.end());
            bfsFrom(bfsSources, othersDist, nullptr, false);
        } else {
            std::fill(othersDist.begin(), othersDist.end(), INF);
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
        updateKing(length);
        adaptToGame(units);
        chooseKing(length, units);
        if (params::DEBUG_SONAR && round >= params::KING_PULL_ROUND && round % 25 == 0)
            ct.output_log("king", king.id, king.len, "age", round - king.round, "me", myId, length,
                          "dist", kingTile >= 0 ? kingDist[body.front()] : -1, "vis", kingVisible ? 1 : 0);
        // Skip prey/hunt/attack BFS when their weights are zero.
        if (params::HUNT_ATTRACT > 0.0 || params::HUNT_ATTRACT_AGGRO > 0.0)
            choosePrey(length, units);
        else
            std::fill(preyDist.begin(), preyDist.end(), INF);
        if (params::HUNT_KING)
            chooseHunt(length, units);
        else
            hunting = false;
        if (params::SEAL_BONUS > 0.0 || params::STEAL_BONUS > 0.0)
            chooseAttackTargets();
        else {
            sealTargets.clear();
            std::fill(stealDist.begin(), stealDist.end(), INF);
        }

        Candidate cands[4];
        for (int d = 0; d < 4; d++) {
            cands[d].dir = d;
            evaluate(cands[d], length);
        }
        // Pocket seal: reward a safe move for the enemy segments it walls in beyond what any of our
        // moves would (an enemy already trapped whatever we do earns nothing).
        if (!sealTargets.empty()) {
            int lo = INF;
            for (auto const& c : cands)
                if (!c.fatal)
                    lo = std::min(lo, c.sealed);
            for (auto& c : cands)
                if (!c.fatal && c.safe && c.sealed > lo)
                    c.score += params::SEAL_BONUS * (c.sealed - lo);
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
        // (A dive is unsafe on purpose: going in is the plan, splitting out comes later.)
        if ((!best || (!best->safe && !best->dive)) && canSplit && bodyKnown) {
            // An unsafe move buys time, which is worth a little in case others move away.
            // In the endgame a long dragon (usually the king) is scored by its longest piece:
            // splitting to "save" both halves only shrinks the dragon that decides the game, so
            // moving on counts as keeping all of it for as long as there is room to move.
            bool const keepWhole = params::ENDGAME_KEEP_WHOLE && length >= params::KING_ROOM_LEN &&
                                   (round >= params::CONSOLIDATE_ROUND || growingKing());
            double moveValue = 0.0;
            if (best)
                moveValue = (keepWhole ? 1.0 : 0.25) * best->newLength *
                            std::min(1.0, static_cast<double>(best->space) / best->newLength);
            int bestChild = 0;
            double bestValue = moveValue;
            std::vector<int> sizes{2, 3, length / 4, length / 3, length / 2, 2 * length / 3, 3 * length / 4,
                                   length - 3, length - 2};
            std::sort(sizes.begin(), sizes.end());
            sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
            for (int child : sizes) {
                if (child < 2 || child > length - 2)
                    continue;
                double const v = splitValue(child, keepWhole);
                if (v > bestValue || (v == bestValue && bestChild > 0 && childKnowsBody(child) && !childKnowsBody(bestChild))) {
                    bestValue = v;
                    bestChild = child;
                }
            }
            // Every move is fatal and no split looks safe: split anyway. The rear leaves facing
            // away from the head (out of the dead end we drove into) and lives at least a while.
            if (!best && bestChild == 0 && params::DIVE)
                bestChild = length - 2;
            if (bestChild > 0) {
                split(bestChild);
                ct.set_indicator_string("escape split " + std::to_string(bestChild));
                return;
            }
        }
        // Every move kills us and we don't know our whole body, so the escape split above was
        // skipped: split anyway. Our front is known, and the rear leaves from our old tail.
        if (!best && params::DOOMED_BLIND_SPLIT && canSplit && !bodyKnown && length >= 4) {
            split(length - 2);
            ct.set_indicator_string("blind escape split");
            return;
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
            if (params::DEBUG_SONAR && length >= 8)
                ct.output_log("doomed", length, "body", body.size(), "canSplit", canSplit ? 1 : 0, "units", units,
                              "limit", game.get_unit_limit(), "round", round);
            return;
        }

        if (tryShed(length, bodyKnown, cands))
            return;
        if (tryTailStrike(length, units, bodyKnown))
            return;
        if (tryFeed(length))
            return;

        // Insurance split: a lone dragon dying loses the game, whatever its length.
        if (ct.get_unit_count() < params::MIN_UNITS && length >= params::INSURANCE_MIN_LEN &&
            !(params::FINAL_MERGE && round >= params::FINAL_MERGE_ROUND) &&
            ct.can_split(params::INSURANCE_CHILD) && bodyKnown && best->safe && !enemyNear(head, 3) &&
            splitValue(params::INSURANCE_CHILD) == length) {
            split(params::INSURANCE_CHILD);
            ct.set_indicator_string("insurance split");
            return;
        }

        if (trySwarmSplit(length, units, *best, bodyKnown))
            return;

        move(best->dir);
        if (hunting)
            ct.set_indicator_string("hunt");
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
