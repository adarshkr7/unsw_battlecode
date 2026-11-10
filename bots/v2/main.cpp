// AllRed v9: a swarm that consolidates into its queen.
//
// Dragons 0 and 1 are the queens, and at round 500 the longer queen wins. Each turn: fold
// the 7x7 view into long-lived knowledge (edges, pearl timers, the embedded map if we
// recognised it), then decide, in order:
//   1. strike: a head trade worth taking, by a move or a sprint (both dragons die); the
//      enemy queen is worth any of ours but our own queen, which never trades;
//   2. survive: when every move traps us, split so the most length lives on;
//   3. feed: late in the game a small dragon dies next to our queen, whose pearl-seeking
//      then eats what it drops, so we finish with one long queen;
//   4. swarm: early on, split in half whenever long enough, up to the dragon cap;
//   5. otherwise the best-scoring move: pearls reachable (a belief over every tile,
//      discounted by distance and by who gets there first), room to keep moving
//      (a timed flood fill that knows when every visible body frees up), and the
//      head trades enemies could force on us next turn.

#include "board.hpp"
#include "helper.hpp"
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
    std::vector<int> maxCd; // largest pearl countdown we have seen on each tile (-1: none)
    struct Spot {
        int tile, cd, round;
    };
    std::vector<Spot> spots; // rich pearl tiles we know of (seen, or heard over sonar)
    int foodCursor = 0;
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
    struct Head { int id; int tile; int len; bool exact = true; }; // exact: whole body seen
    std::vector<Head> enemyHeads, allyHeads;
    std::vector<int> threatLen; // shortest enemy able to put its head on this tile next turn (INF: none)
    std::vector<int> threatLenLong; // the same, but an enemy whose tail we can't see may sprint the maximum
    std::vector<int> threatNear;    // big-dragon guard: enemy reach plus a margin (see BIG_GUARD_*)
    bool guardOn = false;

    // Consolidation: the ally we would feed, if any.
    int kingTile = -1, kingLen = 0;
    std::vector<int> kingDist;

    // Hunting: enemy heads we saw recently (a big dragon stays worth chasing for a while).
    struct Sighting { int id; int tile; int len; int round; };
    std::vector<Sighting> sightings;
    std::vector<int> preyDist; // steps to the nearest enemy head worth trading with
    std::vector<int> queenDist; // hunters: steps to where the enemy queen was last seen
    int allyQueenTile = -1;     // our queen's head, when we (not she) can see it
    std::vector<int> allyQueenDist;
    bool queenHunt = false;
    std::vector<int> unseenDist; // our queen: steps from the nearest tile out of view (bodies block)
    std::vector<double> qThreat; // our queen: chance an enemy head can reach the tile this round
    std::vector<int> qMargin;    // our queen: steps to spare before an enemy can strike the tile next round
    std::vector<int> othersDist; // moves any other dragon's head needs to reach a tile (bodies block)
    bool aggressive = false;     // we clearly outnumber the enemies around us
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

    int unitsNow = 1; // our team's dragon count this turn

    // Sonar: what we send this turn (per direction), BODY payloads we received, and the
    // longest dragon of ours we know of (ourselves, a sighting, or a relayed KING record).
    std::array<std::uint64_t, 4> outbox{};
    std::array<bool, 4> outboxSet{};
    std::vector<std::uint64_t> bodyMsgs;
    sonar::King king;
    sonar::EnemyQueen enemyQ; // the latest sighting of the enemy queen we know of (ours or relayed); len 0: dead
    int enemyQLastTile = -1, enemyQLastRound = -1000; // where we ourselves last saw her head
    int enemyQueenId = -1;     // her id (0 or 1), once we have seen her
    int ourQueenSpawn = -1;    // where our queen started (hers is its mirror image)
    bool kingVisible = false; // the dragon we would feed is in view now
    bool ringBusy = false;    // keep outside the feeding ring round the king this turn
    bool kingPull = true;     // head for the king (from KING_PULL_ROUND; not for an early king)

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
        maxCd.assign(N, -1);
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
        threatLenLong.assign(N, INF);
        threatNear.assign(N, INF);
        kingDist.assign(N, INF);
        preyDist.assign(N, INF);
        queenDist.assign(N, INF);
        allyQueenDist.assign(N, INF);
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
        mirrorEdgeKnowledge(e, horizontal);
    }

    // With the symmetry inferred, a wall seen here is also a wall at the mirror image.
    // (Portals are left alone: the mirror portal has its own id.)
    void mirrorEdgeKnowledge(int e, bool horizontal) {
        if (!params::INFER_SYMMETRY || !K.sym)
            return;
        int8_t const kind = horizontal ? K.hKind[e] : K.vKind[e];
        if (kind != EK_KELP && kind != EK_OPEN)
            return;
        int const m = mirrorEdge(e, horizontal);
        if (m < 0)
            return;
        int8_t& slot = horizontal ? K.hKind[m] : K.vKind[m];
        if (slot == EK_UNKNOWN)
            slot = kind;
    }

    void adoptSymmetry(char sym) {
        if (K.sym || !sym)
            return;
        K.sym = sym;
        ct.output_log("symmetry", std::string(1, sym), "round", round);
        for (int e = 0; e < N; e++) {
            mirrorEdgeKnowledge(e, true);
            mirrorEdgeKnowledge(e, false);
        }
        for (auto const& sp : spots)
            applySpot(mirrorOf(sp.tile), sp.cd);
    }

    // Mirrored tiles share one countdown: under the true symmetry every pair we have seen
    // agrees on spawning or not, and on when it next spawns. Adopt the one candidate with
    // enough agreement and no contradiction.
    void inferSymmetry() {
        if (!params::INFER_SYMMETRY || K.sym || round % 5 != 0)
            return;
        char found = 0;
        int nFound = 0;
        for (char cand : {'h', 'v', 'r'}) {
            int agree = 0, disagree = 0;
            for (int t = 0; t < N && disagree == 0; t++) {
                if (lastSeen[t] < 0)
                    continue;
                int const x = t % W, y = t / W;
                int const m = cand == 'h' ? idxOf(W - 1 - x, y) : cand == 'v' ? idxOf(x, H - 1 - y) : idxOf(W - 1 - x, H - 1 - y);
                if (m <= t || lastSeen[m] < 0)
                    continue;
                bool const nt = nextSpawn[t] < 0, nm = nextSpawn[m] < 0;
                if (nt != nm) {
                    disagree++;
                } else if (!nt && std::min(nextSpawn[t], nextSpawn[m]) >= std::max(lastSeen[t], lastSeen[m])) {
                    if (nextSpawn[t] != nextSpawn[m])
                        disagree++;
                    else if (nextSpawn[t] - std::min(lastSeen[t], lastSeen[m]) >= 3)
                        agree++; // a countdown of 3+ matching by chance is unlikely
                }
            }
            if (disagree == 0 && agree >= params::SYM_MIN_AGREE) {
                found = cand;
                nFound++;
            }
        }
        if (nFound == 1)
            adoptSymmetry(found);
    }

    // -- food spots ----------------------------------------------------------------------
    void noteSpot(int t, int cd) {
        for (auto it = spots.begin(); it != spots.end(); ++it) {
            if (it->tile == t) {
                if (cd > params::RICH_CD) {
                    spots.erase(it);
                } else {
                    it->cd = std::max(it->cd, cd);
                    it->round = round;
                }
                return;
            }
        }
        if (cd > params::RICH_CD)
            return;
        spots.push_back({t, cd, round});
        if (spots.size() > 24) { // keep the richest and freshest
            auto worst = std::max_element(spots.begin(), spots.end(), [&](Spot const& a, Spot const& b) {
                return a.cd * 4 + (round - a.round) < b.cd * 4 + (round - b.round);
            });
            spots.erase(worst);
        }
    }

    // A spot heard over sonar: a strong prior for a tile we have never seen ourselves.
    void applySpot(int t, int cd) {
        if (t < 0 || lastSeen[t] >= 0 || K.maxGap[t] >= 0)
            return;
        K.minGap[t] = 1;
        K.maxGap[t] = std::max(1, cd);
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
                if (pt > maxCd[t]) {
                    maxCd[t] = pt;
                    if (params::SHARE_FOOD)
                        noteSpot(t, pt);
                }
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
                (d.team == myTeam ? allyHeads : enemyHeads).push_back({d.id, d.head, d.len(), d.knownLen > 0});
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

    // The child's head (our old tail) has a free tile to move to.
    bool childHasExit(int child) const {
        int const length = static_cast<int>(body.size());
        if (child < 1 || child > length)
            return false;
        int const tail = body[length - 1];
        for (int d = 0; d < 4; d++) {
            int const n = nbr[tail * 4 + d];
            if (n >= 0 && !(inView[n] && occId[n] >= 0) && std::find(body.begin(), body.end(), n) == body.end())
                return true;
        }
        return false;
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
            else if (type == sonar::FOOD && params::SHARE_FOOD)
            {
                sonar::Food const f = sonar::unpackFood(payload);
                if (params::INFER_SYMMETRY && f.sym)
                    adoptSymmetry("?hvr"[f.sym]);
                for (int i = 0; i < 3; i++) {
                    if (f.cls[i] == 0 || f.x[i] >= W || f.y[i] >= H)
                        continue;
                    int const t = idxOf(f.x[i], f.y[i]);
                    int const cd = f.cls[i] * f.cls[i];
                    if (lastSeen[t] < 0) {
                        noteSpot(t, cd);
                        applySpot(t, cd);
                        if (K.sym)
                            applySpot(mirrorOf(t), cd);
                    }
                }
            }
            else if (type == sonar::KING)
            {
                sonar::King const k = sonar::unpackKing(payload);
                considerKing(k, false);
                noteEnemyLen(k.enemyMax, k.round);
            }
            else if (type == sonar::ENEMY_QUEEN)
            {
                sonar::EnemyQueen const q = sonar::unpackEnemyQueen(payload);
                bool const valid = q.round <= round && q.x < W && q.y < H;
                // A death report sticks; a sighting only replaces an older one.
                if (valid && !enemyQueenDead() && (q.len == 0 || q.round > enemyQ.round))
                    enemyQ = q;
            }
        }
    }

    // Keep the longest dragon of ours we know of; a record ages out after KING_STALE rounds
    // unless its dragon (or someone who sees it) refreshes it. Sightings only bound the length.
    void considerKing(sonar::King r, bool lowerBound) {
        if (r.id < 0 || r.round > round || round - r.round > params::KING_STALE)
            return;
        if (r.id == king.id) {
            if (r.round >= king.round) {
                if (lowerBound) {
                    r.len = std::max(r.len, king.len);
                    r.crowded = king.crowded; // a sighting doesn't know; keep what the king said
                }
                king = r;
            }
            return;
        }
        bool const expired = king.id < 0 || round - king.round > params::KING_STALE;
        // Our queen outranks any longer dragon; without word of her (dead, or far away) the
        // longest one we know of is the king, as before.
        int const rankNew = params::QUEEN_KING && isQueenId(r.id) ? 1 : 0;
        int const rankOld = params::QUEEN_KING && isQueenId(king.id) ? 1 : 0;
        if (expired || rankNew > rankOld ||
            (rankNew == rankOld && (r.len > king.len || (r.len == king.len && r.id < king.id))))
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
        sonar::King self{myId, length, head % W, head / W, round};
        if (params::FEED_RING > 0) {
            // As the king: tell the team when a feeder is already next to us, so the rest wait
            // outside instead of crowding us into a corner.
            for (auto const& a : allyHeads) {
                int dx = std::abs(a.tile % W - head % W), dy = std::abs(a.tile / W - head / W);
                // Only a dragon close enough to feed counts (never one waiting at the ring's edge,
                // or nobody would ever go in).
                int const near = std::min(params::FEED_DIST + 1, params::FEED_RING - 1);
                if (std::min(dx, W - dx) + std::min(dy, H - dy) <= near)
                    self.crowded = true;
            }
        }
        considerKing(self, false);
        for (auto const& a : allyHeads)
            considerKing({a.id, a.len, a.tile % W, a.tile / W, round}, true);
        bool queenSeen = false;
        for (auto const& e : enemyHeads) {
            noteEnemyLen(e.len, round);
            if (isQueenId(e.id)) {
                enemyQ = {e.tile % W, e.tile / W, round, std::max(2, e.len)};
                enemyQueenId = e.id;
                enemyQLastTile = e.tile;
                enemyQLastRound = round;
                queenSeen = true;
            }
        }
        // She was in view last round, her head's tile is in view now, and no part of her is
        // anywhere in view: she died (a living queen still covers her old head tile, and her
        // front stays hers when she splits).
        if (!queenSeen && enemyQueenId >= 0 && enemyQLastRound == round - 1 && inView[enemyQLastTile] &&
            occId[enemyQLastTile] != enemyQueenId) {
            bool anyPart = false;
            for (int t = 0; t < N && !anyPart; t++)
                anyPart = inView[t] && occId[t] == enemyQueenId && occTeam[t] != myTeam;
            if (!anyPart)
                enemyQ = {enemyQLastTile % W, enemyQLastTile / W, round, 0};
        }
        // Where our queen started: our own head on round 0, or her first king record.
        if (ourQueenSpawn < 0) {
            if (amQueen() && round <= 1)
                ourQueenSpawn = body.front();
            else if (isQueenId(king.id) && king.round <= 2 && king.round >= 0)
                ourQueenSpawn = idxOf(king.x, king.y);
        }
    }

    bool enemyQueenDead() const { return enemyQ.round >= 0 && enemyQ.len == 0; }

    // A fresh sighting of the enemy queen is worth passing on.
    bool enemyQueenFresh() const {
        if (!params::HUNT_QUEEN || params::QUEEN_SHARE_AGE <= 0 || enemyQ.round < 0)
            return false;
        return round - enemyQ.round <= (enemyQueenDead() ? params::QUEEN_DEAD_SHARE_AGE : params::QUEEN_SHARE_AGE);
    }
    // Half the directions carry it (the other half food or the king), alternating each round.
    bool wantEnemyQueen(int d) const {
        return enemyQueenFresh() && haveBody && nbr[body.front() * 4 + d] != STEP_KELP && (round + d) % 2 == 1;
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
    bool wantFood(int d) const {
        if (!params::SHARE_FOOD || round >= params::KING_PULL_ROUND || !haveBody || nbr[body.front() * 4 + d] == STEP_KELP)
            return false;
        if (spots.empty() && !(params::INFER_SYMMETRY && K.sym))
            return false;
        return (round + d) % 2 == 0;
    }

    std::uint64_t foodMessage() {
        sonar::Food f;
        f.sym = params::INFER_SYMMETRY ? (K.sym == 'h' ? 1 : K.sym == 'v' ? 2 : K.sym == 'r' ? 3 : 0) : 0;
        std::erase_if(spots, [&](Spot const& sp) { return round - sp.round > params::FOOD_STALE; });
        std::vector<Spot> best(spots);
        std::sort(best.begin(), best.end(), [](Spot const& a, Spot const& b) { return a.cd < b.cd; });
        int const n = static_cast<int>(std::min<size_t>(best.size(), 9));
        for (int i = 0; i < 3 && i < n; i++) {
            Spot const& sp = best[(foodCursor + i) % n];
            f.x[i] = sp.tile % W;
            f.y[i] = sp.tile / W;
            f.cls[i] = std::clamp(static_cast<int>(std::ceil(std::sqrt(std::max(1, sp.cd)))), 1, 15);
        }
        foodCursor += 3;
        return sonar::packFood(f);
    }

    void sendSonar() {
        if (!params::SONAR)
            return;
        bool const haveKing = king.id >= 0 && round - king.round <= params::KING_STALE;
        for (int d = 0; d < 4; d++) {
            if (outboxSet[d])
                ct.send_sonar(Direction(DCH[d]), outbox[d]);
            else if (wantFood(d))
                ct.send_sonar(Direction(DCH[d]), foodMessage());
            else if (wantEnemyQueen(d))
                ct.send_sonar(Direction(DCH[d]), sonar::packEnemyQueen(enemyQ));
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
        double score = -1e18;
        std::vector<int> steps, tiles; // a multi-step move (free steps): directions, head tiles
    };

    // A move of one step (c.dir), or of several (c.steps, every tile in view, no sprint cost).
    void evaluate(Candidate& c, int length) {
        int const head = body.front();
        // Our body after the move, head first.
        std::deque<int> post;
        double score = 0.0;
        int eaten = 0;
        if (c.steps.size() >= 2) {
            post = body;
            c.tiles.clear();
            for (int d : c.steps) {
                int const n = nbr[post.front() * 4 + d];
                // Other dragons stand still during our turn; our own tail moves on each step,
                // but entering the tile it still holds is fatal.
                if (n < 0 || !inView[n] || (occId[n] >= 0 && occId[n] != myId) ||
                    std::find(post.begin(), post.end(), n) != post.end()) {
                    c.fatal = true;
                    return;
                }
                bool const eat = pearlNow[n] && std::find(c.tiles.begin(), c.tiles.end(), n) == c.tiles.end();
                c.tiles.push_back(n);
                post.push_front(n);
                if (eat) {
                    eaten++;
                    score += kingsFood(n) || queenFood(n) ? -params::KING_FOOD_PENALTY : params::EAT_NOW * queenFoodWeight();
                } else {
                    post.pop_back();
                }
            }
            c.dir = c.steps.front();
        } else {
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
            bool const eat = inView[dest] && pearlNow[dest];
            if (eat) {
                eaten = 1;
                score += kingsFood(dest) || queenFood(dest) ? -params::KING_FOOD_PENALTY : params::EAT_NOW * queenFoodWeight();
            }
            // (The part of our body we know: all of it, or the front of an unknown one.)
            int const keep = std::min<int>(static_cast<int>(body.size()), length + eaten - 1);
            post.assign(body.begin(), body.begin() + keep);
            post.push_front(dest);
        }
        int const dest = post.front();
        c.dest = dest;
        c.eats = eaten > 0;
        if (!inView[dest])
            score -= length <= params::DIVE_MAX_LEN ? params::OUT_OF_VIEW_SMALL : params::OUT_OF_VIEW;
        score -= params::REVISIT * heat(dest);
        // Spread out (Cutlery's strongest move term): keep away from ally heads in the swarm phase.
        if (params::SPREAD_WEIGHT > 0.0 && round < params::CONSOLIDATE_ROUND) {
            int nearest = 99;
            for (auto const& a : allyHeads) {
                int dx = std::abs(a.tile % W - dest % W), dy = std::abs(a.tile / W - dest / W);
                nearest = std::min(nearest, std::min(dx, W - dx) + std::min(dy, H - dy));
            }
            score += params::SPREAD_WEIGHT * std::min(nearest, 4);
            if (nearest <= 1)
                score -= params::SPREAD_ADJ;
        }
        // Keep spawn tiles clear: a pearl only spawns on an empty tile (Cutlery).
        if (params::SPAWN_CLEAR > 0.0 && inView[dest] && !pearlNow[dest] && nextSpawn[dest] >= 0 &&
            nextSpawn[dest] - round <= 2)
            score -= params::SPAWN_CLEAR;

        // When each tile of our body after the move frees up.
        int const newLength = length + eaten;
        c.newLength = newLength;
        bool const kingRoomVsEnemies = params::ENDGAME_KEEP_WHOLE && bigKing(length);
        // Index from the tail j frees for move j+2 (collision is checked before the tail moves).
        ++ownScenario;
        for (int j = 1; j < static_cast<int>(post.size()); j++) {
            raiseBlock(post[j], newLength - j + 1);
            ownStamp[post[j]] = ownScenario;
        }
        raiseBlock(dest, INF);
        bool reachedOwn = false;

        // Time-aware BFS from dest, scoring pearls as we go. Every reachable tile counts for
        // pearls and for reaching our own tail; only tiles we reach no later than any other
        // head (and through such tiles) count as our room.
        newSearch();
        int qh = 0, qt = 0, room = 1;
        int near = 1; // tiles within 3 steps: room to dodge
        int const maxDepth = std::max(params::EVAL_MAX_DEPTH, newLength + 8);
        // Our queen next round: tiles her free steps reach that no enemy can strike by then.
        bool const lookAhead = params::QUEEN_CORNERED > 0.0 && params::QUEEN_SAFE && amQueen() && !qMargin.empty();
        int const nextFree = (length + eaten + 3) / 4;
        int escapes = 0;
        visit(dest, 0);
        owned[dest] = 1;
        queue[qt++] = dest;
        double best = 0.0, sum = 0.0;
        double pocketFood = eaten; // expected pearls we can reach, undiscounted
        while (qh < qt) {
            int const t = queue[qh++];
            int const k = dist[t];
            if (t != dest && !(inView[t] && occId[t] >= 0)) {
                double v = kingsFood(t) || queenFood(t) ? 0.0 : pearlBelief(t, round + 1 + k);
                if (v > 0.0) {
                    int const moves = 1 + k;
                    if (distE[t] < moves)
                        v *= params::ENEMY_CLOSER;
                    // The king takes what is dropped for it, however many feeders are nearer.
                    if (!amKing() && !(params::QUEEN_FOOD_RADIUS >= 0 && amQueen()) &&
                        (distA[t] < moves || (distA[t] == moves && ownerA[t] < myId)))
                        v *= params::ALLY_CLOSER;
                    best = std::max(best, v * powBest[k]);
                    sum += v * powSum[k];
                    pocketFood += v;
                }
            }
            if (k >= maxDepth)
                continue;
            for (int d = 0; d < 4; d++) {
                int const n = nbr[t * 4 + d];
                if (n < 0 || seen[n] == stamp || block[n] > k + 1)
                    continue;
                if (ownStamp[n] == ownScenario)
                    reachedOwn = true;
                visit(n, k + 1);
                if (k + 1 <= 3)
                    near++;
                if (lookAhead && k + 1 <= nextFree && qMargin[n] > 0)
                    escapes++;
                // We are on move, so ties with another head are ours.
                // The endgame king counts only enemy heads as rivals for room: the feeders
                // crowding around it are coming to die for it, not to take its space.
                int const rival = kingRoomVsEnemies ? distE[n] : othersDist[n];
                owned[n] = owned[t] && (!params::VORONOI || rival >= k + 2);
                room += owned[n];
                queue[qt++] = n;
            }
        }
        restoreBlock();
        c.space = room;
        c.safe = reachedOwn || room >= roomyEnough(newLength);
        score += queenFoodWeight() * (params::FOOD_BEST * best + params::FOOD_SUM * sum);

        // Diving: most pearls on maze maps spawn in 1-wide lanes and dead ends, which are never
        // "safe". Going in is a priced risk rather than a death sentence when the pocket has food:
        //  - if eating it makes us 4+ long, we can split our way out later (the child takes our
        //    tail and faces out of the pocket), losing only the parent we leave behind;
        //  - a small dragon may die in there while we have dragons to spare: it eats first, and
        //    half its body comes back as pearls for the ally that follows it in.
        // Only where kelp makes the pocket: when other dragons are what boxes us in, the old
        // rule (avoid, or split out) stays.
        // Never our queen: she keeps the head, so she is the part left behind in the pocket.
        bool const queenSafe = params::QUEEN_SAFE && amQueen();
        if (!c.safe && params::DIVE && !queenSafe && newLength <= params::DIVE_MAX_LEN && pocketFood >= params::DIVE_MIN_FOOD &&
            (!params::DIVE_WALLS_ONLY || wallRoom(dest, roomyEnough(newLength)) < roomyEnough(newLength))) {
            bool const canSplitLater = unitsNow < game.get_unit_limit();
            if (canSplitLater && newLength + static_cast<int>(pocketFood) >= params::DIVE_ESCAPE_LEN) {
                c.dive = true;
                score -= std::max(0.0, params::DIVE_ESCAPE_COST - params::DIVE_RICH_DISCOUNT * (pocketFood - 1.0));
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
        bool const bigNow = bigKing(newLength);
        int const tl = bigNow ? threatLenLong[dest] : threatLen[dest];
        if (queenSafe) {
            // Any enemy head on our queen's is the game: no trade is even for her.
            score -= params::QUEEN_THREAT * qThreat[dest] * (enemyQueenDead() ? params::QUEEN_ENEMY_DEAD_SAFE : 1.0);
            if (near < params::QUEEN_FREEDOM_CAP)
                score -= params::QUEEN_FREEDOM * (params::QUEEN_FREEDOM_CAP - near);
            // Cornered next round: no tile to flee to that an enemy cannot strike.
            if (lookAhead && qMargin[dest] <= 2 && escapes < params::QUEEN_ESCAPES)
                score -= params::QUEEN_CORNERED * (params::QUEEN_ESCAPES - escapes) / params::QUEEN_ESCAPES;
            if (distE[dest] < params::QUEEN_KEEPAWAY_DIST)
                score -= params::QUEEN_KEEPAWAY * (params::QUEEN_KEEPAWAY_DIST - distE[dest]);
            if (unseenDist[dest] < 3)
                score -= params::QUEEN_UNSEEN * (3 - unseenDist[dest]);
        } else if (tl < INF) {
            int const disadvantage = newLength - tl;
            double scale = defensive ? params::ADAPT_THREAT_SCALE : 1.0;
            if (race != 0 && king.id == myId)
                scale *= race > 0 ? params::RACE_KING_SAFE : params::RACE_KING_BOLD;
            // Endgame: a big dragon is what decides the game, and small enemies hunt it (about 3
            // of ours of 10+ were killed head-on per game after round 380). Dodge harder.
            if (round >= params::CONSOLIDATE_ROUND && newLength >= params::KING_ROOM_LEN)
                scale *= params::BIG_THREAT_SCALE;
            score -= scale * (disadvantage > 0 ? params::THREAT_PER_LEN * disadvantage : params::THREAT_EVEN);
        }
        if (guardOn && threatNear[dest] < newLength)
            score -= params::BIG_GUARD_PER_LEN * (newLength - threatNear[dest]);
        if (params::KEEPAWAY && round >= params::KEEPAWAY_ROUND && newLength >= params::KING_ROOM_LEN &&
            distE[head] <= 6) {
            if (distE[dest] <= params::KEEPAWAY_DIST)
                score -= params::KEEPAWAY_W * (params::KEEPAWAY_DIST + 1 - distE[dest]);
            if (distA[dest] < 6)
                score += params::KEEPAWAY_ALLY * (6 - distA[dest]);
        }
        if (kingPull && kingTile >= 0 && kingDist[dest] < INF) {
            // While the ring is busy, head for its edge and wait there.
            int const floor = ringBusy ? params::FEED_RING + 1 : 0;
            int const dd = std::max(kingDist[dest], floor);
            score += params::FEED_ATTRACT / (1.0 + dd);
            // From afar the pull above is flat: reward every step that closes the distance.
            if (kingDist[head] < INF)
                score += params::KING_APPROACH * (length >= params::MERGE_MIN_LEN ? params::MERGE_APPROACH_MULT : 1.0) *
                         (std::max(kingDist[head], floor) - dd);
            if (ringBusy && kingDist[dest] <= params::FEED_RING)
                score -= params::RING_ENTER_PENALTY;
        }
        if (preyGain > 0.0 && preyDist[dest] < INF)
            score += (aggressive ? params::HUNT_ATTRACT_AGGRO : params::HUNT_ATTRACT) *
                     std::min(preyGain, params::HUNT_GAIN_CAP) / (1.0 + preyDist[dest]);
        if (queenHunt && queenDist[head] < INF && queenDist[dest] < INF)
            score += params::QUEEN_HUNT_APPROACH * (queenDist[head] - queenDist[dest]);
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

    // Queens: ids 0 and 1, one per team (the team tells ours from theirs).
    bool amQueen() const { return myId <= 1; }
    static bool isQueenId(int id) { return id >= 0 && id <= 1; }
    // Steps a dragon of `len` can move in one turn before the next would kill it.
    static int sprintReach(int len) {
        return params::NEW_SPRINT ? (len + 3) / 4 + std::max(0, len - 2) : len - 1;
    }

    // Endgame with sonar: we are the king everyone is feeding.
    bool amKing() const {
        return (params::SONAR && round >= params::KING_PULL_ROUND && king.id == myId) || iAmEarlyKing();
    }
    bool earlyKingPhase() const {
        return params::EARLY_KING && params::SONAR && round >= params::EARLY_KING_ROUND &&
               W * H >= params::EARLY_KING_MIN_AREA;
    }
    bool iAmEarlyKing() const { return earlyKingPhase() && king.id == myId; }
    // Endgame (or early) king: protected, scored by its longest piece.
    bool bigKing(int length) const {
        return (round >= params::CONSOLIDATE_ROUND || iAmEarlyKing()) && length >= params::KING_ROOM_LEN;
    }
    // A pearl next to the king we feed is its food: leave it for it rather than eat it and
    // pass on only half when we feed in turn.
    // Pearls near our queen (in view) are hers, all game: her length is our score.
    bool queenFood(int t) const { return allyQueenTile >= 0 && allyQueenDist[t] <= params::QUEEN_FOOD_RADIUS; }
    // Our queen's length is our score: her pearls count for more.
    double queenFoodWeight() const { return amQueen() ? params::QUEEN_FOOD_WEIGHT : 1.0; }
    // Mid-game feeding: while we have dragons to spare, a small dragon next to our queen dies
    // for her (she eats what it drops), so she grows long early.
    bool queenMidFeed(int length, int units) const {
        return round >= params::QUEEN_MIDFEED_ROUND && !amQueen() && length <= params::QUEEN_MIDFEED_MAX_LEN &&
               units >= params::QUEEN_MIDFEED_MIN_UNITS && allyQueenTile >= 0;
    }
    bool kingsFood(int t) const {
        return kingVisible && (round >= params::FEED_ROUND || earlyKingPhase()) &&
               kingDist[t] <= params::KING_FOOD_RADIUS;
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
    int splitValue(int child, bool longest = false, int* parentOut = nullptr, int* childOut = nullptr) {
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
        // The child moves later this round, so its tail is gone before our next move.
        for (int i = parentLen; i < length; i++)
            raiseBlock(body[i], (i - parentLen) + (params::SPLIT_CHILD_EARLY ? 1 : 2));
        raiseBlock(body[0], INF);
        bool const parentSafe = floodSafe(body[0], parentLen);
        restoreBlock();

        int const childPart = childSafe ? child : chained, parentPart = parentSafe ? parentLen : 0;
        if (parentOut)
            *parentOut = parentPart;
        if (childOut)
            *childOut = childPart;
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
    // (see sprintReach, through tiles that are free now).
    void computeThreats() {
        threatsInto(threatLen, false);
        // Our big dragons in the endgame: most were killed head-on by enemies in plain view
        // sprinting 3-6 steps, longer than their visible length suggested. Our queen likewise.
        if (params::QUEEN_SAFE && amQueen())
            computeQueenThreat();
        if (params::BIG_ASSUME_LONG && round >= params::CONSOLIDATE_ROUND && ct.get_length() >= params::KING_ROOM_LEN - 1)
            threatsInto(threatLenLong, true);
        else
            threatLenLong = threatLen;
        guardOn = params::BIG_GUARD_PER_LEN > 0.0 && round >= params::BIG_GUARD_ROUND &&
                  ct.get_length() >= params::KING_ROOM_LEN - 1;
        if (guardOn)
            threatsInto(threatNear, true, params::BIG_GUARD_MARGIN);
    }

    // Chance that an enemy head `steps` away can get there this turn, from the length we see.
    static double reachChance(int steps, int len, bool exact) {
        if (sprintReach(len) >= steps)
            return 1.0;
        if (exact)
            return 0.0;
        double p = 1.0;
        for (int extra = 1; extra <= 6; extra++) {
            p *= params::QUEEN_LONGER_P;
            if (sprintReach(len + extra) >= steps)
                return p;
        }
        return 0.0;
    }

    // Our queen: for every tile, the chance an enemy head can be there before her next turn.
    void computeQueenThreat() {
        qThreat.assign(N, 0.0);
        struct Source { int tile, len, age; bool exact; };
        std::vector<Source> sources;
        for (auto const& e : enemyHeads)
            sources.push_back({e.tile, e.len, 0, e.exact});
        for (auto const& sg : sightings) {
            bool const current = std::any_of(enemyHeads.begin(), enemyHeads.end(), [&](Head const& e) { return e.id == sg.id; });
            int const age = round - sg.round;
            if (!current && age >= 1 && age <= params::QUEEN_STALE_AGE && !inView[sg.tile])
                sources.push_back({sg.tile, sg.len, age, false});
        }
        qMargin.assign(N, INF);
        for (auto const& s : sources) {
            double const factor = s.age > 0 ? params::QUEEN_STALE_P : 1.0;
            // Next round it may first step its free steps towards her, then strike.
            int const nextReach = (s.len + 3) / 4 + std::min(sprintReach(s.len), params::STRIKE_MAX_STEPS) - s.age;
            int const radius = std::max(params::STRIKE_MAX_STEPS + s.age, params::QUEEN_CORNERED > 0.0 ? nextReach + 4 : 0);
            newSearch();
            int qh = 0, qt = 0;
            visit(s.tile, 0);
            queue[qt++] = s.tile;
            qMargin[s.tile] = std::min(qMargin[s.tile], -nextReach);
            while (qh < qt) {
                int const t = queue[qh++];
                int const k = dist[t];
                if (k >= radius)
                    continue;
                for (int d = 0; d < 4; d++) {
                    int const n = nbr[t * 4 + d];
                    if (n < 0 || seen[n] == stamp)
                        continue;
                    if (inView[n] && occId[n] >= 0 && !(params::OWN_BODY_OPEN && occId[n] == myId))
                        continue; // other bodies block
                    visit(n, k + 1);
                    queue[qt++] = n;
                    qMargin[n] = std::min(qMargin[n], k + 1 - nextReach);
                    if (k + 1 > params::STRIKE_MAX_STEPS + s.age)
                        continue;
                    double const p = factor * reachChance(std::max(1, k + 1 - s.age), s.len, s.exact);
                    qThreat[n] = std::max(qThreat[n], p);
                }
            }
        }
    }

    // How many steps an enemy just out of view would need to reach each tile in view.
    void computeUnseen() {
        unseenDist.assign(N, INF);
        int qh = 0, qt = 0;
        for (int t = 0; t < N; t++) {
            if (inView[t])
                continue;
            unseenDist[t] = 0;
            for (int d = 0; d < 4; d++) {
                int const n = nbr[t * 4 + d];
                if (n >= 0 && inView[n]) {
                    queue[qt++] = t;
                    break;
                }
            }
        }
        while (qh < qt) {
            int const t = queue[qh++];
            for (int d = 0; d < 4; d++) {
                int const n = nbr[t * 4 + d];
                if (n < 0 || !inView[n] || unseenDist[n] != INF)
                    continue;
                unseenDist[n] = unseenDist[t] + 1;
                if (occId[n] < 0 || occId[n] == myId) // it can end on a head, not pass through another body
                    queue[qt++] = n;
            }
        }
    }

    void threatsInto(std::vector<int>& out, bool assumeLong, int extra = 0) {
        std::vector<int>& threatLen = out;
        std::fill(threatLen.begin(), threatLen.end(), INF);
        for (auto const& e : enemyHeads) {
            int const reach = extra + (assumeLong && !e.exact ? params::STRIKE_MAX_STEPS
                                                              : std::clamp(sprintReach(e.len), 1, params::STRIKE_MAX_STEPS));
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
                    if (n < 0 || seen[n] == stamp)
                        continue;
                    // Our own body is moving away (and where we move to was often our body):
                    // only other dragons' bodies block.
                    if (inView[n] && occId[n] >= 0 && !(params::OWN_BODY_OPEN && occId[n] == myId)) {
                        // It cannot pass through a body, but it can end on one of our heads (a trade).
                        if (params::THREAT_ON_OUR_HEADS && occHead[n] && occTeam[n] == myTeam)
                            threatLen[n] = std::min(threatLen[n], e.len);
                        continue;
                    }
                    visit(n, k + 1);
                    queue[qt++] = n;
                    threatLen[n] = std::min(threatLen[n], e.len);
                }
            }
        }
    }

    // Interceptors: an enemy head at tile t, of length elen, close to a big ally of ours.
    // Guards: an enemy head at tile t close to our queen (seen, or her fresh king record).
    bool threatensQueen(int t) const {
        int const tx = t % W, ty = t / W;
        auto close = [&](int x, int y) {
            int dx = std::abs(x - tx), dy = std::abs(y - ty);
            return std::min(dx, W - dx) + std::min(dy, H - dy) <= params::QUEEN_GUARD_RADIUS;
        };
        for (auto const& a : allyHeads)
            if (isQueenId(a.id) && close(a.tile % W, a.tile / W))
                return true;
        return isQueenId(king.id) && king.id != myId && round - king.round <= 2 && close(king.x, king.y);
    }

    bool threatensBig(int t, int elen) const {
        int const tx = t % W, ty = t / W;
        auto close = [&](int x, int y) {
            int dx = std::abs(x - tx), dy = std::abs(y - ty);
            return std::min(dx, W - dx) + std::min(dy, H - dy) <= params::INTERCEPT_RADIUS;
        };
        for (auto const& a : allyHeads)
            if (a.len >= params::INTERCEPT_MIN_KING && a.len > elen && close(a.tile % W, a.tile / W))
                return true;
        return king.id >= 0 && king.id != myId && round - king.round <= 2 && king.len >= params::INTERCEPT_MIN_KING &&
               king.len > elen && close(king.x, king.y);
    }

    double interceptGain(int t, int elen, int length, int units) const {
        if (params::QUEEN_GUARD && !amQueen() && length <= params::QUEEN_GUARD_MAX_LEN &&
            units >= params::QUEEN_GUARD_MIN_UNITS && threatensQueen(t))
            return params::QUEEN_GUARD_GAIN;
        if (!params::INTERCEPT || length > params::INTERCEPT_MAX_LEN || units < params::INTERCEPT_MIN_UNITS)
            return 0.0;
        return threatensBig(t, elen) ? params::INTERCEPT_GAIN : 0.0;
    }

    // What a head trade is worth to us: positive means take it.
    double tradeGain(int enemyLen, int myLen, int units, bool enemyQueen = false) const {
        if (params::QUEEN_SAFE && amQueen())
            return -1.0; // our queen's length is our score
        if (units <= 1)
            return -1.0; // our last dragon: trading it loses the game
        // Their queen dead is 0 long at round 500: worth any dragon of ours but the queen.
        if (params::HUNT_QUEEN && enemyQueen)
            return params::QUEEN_KILL_GAIN;
        int const gain = enemyLen - myLen;
        if (gain > 0)
            return gain;
        // Their queen is dead: we win at round 500 if ours lives and we are not wiped out, so
        // trade nothing away evenly.
        if (params::QUEEN_ENEMY_DEAD_SAFE > 1.0 && enemyQueenDead())
            return -1.0;
        // Even trades thin both swarms; worth it while we have dragons to spare, or when we
        // outnumber the enemies around us (their losses hurt them more).
        if (gain == 0 && (aggressive || (!defensive && round < params::CONSOLIDATE_ROUND && units >= params::TRADE_MIN_UNITS)))
            return aggressive ? 1.0 : 0.5;
        // Top teams' small dragons ram any head at least as long as themselves.
        if (params::KAMIKAZE && gain == 0 && myLen <= 3 && units >= params::KAMIKAZE_MIN_UNITS)
            return 0.5;
        return -1.0;
    }

    // Reach an enemy head this turn, by one move or a sprint through free tiles in view.
    bool tryStrike(int length, int units) {
        bool queenInView = false;
        for (auto const& e : enemyHeads)
            queenInView = queenInView || isQueenId(e.id);
        int const cap = params::HUNT_QUEEN && queenInView ? std::max(params::STRIKE_MAX_STEPS, params::QUEEN_STRIKE_MAX)
                                                          : params::STRIKE_MAX_STEPS;
        int const maxSteps = std::min(sprintReach(length), cap);
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
                    bool const queen = isQueenId(occId[n]);
                    // Long sprints only for the enemy queen.
                    if (occHead[n] && occTeam[n] != myTeam && (queen || k + 1 <= params::STRIKE_MAX_STEPS)) {
                        double const g = std::max(tradeGain(lengthOf(occId[n]), length, units, queen),
                                                  interceptGain(n, lengthOf(occId[n]), length, units));
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
            int const memory = params::HUNT_QUEEN && isQueenId(sg.id) ? params::QUEEN_HUNT_MEMORY : params::HUNT_MEMORY;
            return round - sg.round > memory || (sg.round < round && inView[sg.tile]);
        });
        preyGain = 0.0;
        std::vector<Head> prey;
        for (auto const& sg : sightings) {
            double const g = std::max(tradeGain(sg.len, length, units, isQueenId(sg.id)),
                                      interceptGain(sg.tile, sg.len, length, units));
            if (g <= 0.0)
                continue;
            prey.push_back({sg.id, sg.tile, sg.len});
            preyGain = std::max(preyGain, g);
        }
        if (prey.empty())
            std::fill(preyDist.begin(), preyDist.end(), INF);
        else
            bfsFrom(prey, preyDist, nullptr);

        // Hunters close in on the enemy queen from afar: where we, or a relay, last saw her.
        queenHunt = false;
        bool const hunter = params::HUNT_QUEEN && params::QUEEN_HUNT_APPROACH > 0.0 && !amQueen() &&
                            length <= params::QUEEN_HUNTER_MAX_LEN && units >= params::QUEEN_HUNT_MIN_UNITS &&
                            round < params::FEED_ROUND && myId % params::QUEEN_HUNT_SHARE == 0;
        int target = -1;
        if (hunter && !enemyQueenDead()) {
            int const qt = idxOf(enemyQ.x, enemyQ.y);
            if (enemyQ.round >= 0 && round - enemyQ.round <= params::QUEEN_HUNT_MEMORY && !(enemyQ.round < round && inView[qt]))
                target = qt;
            // Not seen lately: where she started, the mirror image of our queen's start.
            else if (params::QUEEN_HUNT_SPAWN && enemyQ.round < 0 && ourQueenSpawn >= 0 && mirrorOf(ourQueenSpawn) >= 0 &&
                     !inView[mirrorOf(ourQueenSpawn)])
                target = mirrorOf(ourQueenSpawn);
        }
        if (target >= 0) {
            queenHunt = true;
            bfsFrom({{-1, target, 2}}, queenDist, nullptr);
            // Only hunters already near her chase: a trek across the map costs pearls and dragons.
            if (params::QUEEN_HUNT_RADIUS > 0 && queenDist[body.front()] > params::QUEEN_HUNT_RADIUS)
                queenHunt = false;
        }
    }

    // Late game: the longest ally in view that we should feed, if any.
    void chooseKing(int length, int units) {
        if (queenMidFeed(length, units) && round < params::KING_PULL_ROUND) {
            kingTile = allyQueenTile;
            kingLen = 0;
            kingVisible = true;
            kingPull = false; // no pull from afar: only those already next to her
            ringBusy = false;
            kingDist = allyQueenDist;
            return;
        }
        kingTile = -1;
        kingLen = 0;
        kingVisible = false;
        kingPull = true;
        ringBusy = false;
        // Our queen feeds nobody: her length is our score.
        if (params::QUEEN_KING && amQueen())
            return;
        // Late in the game the last two dragons may merge too (we lost 10-11 and 17-18 holding
        // two that never could); earlier, keep a spare against elimination.
        int const keep = params::FINAL_MERGE && round >= params::FINAL_MERGE_ROUND ? 1 : params::FEED_KEEP_UNITS;
        if (units <= keep)
            return;
        // With sonar the whole team agrees on one king: the longest dragon anyone has heard of.
        // Head for it from KING_PULL_ROUND, feed it once it is in view.
        if (params::SONAR && king.id >= 0 && round - king.round <= params::KING_STALE) {
            if (king.id == myId)
                return;
            if (round < params::KING_PULL_ROUND) {
                // Early king: no pull yet, just leave the food around it (when we see it) alone.
                if (!earlyKingPhase())
                    return;
                // With dragons to spare, small ones head for it (to feed it) as in the endgame.
                bool const pull = round >= params::EARLY_FEED_ROUND && length <= params::EARLY_FEED_MAX_LEN &&
                                  units >= params::EARLY_PULL_MIN_UNITS;
                if (!pull) {
                    kingPull = false;
                    for (auto const& a : allyHeads) {
                        if (a.id == king.id) {
                            kingTile = a.tile;
                            kingLen = a.len;
                            kingVisible = true;
                        }
                    }
                    if (kingVisible)
                        bfsFrom({{-1, kingTile, kingLen}}, kingDist, nullptr);
                    return;
                }
            }
            kingTile = idxOf(king.x, king.y);
            kingLen = king.len;
            for (auto const& a : allyHeads) {
                if (a.id == king.id) {
                    kingTile = a.tile;
                    kingVisible = true;
                }
            }
            bfsFrom({{-1, kingTile, kingLen}}, kingDist, nullptr);
            // Our queen sealed off (Schooltime boxes queens in kelp): nobody can feed her, and
            // with queens tied the longest dragon decides. Feed the biggest ally in view instead.
            if (params::QUEEN_SEALED_FALLBACK && isQueenId(king.id) && kingDist[body.front()] == INF &&
                round >= params::FEED_ROUND) {
                kingTile = -1;
                kingLen = 0;
                kingVisible = false;
                biggerAllyKing(length);
                return;
            }
            // Feeding ring: from outside it, wait while someone is already feeding (the king
            // says so over sonar, or we see them), and everyone waits before feeding starts.
            if (params::FEED_RING > 0 && kingDist[body.front()] > params::FEED_RING) {
                ringBusy = round < params::CONSOLIDATE_ROUND ||
                           (king.crowded && round - king.round <= params::RING_FRESH);
                for (auto const& a : allyHeads)
                    if (a.id != king.id && kingDist[a.tile] <= params::FEED_RING)
                        ringBusy = true;
            }
            return;
        }
        if (round < params::CONSOLIDATE_ROUND)
            return;
        biggerAllyKing(length);
    }

    // Any ally in view bigger than us (ties to the lower id) is a king: feeding it makes it bigger
    // still, so feeders converge on one dragon instead of all waiting for a big one. Our queen
    // in view comes first (unless she is the one we cannot reach).
    void biggerAllyKing(int length) {
        bool const skipQueen = params::QUEEN_SEALED_FALLBACK && isQueenId(king.id) && round - king.round <= params::KING_STALE;
        int kingId = myId;
        for (auto const& a : allyHeads) {
            // Our queen in view is the king, however short.
            if (params::QUEEN_KING && isQueenId(a.id) && !skipQueen) {
                kingTile = a.tile;
                kingLen = a.len;
                kingId = a.id;
                break;
            }
            bool const bigger = a.len > length || (a.len == length && a.id < myId);
            bool const better = a.len > kingLen || (a.len == kingLen && a.id < kingId);
            if (bigger && better && a.len >= length + params::FEED_MARGIN) {
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

    // Local recycling (all three top teams): after the switch a small dragon next to a much
    // longer ally dies in front of it, king or not.
    bool tryRecycle(int length, int units) {
        if (!params::LOCAL_RECYCLE || round < params::CONSOLIDATE_ROUND || length > 3 || units <= params::FEED_KEEP_UNITS ||
            (params::QUEEN_KING && amQueen()))
            return false;
        int const head = body.front();
        for (auto const& a : allyHeads) {
            if (a.len < length + params::RECYCLE_MIN_GAIN)
                continue;
            int dx = std::abs(a.tile % W - head % W), dy = std::abs(a.tile / W - head / W);
            if (std::min(dx, W - dx) + std::min(dy, H - dy) > 2)
                continue;
            if (std::uniform_real_distribution<double>(0.0, 1.0)(rng) >= params::RECYCLE_PROB)
                return false;
            ct.do_split(1); // an illegal split: we die, dropping pearls in front of the ally
            plannedPath.clear();
            ct.set_indicator_string("recycle");
            return true;
        }
        return false;
    }

    // Die next to the king: our body becomes pearls right where it is about to go.
    bool tryFeed(int length) {
        if (kingTile < 0 || !kingVisible || kingDist[body.front()] > params::FEED_DIST)
            return false;
        // Early king at the dragon cap: our pearls can't become a new dragon anyway.
        bool const capFeed = params::EARLY_KING_FEED_AT_CAP && earlyKingPhase() && length <= 3 &&
                             unitsNow >= game.get_unit_limit() - 1 && kingLen >= length + params::RECYCLE_MIN_GAIN;
        bool const midFeed = earlyKingPhase() && round >= params::EARLY_FEED_ROUND &&
                             length <= params::EARLY_FEED_MAX_LEN && unitsNow >= params::EARLY_FEED_MIN_UNITS &&
                             kingLen >= length + params::RECYCLE_MIN_GAIN;
        if (round < params::FEED_ROUND && !capFeed && !midFeed && !queenMidFeed(length, unitsNow))
            return false;
        ct.do_split(1); // an illegal split: we die with no valid action
        plannedPath.clear();
        ct.set_indicator_string("feed");
        return true;
    }

    // Swarm phase: split in half whenever long enough and both halves are safe.
    bool trySwarmSplit(int length, int units, Candidate const& best, bool bodyKnown) {
        if (round >= params::CONSOLIDATE_ROUND || length < params::SWARM_SPLIT_LEN || !bodyKnown ||
            units >= game.get_unit_limit() - (amQueen() ? 0 : params::QUEEN_SPARE_SLOTS) ||
            (best.eats && !params::SPLIT_WHILE_EATING))
            return false;
        // The parent stands still for a turn, so not where an enemy head can reach it.
        if (threatLen[body.front()] < INF)
            return false;
        // Early king (sparring style): from KING_GROW_ROUND the king stops halving itself.
        if (params::SONAR && round >= params::KING_GROW_ROUND && king.id == myId)
            return false;
        if (iAmEarlyKing())
            return false;
        if (amQueen() && (round >= params::QUEEN_SPLIT_UNTIL || length > params::QUEEN_SPLIT_MAX_LEN)) {
            // Budding: a long queen seeds the swarm with a small child and keeps growing.
            int const child = params::QUEEN_BUD_CHILD;
            if (params::QUEEN_BUD_LEN <= 0 || round >= params::QUEEN_BUD_UNTIL || length < params::QUEEN_BUD_LEN ||
                !ct.can_split(child) || splitValue(child) != length ||
                (params::SONAR && params::SPLIT_WAITS_FOR_HANDOFF && !childKnowsBody(child)))
                return false;
            split(child);
            ct.set_indicator_string("bud");
            return true;
        }
        // Halves, or one off, preferring a cut the child's body message can reach. A child that
        // could neither see its whole body nor be told it waits: next turn the body may be straight.
        int child = 0;
        for (int c : {length / 2, length / 2 + 1, length / 2 - 1}) {
            bool const halvesOk = params::SPLIT_RELAXED ? splitValue(c) >= length - c && childHasExit(c)
                                                        : splitValue(c) == length;
            if (ct.can_split(c) && halvesOk &&
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

    void move(int d) {
        ct.make_move(Direction(DCH[d]));
        int const dest = nbr[body.front() * 4 + d];
        plannedPath.clear();
        if (dest >= 0)
            plannedPath.push_back(dest);
    }

    void movePath(Candidate const& c) {
        std::vector<Direction> dirs;
        for (int d : c.steps)
            dirs.push_back(Direction(DCH[d]));
        ct.make_moves(dirs);
        plannedPath = c.tiles;
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
        readSonar();
        inferSymmetry();
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
        if (params::QUEEN_SAFE && amQueen())
            computeUnseen();
        allyQueenTile = -1;
        if (params::QUEEN_FOOD_RADIUS >= 0 && !amQueen()) {
            for (auto const& a : allyHeads)
                if (isQueenId(a.id))
                    allyQueenTile = a.tile;
            if (allyQueenTile >= 0)
                bfsFrom({{-1, allyQueenTile, 0}}, allyQueenDist, nullptr);
        }
        updateKing(length);
        adaptToGame(units);
        chooseKing(length, units);
        if (params::DEBUG_SONAR && round >= params::KING_PULL_ROUND && round % 25 == 0)
            ct.output_log("king", king.id, king.len, "age", round - king.round, "me", myId, length,
                          "dist", kingTile >= 0 ? kingDist[body.front()] : -1, "vis", kingVisible ? 1 : 0);
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
        // Free steps (ceil(L/4) a turn): every path of 2 or more steps through tiles in view,
        // never turning straight back. Scored like a single step, from where it ends.
        std::vector<Candidate> multi;
        int const freeSteps = (length + 3) / 4;
        if (params::MULTI_STEP && freeSteps >= 2 && static_cast<int>(body.size()) == length &&
            (!params::MULTI_QUEEN_ONLY || amQueen())) {
            int const maxSteps = std::min(freeSteps, N > params::MULTI_BIG_TILES ? params::MULTI_MAX_STEPS_BIG
                                                                                  : params::MULTI_MAX_STEPS);
            std::vector<int> path;
            auto extend = [&](auto&& self) -> void {
                if (path.size() >= 2) {
                    Candidate c;
                    c.steps = path;
                    evaluate(c, length);
                    if (c.fatal)
                        return; // nor can any longer path through here live
                    multi.push_back(std::move(c));
                }
                if (static_cast<int>(path.size()) >= maxSteps)
                    return;
                for (int d = 0; d < 4; d++) {
                    if (!path.empty() && d == (path.back() + 2) % 4)
                        continue;
                    if (path.empty() && cands[d].fatal)
                        continue;
                    path.push_back(d);
                    self(self);
                    path.pop_back();
                }
            };
            extend(extend);
            for (auto const& c : multi)
                if (!best || c.score > best->score)
                    best = &c;
        }
        if (params::DEBUG && best) {
            std::string p;
            for (int d : best->steps.empty() ? std::vector<int>{best->dir} : best->steps)
                p += DCH[d];
            ct.output_log("best", p, static_cast<long>(best->score), "space", best->space, "safe", best->safe ? 1 : 0,
                          "threat", amQueen() && !qThreat.empty() ? qThreat[best->dest] : -1.0, "paths", multi.size());
        }
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

        // The last dragon slot is our queen's: at the cap she could not split to save herself.
        bool const slotFree = amQueen() || units < game.get_unit_limit() - params::QUEEN_SPARE_SLOTS;
        bool const canSplit = ct.can_split(length / 2) && slotFree;
        int const head = body.front();

        // Trapped (or worse): compare moving on with every split, by length expected to survive.
        bool const bodyKnown = static_cast<int>(body.size()) == length;
        // Our queen does not stand still (a split) where an enemy head can probably reach her.
        bool const queenExposed = params::QUEEN_SAFE && amQueen() && best && qThreat[head] >= 0.5;
        // (A dive is unsafe on purpose: going in is the plan, splitting out comes later.)
        if ((!best || (!best->safe && !best->dive)) && canSplit && bodyKnown && !queenExposed) {
            // An unsafe move buys time, which is worth a little in case others move away.
            // In the endgame a long dragon (usually the king) is scored by its longest piece:
            // splitting to "save" both halves only shrinks the dragon that decides the game, so
            // moving on counts as keeping all of it for as long as there is room to move.
            bool const keepWhole = params::ENDGAME_KEEP_WHOLE && bigKing(length);
            // Our queen likewise keeps all of herself for as long as she has room to move.
            bool const queenWhole = params::QUEEN_SAFE && amQueen();
            double moveValue = 0.0;
            if (best)
                moveValue = (keepWhole || queenWhole ? 1.0 : 0.25) * best->newLength *
                            std::min(1.0, static_cast<double>(best->space) / best->newLength);
            // A king with a few free tiles ahead is not trapped yet: "room" rarely reaches its
            // length on a crowded map, and splitting every few rounds shredded 20-long kings to 9
            // on the ladder. Keep moving whole; split only when it is really boxed in.
            if (keepWhole && best && best->space >= params::KEEP_WHOLE_MIN_SPACE)
                moveValue = best->newLength;
            int bestChild = 0;
            double bestValue = moveValue;
            // Our queen keeps the head: what counts is her own surviving length.
            bool const queenSplit = params::QUEEN_SAFE && amQueen();
            std::vector<int> sizes{2, 3, length / 4, length / 3, length / 2, 2 * length / 3, 3 * length / 4,
                                   length - 3, length - 2};
            std::sort(sizes.begin(), sizes.end());
            sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
            for (int child : sizes) {
                if (child < 2 || child > length - 2)
                    continue;
                double v;
                if (queenSplit) {
                    int parentPart = 0, childPart = 0;
                    splitValue(child, false, &parentPart, &childPart);
                    v = parentPart + params::QUEEN_CHILD_WEIGHT * childPart;
                } else {
                    v = splitValue(child, keepWhole);
                }
                if (v > bestValue || (v == bestValue && bestChild > 0 && childKnowsBody(child) && !childKnowsBody(bestChild))) {
                    bestValue = v;
                    bestChild = child;
                }
            }
            // Every move is fatal and no split looks safe: split anyway. The rear leaves facing
            // away from the head (out of the dead end we drove into) and lives at least a while.
            // (Our queen sheds the smallest child instead: she stays put a turn, keeping her length.)
            if (!best && bestChild == 0 && params::DIVE)
                bestChild = queenSplit ? 2 : length - 2;
            if (bestChild > 0) {
                split(bestChild);
                ct.set_indicator_string("escape split " + std::to_string(bestChild));
                return;
            }
        }
        // Every move kills us and we don't know our whole body, so the escape split above was
        // skipped: split anyway. Our front is known, and the rear leaves from our old tail.
        if (!best && params::DOOMED_BLIND_SPLIT && canSplit && !bodyKnown && length >= 4) {
            split(params::QUEEN_SAFE && amQueen() ? 2 : length - 2);
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

        if (tryFeed(length))
            return;
        if (tryRecycle(length, units))
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

        if (best->steps.size() >= 2)
            movePath(*best);
        else
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
