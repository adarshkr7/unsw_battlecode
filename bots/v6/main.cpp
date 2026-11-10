// AllRed v10: fs2's fast swarm (portals, exploration, density trades, an anchored king)
// with v9's queen: the anchored king is our queen. No map data: every map is played as an
// unseen one, learned from what our dragons see and tell each other.
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
    int portalCursor = 0;
    bool isScout = false;          // a portal scout this turn (see SCOUT_*)
    std::vector<int> scoutDist;    // steps to a tile from which a portal worth scouting can be entered
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
    std::vector<int> farDist, myDist; // long-range exploration: steps to our chosen unseen tile, and from our head
    bool exploreFar = false;
    int allyQueenTile = -1;     // our queen's head, when we (not she) can see it
    std::vector<int> allyQueenDist;
    bool lkKingSeen = false;    // v19: a local king of ours is in view (not counting us)
    std::vector<int> lkDist;    // v19: steps from the nearest local king's head in view
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
    sonar::Prey packPrey;      // the enemy's longest dragon we know of (pack hunting)
    bool isHunter = false;     // this turn we head for packPrey
    int anchorTile = -1;       // anchored king: where we became king
    int bornRound = -1;        // first round this process ran
    std::vector<std::pair<int, int>> deadKings; // anchored king: (id, round) of kings seen to be gone
    bool kingCrowded = false;  // anchored king: another of ours is already inside the crowd radius
    std::vector<sonar::Threat> threats; // zone radar: enemy groups seen near the king (ours and heard)
    sonar::Threat ownThreat;            // the enemy heads we see this turn (e = 0: none)
    int zoneE = 0, zoneA = 0;           // the king's count of enemy / our heads within ZONE_R
    bool escorting = false;             // zone HOLD/FLEE: keep ESCORT_DIST off the king
    bool guarding = false;              // bodyguard: keep GUARD_DIST off the king, intercept
    int homeX = -1, homeY = -1;         // where our team started (HOME record)
    std::vector<int> packDist; // steps to packPrey
    sonar::EnemyQueen enemyQ; // the latest sighting of the enemy queen we know of (ours or relayed)
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
        scoutDist.assign(N, INF);
        preyDist.assign(N, INF);
        packDist.assign(N, INF);
        queenDist.assign(N, INF);
        farDist.assign(N, INF);
        myDist.assign(N, INF);
        allyQueenDist.assign(N, INF);
        lkDist.assign(N, INF);
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

    // -- portals -------------------------------------------------------------------------
    // A move from `from` in direction d that goes through a portal (it lands somewhere other than
    // the plain neighbour).
    bool throughPortal(int from, int d, int dest) const {
        return dest >= 0 && dest != idxOf(wrapX(from % W + DX[d]), wrapY(from / W + DY[d]));
    }

    bool farSideWanted(int t) const {
        return t == STEP_UNKNOWN_PORTAL ||
               (t >= 0 && (lastSeen[t] < 0 || round - lastSeen[t] > params::SCOUT_STALE));
    }

    void chooseScouting(int length) {
        isScout = false;
        if (params::SCOUT_FRACTION <= 0.0 || length > params::SCOUT_MAX_LEN || round >= params::SCOUT_UNTIL)
            return;
        std::uint32_t const hash = static_cast<std::uint32_t>(myId) * 2654435761u;
        if ((hash >> 8) % 1000 >= static_cast<std::uint32_t>(params::SCOUT_FRACTION * 1000.0))
            return;
        isScout = true;
        // Tiles from which one step crosses a portal into a far side worth a look.
        std::vector<Head> targets;
        auto consider = [&](int tile, int d) {
            if (tile >= 0 && farSideWanted(nbr[tile * 4 + d]))
                targets.push_back({-1, tile, 0});
        };
        for (auto const& [pid, ends] : K.portalEnds) {
            for (auto const& [h, e] : ends) {
                int const x = e % W, y = e / W;
                if (h) { // the north side of tile e: cross from the tile above going S, or from e going N
                    consider(idxOf(x, wrapY(y - 1)), 2);
                    consider(e, 0);
                } else { // the west side of tile e
                    consider(idxOf(wrapX(x - 1), y), 1);
                    consider(e, 3);
                }
            }
        }
        if (targets.empty())
            std::fill(scoutDist.begin(), scoutDist.end(), INF);
        else
            bfsFrom(targets, scoutDist, nullptr);
    }

    // A portal end we have not seen ourselves: take it as heard (what we see still wins).
    void notePortalEnd(int pid, bool horizontal, int e) {
        int8_t& slot = horizontal ? K.hKind[e] : K.vKind[e];
        int& id = horizontal ? K.hPid[e] : K.vPid[e];
        if (slot == EK_UNKNOWN || (slot == EK_PORTAL && id == pid)) {
            slot = EK_PORTAL;
            id = pid;
            K.addPortalEnd(pid, horizontal, e);
        }
    }

    void learnPortalPair(int pid, bool h1, int e1, bool h2, int e2) {
        if (pid < 0 || h1 != h2 || (h1 == h2 && e1 == e2))
            return; // connected portals share an orientation
        notePortalEnd(pid, h1, e1);
        notePortalEnd(pid, h2, e2);
    }

    // Mirrored portals form mirrored pairs: with the symmetry known, a portal edge we see at the
    // mirror of a known pair's end leads to the mirror of the other end.
    void inferMirrorPortals() {
        if (!params::SHARE_PORTALS || !K.sym)
            return;
        std::vector<std::pair<int, std::vector<std::pair<bool, int>>>> pairs;
        for (auto const& [pid, ends] : K.portalEnds)
            if (ends.size() == 2)
                pairs.emplace_back(pid, ends);
        for (auto const& [pid, ends] : pairs) {
            for (int k = 0; k < 2; k++) {
                auto const [h, e] = ends[k];
                auto const [h2, e2] = ends[1 - k];
                int const m = mirrorEdge(e, h), m2 = mirrorEdge(e2, h2);
                if (m < 0 || m2 < 0)
                    continue;
                int8_t const kind = h ? K.hKind[m] : K.vKind[m];
                int const q = h ? K.hPid[m] : K.vPid[m];
                if (kind != EK_PORTAL || q < 0 || q == pid)
                    continue;
                auto const it = K.portalEnds.find(q);
                if (it == K.portalEnds.end() || it->second.size() < 2)
                    notePortalEnd(q, h2, m2);
            }
        }
    }

    bool havePortalPair() const {
        for (auto const& [pid, ends] : K.portalEnds)
            if (ends.size() == 2 && pid <= sonar::PORTAL_MAX_ID)
                return true;
        return false;
    }

    std::uint64_t portalMessage() {
        std::vector<int> ids;
        for (auto const& [pid, ends] : K.portalEnds)
            if (ends.size() == 2 && pid <= sonar::PORTAL_MAX_ID)
                ids.push_back(pid);
        std::sort(ids.begin(), ids.end());
        int const pid = ids[portalCursor++ % ids.size()];
        auto const& ends = K.portalEnds[pid];
        sonar::Portal p;
        p.pid = pid;
        p.h1 = ends[0].first;
        p.x1 = ends[0].second % W;
        p.y1 = ends[0].second / W;
        p.h2 = ends[1].first;
        p.x2 = ends[1].second % W;
        p.y2 = ends[1].second / W;
        return sonar::packPortal(p);
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
            if (type == sonar::BODY) {
                bodyMsgs.push_back(payload);
                int const len = static_cast<int>(payload & 31);
                if (params::ANCHOR_HOME && homeX < 0 && len >= 2 && len <= 17 && ((payload >> 37) & 1)) {
                    int const x = static_cast<int>((payload >> 38) & 0x7F), y = static_cast<int>((payload >> 45) & 0x7F);
                    if (x < W && y < H) {
                        homeX = x;
                        homeY = y;
                    }
                }
            }
            else if (type == sonar::PORTAL && params::SHARE_PORTALS)
            {
                sonar::Portal const p = sonar::unpackPortal(payload);
                if (p.x1 < W && p.y1 < H && p.x2 < W && p.y2 < H)
                    learnPortalPair(p.pid, p.h1, idxOf(p.x1, p.y1), p.h2, idxOf(p.x2, p.y2));
            }
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
            else if (type == sonar::HOME && params::ANCHOR_HOME)
            {
                int const x = static_cast<int>(payload & 0x7F), y = static_cast<int>((payload >> 7) & 0x7F);
                if (homeX < 0 && x < W && y < H) {
                    homeX = x;
                    homeY = y;
                }
            }
            else if (type == sonar::THREAT && params::ZONE)
            {
                noteThreat(sonar::unpackThreat(payload));
            }
            else if (type == sonar::PREY)
            {
                sonar::Prey const k = sonar::unpackPrey(payload);
                if (k.x >= W || k.y >= H)
                    continue;
                if (params::PACK_ROUND < 999)
                    considerPrey(k);
                // A sighting of the enemy queen (prey records are enemies only).
                if (isQueenId(k.id) && k.round > enemyQ.round && k.round <= round)
                    enemyQ = sonar::enemyQueenOf(k);
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
                    r.mode = king.mode;
                }
                king = r;
            }
            return;
        }
        bool const expired = king.id < 0 || round - king.round > params::KING_STALE;
        if (anchorPhase()) {
            // Anchored king: our queen, whatever her length. Without word of her, only a dragon long
            // enough, not known to be dead, and clearly longer than a valid king replaces it.
            if (round - r.round > params::ANCHOR_STALE || isDeadKing(r.id, r.round))
                return;
            bool const queen = queenRanks(r.id);
            if (!queen && (r.len < params::ANCHOR_KING_MIN_LEN || (queenRanks(king.id) && kingValid())))
                return;
            if (queen || !kingValid() || r.len >= king.len + params::ANCHOR_REPLACE_MARGIN)
                king = r;
            return;
        }
        // Our queen outranks any longer dragon; without word of her (dead, or far away) the
        // longest one we know of is the king, as before.
        int const rankNew = queenRanks(r.id) ? 1 : 0;
        int const rankOld = queenRanks(king.id) ? 1 : 0;
        if (expired || rankNew > rankOld ||
            (rankNew == rankOld && (r.len > king.len || (r.len == king.len && r.id < king.id))))
            king = r;
    }

    // Anchored king: the king we know of still counts (fresh, long enough, not seen to be gone).
    bool kingValid() const {
        return king.id >= 0 && round - king.round <= params::ANCHOR_STALE &&
               (king.len >= params::ANCHOR_KING_MIN_LEN || queenRanks(king.id)) && !isDeadKing(king.id, king.round);
    }
    // Our queen is the king (QUEEN_KING): her records outrank any other.
    static bool queenRanks(int id) { return params::QUEEN_KING && isQueenId(id); }
    int wrapDist(int ax, int ay, int bx, int by) const {
        int const dx = std::abs(ax - bx), dy = std::abs(ay - by);
        return std::min(dx, W - dx) + std::min(dy, H - dy);
    }
    // Zone radar: keep the newest report per enemy group (groups within 3 steps are the same group).
    void noteThreat(sonar::Threat const& t) {
        if (t.e <= 0 || t.round > round || round - t.round > params::THREAT_STALE || t.cx >= W || t.cy >= H)
            return;
        for (auto& x : threats) {
            if (wrapDist(x.cx, x.cy, t.cx, t.cy) <= 3) {
                if (t.round > x.round) {
                    x = t;
                } else if (t.round == x.round) {
                    x.e = std::max(x.e, t.e);
                    x.a = std::max(x.a, t.a);
                    x.maxlen = std::max(x.maxlen, t.maxlen);
                }
                return;
            }
        }
        threats.push_back(t);
        if (threats.size() > 16)
            threats.erase(std::min_element(threats.begin(), threats.end(),
                                           [](auto const& a, auto const& b) { return a.round < b.round; }));
    }
    int zoneMode() const { return params::ZONE && anchorPhase() && kingValid() ? king.mode : 0; }
    bool nearKing(int within) const {
        return kingValid() && wrapDist(body.front() % W, body.front() / W, king.x, king.y) <= within;
    }
    // Seen to be gone, and the record (made in round `recRound`) is no newer: a record made after
    // the round we missed it says it lives (it moved out of view fast, or through a portal).
    bool isDeadKing(int id, int recRound) const {
        for (auto const& [d, r] : deadKings)
            if (d == id && round - r <= params::ANCHOR_DEAD_MEMORY && recRound <= r)
                return true;
        return false;
    }

    // Keep the longest enemy dragon we know of; a record ages out after PREY_STALE rounds.
    void considerPrey(sonar::Prey const& r) {
        if (r.id < 0 || r.round > round || round - r.round > params::PREY_STALE || r.len < params::PACK_MIN_PREY)
            return;
        if (r.id == packPrey.id) {
            // Lengths are the segments someone saw at once: keep the most anyone saw.
            if (r.round >= packPrey.round) {
                int const len = std::max(r.len, packPrey.len);
                packPrey = r;
                packPrey.len = len;
            }
            return;
        }
        bool const expired = packPrey.id < 0 || round - packPrey.round > params::PREY_STALE;
        if (expired || r.len > packPrey.len || (r.len == packPrey.len && r.round > packPrey.round))
            packPrey = r;
    }
    bool havePrey() const { return packPrey.id >= 0 && round - packPrey.round <= params::PREY_STALE; }

    void updatePrey(int length, int units) {
        isHunter = false;
        if (params::PACK_ROUND >= 999 || round < params::PACK_ROUND - params::PACK_SHARE_LEAD)
            return;
        // Where it was seen and is not any more: forget it (unless someone reports it again).
        if (havePrey()) {
            int const t = idxOf(packPrey.x, packPrey.y);
            if (packPrey.round < round && inView[t]) {
                bool there = false;
                for (auto const& e : enemyHeads)
                    if (e.id == packPrey.id)
                        there = true;
                if (!there)
                    packPrey.round = -1000;
            }
        }
        for (auto const& e : enemyHeads)
            considerPrey({e.id, e.len, e.tile % W, e.tile / W, round});
        if (round < params::PACK_ROUND || !havePrey() || length > params::PACK_MAX_LEN ||
            units < params::PACK_MIN_UNITS ||
            static_cast<int>(sonar::mix(static_cast<std::uint64_t>(myId) ^ 0x5EED) % 100) >= params::PACK_SHARE_PCT)
            return;
        isHunter = true;
        bfsFrom({{-1, idxOf(packPrey.x, packPrey.y), packPrey.len}}, packDist, nullptr);
        // Hunters don't feed: drop the king we would head for.
        kingTile = -1;
        kingVisible = false;
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
        bool claim = true;
        if (anchorPhase()) {
            // The king refreshes its record every turn (a king that split below the minimum stops
            // counting); our queen always claims; anyone else only when long enough and clearly
            // longer than a valid king, and never over a valid queen.
            // (A newborn's own record used to come first and, refreshed every turn, stayed king.)
            if (king.id == myId)
                king = self;
            claim = king.id != myId &&
                    (queenRanks(myId) ||
                     (length >= params::ANCHOR_KING_MIN_LEN && !(queenRanks(king.id) && kingValid()) &&
                      (!kingValid() || length >= king.len + params::ANCHOR_REPLACE_MARGIN)));
            // A fresh record put the king on a tile we see around it, and it isn't there: it's gone.
            if (king.id >= 0 && king.id != myId && kingValid()) {
                int const age = round - king.round;
                int dx = std::abs(king.x - head % W), dy = std::abs(king.y - head / W);
                int const cheb = std::max(std::min(dx, W - dx), std::min(dy, H - dy));
                if (age <= 1 && cheb <= 2 - age) {
                    bool seen = false;
                    for (auto const& a : allyHeads)
                        if (a.id == king.id)
                            seen = true;
                    if (!seen) {
                        deadKings.push_back({king.id, round});
                        king.round = -1000;
                    }
                }
            }
            std::erase_if(deadKings, [&](auto const& d) { return round - d.second > params::ANCHOR_DEAD_MEMORY; });
        }
        if (claim)
            considerKing(self, false);
        for (auto const& a : allyHeads)
            considerKing({a.id, a.len, a.tile % W, a.tile / W, round}, true);
        for (auto const& e : enemyHeads) {
            noteEnemyLen(e.len, round);
            if (isQueenId(e.id))
                enemyQ = {e.tile % W, e.tile / W, round, e.len, e.id};
        }
    }

    // A fresh sighting of the enemy queen is worth passing on.
    bool enemyQueenFresh() const { return params::HUNT_QUEEN && params::QUEEN_SHARE_AGE > 0 && round - enemyQ.round <= params::QUEEN_SHARE_AGE; }
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
        // Home rides along in the spare bits (links end by bit 36 for a child of 17 or less): a child
        // is a new process, and relays alone left ~30% of our dragons never knowing it.
        if (params::ANCHOR_HOME && homeX >= 0 && child <= 17)
            p |= (1ull << 37) | (static_cast<std::uint64_t>(homeX & 0x7F) << 38) | (static_cast<std::uint64_t>(homeY & 0x7F) << 45);
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
        bool const haveKing = anchorPhase() ? kingValid() : king.id >= 0 && round - king.round <= params::KING_STALE;
        // Zone radar: near the king, report the enemy heads we see (or relay the freshest report).
        sonar::Threat report;
        if (params::ZONE && anchorPhase() && haveBody && nearKing(params::ZONE_COMM_R)) {
            if (ownThreat.e > 0)
                report = ownThreat;
            else
                for (auto const& t : threats)
                    if (t.round > report.round && round - t.round <= 2)
                        report = t;
        }
        for (int d = 0; d < 4; d++) {
            if (outboxSet[d])
                ct.send_sonar(Direction(DCH[d]), outbox[d]);
            else if (params::SHARE_PORTALS && (round + d) % 3 == 1 && haveBody &&
                     nbr[body.front() * 4 + d] != STEP_KELP && havePortalPair())
                ct.send_sonar(Direction(DCH[d]), portalMessage());
            else if (params::ANCHOR_HOME && W * H >= params::ANCHOR_MIN_AREA && homeX >= 0 &&
                     (round + d) % params::HOME_SLOT_EVERY == 3 && haveBody &&
                     nbr[body.front() * 4 + d] != STEP_KELP)
                ct.send_sonar(Direction(DCH[d]), sonar::packHome(homeX, homeY));
            else if (report.e > 0 && (round + d) % 2 == 0 && nbr[body.front() * 4 + d] != STEP_KELP)
                ct.send_sonar(Direction(DCH[d]), sonar::packThreat(report));
            else if (params::PACK_ROUND < 999 && round >= params::PACK_ROUND - params::PACK_SHARE_LEAD &&
                     havePrey() && (round + d) % 3 == 2 && haveBody && nbr[body.front() * 4 + d] != STEP_KELP)
                ct.send_sonar(Direction(DCH[d]), sonar::packPrey(packPrey));
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
        int visits = 0;                // tiles its search reached (what it cost)
    };

    // A move of one step (c.dir), or of several (c.steps, every tile in view, no sprint cost).
    void evaluate(Candidate& c, int length) {
        int const head = body.front();
        // Our body after the move, head first.
        std::deque<int> post;
        double score = 0.0;
        int eaten = 0;
        bool crossed = false; // a step of a multi-step path goes through a portal
        int paid = 0; // v19: steps beyond the free ones, a segment each (eat-sprints only)
        if (c.steps.size() >= 2) {
            post = body;
            c.tiles.clear();
            int const freeSteps = (length + 3) / 4;
            for (int d : c.steps) {
                int const n = nbr[post.front() * 4 + d];
                // Other dragons stand still during our turn; our own tail moves on each step,
                // but entering the tile it still holds is fatal.
                if (n < 0 || !inView[n] || (occId[n] >= 0 && occId[n] != myId) ||
                    std::find(post.begin(), post.end(), n) != post.end()) {
                    c.fatal = true;
                    return;
                }
                crossed = crossed || throughPortal(post.front(), d, n);
                bool const eat = pearlNow[n] && std::find(c.tiles.begin(), c.tiles.end(), n) == c.tiles.end();
                c.tiles.push_back(n);
                post.push_front(n);
                if (eat) {
                    eaten++;
                    score += kingsFood(n) || queenFood(n) || lkFood(n) ? -params::KING_FOOD_PENALTY : params::EAT_NOW;
                } else {
                    post.pop_back();
                }
                if (static_cast<int>(c.tiles.size()) > freeSteps) {
                    post.pop_back(); // the step cost a segment
                    paid++;
                    score -= params::EAT_SPRINT_COST;
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
            // Our queen never goes through a portal blind.
            bool const queenSafe = params::QUEEN_SAFE && amQueen();
            if (dest == STEP_UNKNOWN_PORTAL && isScout && !queenSafe) {
                c.score = params::SCOUT_CROSS + std::uniform_real_distribution<double>(0.0, 1.0)(rng);
                c.safe = true;
                c.space = 3 * length;
                c.newLength = length;
                return;
            }
            if (dest == STEP_UNKNOWN_PORTAL && length <= params::PORTAL_EXPLORE_MAX_LEN &&
                unitsNow >= params::PORTAL_EXPLORE_MIN_UNITS && !queenSafe) {
                // We can't search past it; going through teaches the team where it leads.
                c.score = params::PORTAL_EXPLORE_SCORE + std::uniform_real_distribution<double>(0.0, 1.0)(rng);
                c.safe = true;
                c.space = 3 * length;
                c.newLength = length;
                return;
            }
            if (dest == STEP_UNKNOWN_PORTAL) {
                c.score = -params::UNKNOWN_PORTAL - params::TRAPPED / 2 - (queenSafe ? params::QUEEN_PORTAL_PENALTY : 0.0);
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
                score += kingsFood(dest) || queenFood(dest) || lkFood(dest) ? -params::KING_FOOD_PENALTY : params::EAT_NOW;
            }
            // (The part of our body we know: all of it, or the front of an unknown one.)
            int const keep = std::min<int>(static_cast<int>(body.size()), length + eaten - 1);
            post.assign(body.begin(), body.begin() + keep);
            post.push_front(dest);
        }
        int const dest = post.front();
        c.dest = dest;
        c.eats = eaten > 0;
        // (A multi-step path stays in view, so only a single step can cross a portal out of it.)
        bool const viaPortal = c.steps.size() < 2 && throughPortal(head, c.dir, dest);
        if (params::QUEEN_SAFE && amQueen() && (viaPortal || crossed))
            score -= params::QUEEN_PORTAL_PENALTY;
        if (!inView[dest]) {
            if (viaPortal && isScout)
                ; // scouts go through on purpose
            else if (viaPortal && params::PORTAL_KNOWN_OOV >= 0.0)
                score -= length <= params::DIVE_MAX_LEN ? params::PORTAL_KNOWN_OOV : params::PORTAL_KNOWN_OOV_BIG;
            else
                score -= length <= params::DIVE_MAX_LEN ? params::OUT_OF_VIEW_SMALL : params::OUT_OF_VIEW;
        }
        if (isScout) {
            if (viaPortal && farSideWanted(dest))
                score += params::SCOUT_CROSS;
            else if (!viaPortal && scoutDist[head] < INF && scoutDist[dest] < INF)
                score += params::SCOUT_PULL * (scoutDist[head] - scoutDist[dest]);
        }
        score -= params::REVISIT * heat(dest);
        double const exploreScale =
            W * H >= params::EXPLORE_FULL_AREA ? 1.0
            : W * H < params::EXPLORE_MIN_AREA ? 0.0
            : static_cast<double>(W * H - params::EXPLORE_MIN_AREA) / std::max(1, params::EXPLORE_FULL_AREA - params::EXPLORE_MIN_AREA);
        if (params::EXPLORE_W > 0.0 && round < params::EXPLORE_UNTIL && exploreScale > 0.0 &&
            (params::QUEEN_EXPLORE || !amQueen()) &&
            !(params::EXPLORE_SMART && threatLen[dest] < INF)) {
            // Never-seen tiles that come into view if we stand on dest (the view is 7x7); with the
            // symmetry known, only those whose mirror is unseen as well.
            int fresh = 0;
            int const x0 = dest % W, y0 = dest / W;
            for (int dy = -3; dy <= 3; dy++)
                for (int dx = -3; dx <= 3; dx++) {
                    int const t = idxOf(wrapX(x0 + dx), wrapY(y0 + dy));
                    if (lastSeen[t] >= 0)
                        continue;
                    int const m = params::EXPLORE_SMART ? mirrorOf(t) : -1;
                    if (m >= 0 && lastSeen[m] >= 0)
                        continue;
                    fresh++;
                }
            score += params::EXPLORE_W * exploreScale * fresh * (length >= params::EXPLORE_BIG_LEN ? 0.3 : 1.0);
        }
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
        int const newLength = length + eaten - paid;
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
        // CPU (from v9.1): stop EVAL_MAX_DEPTH steps out, or the dragon's length plus 8 so it still
        // finds its own tail. Pearls that far away are worth almost nothing (0.85^30 < 1%).
        int const maxDepth = params::EVAL_MAX_DEPTH > 0 ? std::max(params::EVAL_MAX_DEPTH, newLength + 8) : INF;
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
        c.visits = qt;
        c.space = room;
        c.safe = reachedOwn || room >= enoughRoom(newLength);
        score += params::FOOD_BEST * best + params::FOOD_SUM * sum;

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
            score -= params::QUEEN_THREAT * qThreat[dest];
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
            double pen = disadvantage > 0 ? params::THREAT_PER_LEN * disadvantage : params::THREAT_EVEN;
            if (params::THREAT_DENSITY_PENALTY > 0.0 && disadvantage >= 0 &&
                headsNear(enemyHeads, dest, params::TRADE_DENSITY_RADIUS, -1) >=
                    headsNear(allyHeads, dest, params::TRADE_DENSITY_RADIUS, -1))
                pen = std::max(pen, params::THREAT_DENSITY_PENALTY);
            score -= scale * pen;
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
        if (params::ZONE && iAmEarlyKing()) {
            // Radar: keep away from reported enemy groups (harder when fleeing).
            double const w = params::RADAR_W * (king.mode == 3 ? 2.0 : 1.0);
            for (auto const& t : threats) {
                int const d = wrapDist(dest % W, dest / W, t.cx, t.cy);
                if (d <= params::RADAR_R)
                    score -= w * t.e * (params::RADAR_R + 1 - d);
            }
        }
        if (escorting && kingTile >= 0 && kingDist[dest] < INF)
            score -= params::ESCORT_W * std::abs(kingDist[dest] - params::ESCORT_DIST);
        if (guarding && kingTile >= 0 && kingDist[dest] < INF)
            score -= params::GUARD_W * std::abs(kingDist[dest] - params::GUARD_DIST);
        if (params::ANCHOR_CROWD_RADIUS > 0 && anchorPhase() && !iAmEarlyKing() && kingVisible && kingTile >= 0 &&
            kingDist[dest] <= params::ANCHOR_CROWD_RADIUS) {
            // Keep off the king (its own body and ours box it in), unless we're the one feeder going in.
            bool const goingIn = anchorFeeder(length, unitsNow) && !kingCrowded;
            if (!goingIn && !guarding)
                score -= params::ANCHOR_CROWD_PENALTY * (params::ANCHOR_CROWD_RADIUS + 1 - kingDist[dest]);
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
        if (params::ANCHOR_KING && anchorTile >= 0 && iAmEarlyKing()) {
            int dx = std::abs(dest % W - anchorTile % W), dy = std::abs(dest / W - anchorTile / W);
            int const d = std::min(dx, W - dx) + std::min(dy, H - dy);
            int const radius = std::max(params::ANCHOR_RADIUS, static_cast<int>(params::ANCHOR_RADIUS_PER_LEN * newLength));
            if (d > radius)
                score -= params::ANCHOR_LEASH * (d - radius);
        }
        if (isHunter && packDist[dest] < INF) {
            score += params::PACK_ATTRACT / (1.0 + packDist[dest]);
            if (packDist[head] < INF)
                score += params::PACK_APPROACH * (packDist[head] - packDist[dest]);
        }
        if (preyGain > 0.0 && preyDist[dest] < INF)
            score += (aggressive ? params::HUNT_ATTRACT_AGGRO : params::HUNT_ATTRACT) *
                     std::min(preyGain, params::HUNT_GAIN_CAP) / (1.0 + preyDist[dest]);
        if (exploreFar && farDist[head] < INF && farDist[dest] < INF)
            score += params::EXPLORE_FAR_W * (farDist[head] - farDist[dest]);
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
    int anchorStart() const { return W * H >= params::ANCHOR_BIG_AREA ? params::ANCHOR_ROUND_BIG : params::ANCHOR_ROUND_MID; }
    bool anchorPhase() const {
        return params::ANCHOR_KING && params::SONAR && W * H >= params::ANCHOR_MIN_AREA && round >= anchorStart();
    }
    bool earlyKingPhase() const {
        return (params::EARLY_KING && params::SONAR && round >= params::EARLY_KING_ROUND &&
                W * H >= params::EARLY_KING_MIN_AREA) || anchorPhase();
    }
    // Anchored king, feeding at the cap: a small dragon with no enemy in view, near enough the king.
    bool anchorFeeder(int length, int units) const {
        return anchorPhase() && length <= params::ANCHOR_FEED_MAX_LEN && units >= game.get_unit_limit() - 1 &&
               enemyHeads.empty() && zoneMode() == 0;
    }
    bool iAmEarlyKing() const {
        return earlyKingPhase() && king.id == myId &&
               (!anchorPhase() || king.len >= params::ANCHOR_KING_MIN_LEN || queenRanks(myId));
    }
    // v19 local kings (docs/top_teams_tactics.md, tactic 1): from LK_ROUND on maps of LK_MIN_AREA+
    // tiles, the dragons whose id hashes under LK_PCT stop swarm-splitting and grow, fed by small
    // allies that die next to them. Any dragon can tell a candidate from its id.
    bool lkPhase() const {
        return params::LK_ROUND < 999 && W * H >= params::LK_MIN_AREA && round >= params::LK_ROUND;
    }
    static bool lkCandidateId(int id) {
        return !isQueenId(id) &&
               (params::LK_ANY_KING ||
                static_cast<int>(sonar::mix(static_cast<std::uint64_t>(id) ^ 0x4C4B) % 100) < params::LK_PCT);
    }
    // A local king long enough to keep its length: it feeds nobody, not even our queen.
    bool lkKeeper(int length) const { return lkPhase() && lkCandidateId(myId) && length >= params::LK_KEEP_LEN; }
    // Endgame (or early) king: protected, scored by its longest piece.
    bool bigKing(int length) const {
        return (round >= params::CONSOLIDATE_ROUND || iAmEarlyKing() || lkKeeper(length)) &&
               length >= params::KING_ROOM_LEN;
    }
    // v19: pearls next to one of our local kings are its own.
    bool lkFood(int t) const { return lkKingSeen && lkDist[t] <= params::LK_FOOD_RADIUS; }
    // v19 early queen feeding (tactic 2): from QFEED_ROUND, small dragons whose id hashes under
    // QFEED_PULL_PCT head for our queen and die next to her, while we keep QFEED_MIN_UNITS dragons.
    bool qfeedPuller(int length, int units) const {
        return params::QFEED_ROUND < 999 && round >= params::QFEED_ROUND && round < params::FEED_ROUND &&
               length <= params::QFEED_MAX_LEN && units >= params::QFEED_MIN_UNITS && !amQueen() &&
               (params::LK_ANY_KING || !lkCandidateId(myId)) &&
               static_cast<int>(sonar::mix(static_cast<std::uint64_t>(myId) ^ 0x5146) % 100) < params::QFEED_PULL_PCT;
    }
    // A pearl next to the king we feed is its food: leave it for it rather than eat it and
    // pass on only half when we feed in turn.
    // Pearls near our queen (in view) are hers, all game: her length is our score.
    bool queenFood(int t) const { return allyQueenTile >= 0 && allyQueenDist[t] <= params::QUEEN_FOOD_RADIUS; }
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
    // Room enough to count as safe: roomyEnough, but never more than the moves left in the game
    // (plus a margin). On the ladder our long queen split herself down to 2 in the last rounds
    // (Portals: 26 -> 2 at round 493, 31 -> 2 at 481) for want of room she no longer needed.
    int enoughRoom(int length) const {
        int const full = roomyEnough(length);
        if (!params::ENDGAME_HORIZON)
            return full;
        return std::min(full, std::max(1, Constants::MAX_ROUNDS - 1 - round) + params::HORIZON_MARGIN);
    }

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
        int const enough = enoughRoom(length);
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
        if (!params::INTERCEPT || (params::ANCHOR_KING && !anchorPhase()) || length > params::INTERCEPT_MAX_LEN ||
            units < params::INTERCEPT_MIN_UNITS)
            return 0.0;
        return threatensBig(t, elen) ? params::INTERCEPT_GAIN : 0.0;
    }

    // What a head trade is worth to us: positive means take it.
    // Heads (other than ours and the target's) within `radius` steps of tile t.
    int headsNear(std::vector<Head> const& heads, int t, int radius, int skipTile) const {
        int const tx = t % W, ty = t / W;
        int count = 0;
        for (auto const& h : heads) {
            if (h.tile == skipTile)
                continue;
            int dx = std::abs(h.tile % W - tx), dy = std::abs(h.tile / W - ty);
            if (std::min(dx, W - dx) + std::min(dy, H - dy) <= radius)
                count++;
        }
        return count;
    }

    // `tile`: where the enemy head is (-1: unknown); `enemyQueen`: it is their queen.
    double tradeGain(int enemyLen, int myLen, int units, int tile = -1, bool enemyQueen = false) const {
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
        // Even trades only where we will eat the drops: our other heads near the target
        // outnumber theirs.
        if (gain == 0 && params::TRADE_DENSITY && !aggressive && tile >= 0 &&
            headsNear(allyHeads, tile, params::TRADE_DENSITY_RADIUS, -1) <=
                headsNear(enemyHeads, tile, params::TRADE_DENSITY_RADIUS, tile))
            return -1.0;
        // Even trades thin both swarms; worth it while we have dragons to spare, or when we
        // outnumber the enemies around us (their losses hurt them more).
        if (gain == 0 && (aggressive || (!defensive && round < params::CONSOLIDATE_ROUND && units >= params::TRADE_MIN_UNITS)))
            return aggressive ? 1.0 : 0.5;
        // Top teams' small dragons ram any head at least as long as themselves.
        if (params::KAMIKAZE && gain == 0 && myLen <= 3 && units >= params::KAMIKAZE_MIN_UNITS)
            return 0.5;
        // v19 (from v16): trade a little down where our heads outnumber theirs. The top teams' most
        // common head-on is their 3 into our 2 where they outnumber us, and they eat the drops.
        if (params::TRADE_DOWN_MAX > 0 && gain < 0 && -gain <= params::TRADE_DOWN_MAX &&
            myLen <= params::TRADE_DOWN_MAX_LEN && tile >= 0 && round < params::CONSOLIDATE_ROUND &&
            units >= params::TRADE_MIN_UNITS && !defensive &&
            headsNear(allyHeads, tile, params::TRADE_DENSITY_RADIUS, -1) >=
                headsNear(enemyHeads, tile, params::TRADE_DENSITY_RADIUS, tile) + params::TRADE_DOWN_MARGIN)
            return params::TRADE_DOWN_GAIN;
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
                        double const g = std::max(tradeGain(lengthOf(occId[n]), length, units, n, queen),
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
            double const g = std::max(tradeGain(sg.len, length, units, sg.tile, isQueenId(sg.id)),
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
                            round < std::min(params::FEED_ROUND, params::QUEEN_HUNT_UNTIL) &&
                            myId % params::QUEEN_HUNT_SHARE == 0;
        int const qt = idxOf(enemyQ.x, enemyQ.y);
        if (hunter && round - enemyQ.round <= params::QUEEN_HUNT_MEMORY && !(enemyQ.round < round && inView[qt])) {
            bfsFrom({{-1, qt, enemyQ.len}}, queenDist, nullptr);
            // v19: only dragons already near her hunt (the top teams' killers start ~12 steps away).
            queenHunt = params::QUEEN_HUNT_RADIUS <= 0 || queenDist[body.front()] <= params::QUEEN_HUNT_RADIUS;
        }
    }

    // Long-range exploration: the nearest never-seen tile that we reach before any other dragon of
    // ours (so the swarm spreads out over the unknown), and a pull towards it. Local exploration
    // (EXPLORE_W) stops once our surroundings are seen: on Australia our swarm roamed the barren
    // middle for 100 rounds while the pearls sat 10-20 tiles away.
    void chooseFarTarget(int length) {
        exploreFar = false;
        if (params::EXPLORE_FAR_W <= 0.0 || round >= params::EXPLORE_UNTIL || W * H < params::EXPLORE_MIN_AREA ||
            amQueen() || length > params::EXPLORE_FAR_MAX_LEN || (params::SONAR && round >= params::KING_PULL_ROUND))
            return;
        bfsFrom({{myId, body.front(), length}}, myDist, nullptr);
        int target = -1;
        for (int t = 0; t < N; t++) {
            if (lastSeen[t] >= 0 || myDist[t] == INF || myDist[t] >= distA[t])
                continue;
            int const m = mirrorOf(t);
            if (m >= 0 && lastSeen[m] >= 0)
                continue; // its mirror is seen: we know what is there
            if (target < 0 || myDist[t] < myDist[target])
                target = t;
        }
        if (target < 0)
            return;
        exploreFar = true;
        bfsFrom({{-1, target, 2}}, farDist, nullptr);
    }

    // Late game: the longest ally in view that we should feed, if any.
    void chooseKing(int length, int units) {
        kingTile = -1;
        kingLen = 0;
        kingVisible = false;
        kingPull = true;
        ringBusy = false;
        escorting = false;
        guarding = false;
        // Our queen feeds nobody: her length is our score.
        if (params::QUEEN_KING && amQueen())
            return;
        // v19: a local king keeps its length for the longest-dragon tiebreak.
        if (lkKeeper(length))
            return;
        // Late in the game the last two dragons may merge too (we lost 10-11 and 17-18 holding
        // two that never could); earlier, keep a spare against elimination.
        int const keep = params::FINAL_MERGE && round >= params::FINAL_MERGE_ROUND ? 1 : params::FEED_KEEP_UNITS;
        if (units <= keep)
            return;
        // With sonar the whole team agrees on one king: the longest dragon anyone has heard of.
        // Head for it from KING_PULL_ROUND, feed it once it is in view.
        if (params::SONAR && king.id >= 0 && (anchorPhase() ? kingValid() : round - king.round <= params::KING_STALE) &&
            (king.id == myId || king.len >= params::KING_MIN_FOLLOW_LEN)) {
            if (king.id == myId)
                return;
            if (round < params::KING_PULL_ROUND) {
                // v19: early queen feeding, for the pullers only.
                if (isQueenId(king.id) && qfeedPuller(length, units)) {
                    kingTile = idxOf(king.x, king.y);
                    kingLen = king.len;
                    for (auto const& a : allyHeads)
                        if (a.id == king.id) {
                            kingTile = a.tile;
                            kingLen = a.len;
                            kingVisible = true;
                        }
                    bfsFrom({{-1, kingTile, kingLen}}, kingDist, nullptr);
                    return;
                }
                // Early king: no pull yet, just leave the food around it (when we see it) alone.
                if (!earlyKingPhase())
                    return;
                // With dragons to spare, small ones head for it (to feed it) as in the endgame.
                bool pull = round >= params::EARLY_FEED_ROUND && length <= params::EARLY_FEED_MAX_LEN &&
                            units >= params::EARLY_PULL_MIN_UNITS;
                if (!pull && params::GUARD_PCT > 0 && length <= params::GUARD_MAX_LEN && nearKing(params::GUARD_RECRUIT_R) &&
                    static_cast<int>(sonar::mix(static_cast<std::uint64_t>(myId) ^ 0x6A4D) % 100) < params::GUARD_PCT) {
                    // Bodyguard: stay GUARD_DIST off the king; INTERCEPT makes intruders trade targets.
                    int t = idxOf(king.x, king.y);
                    for (auto const& a : allyHeads)
                        if (a.id == king.id) {
                            t = a.tile;
                            kingVisible = true;
                        }
                    kingTile = t;
                    kingLen = king.len;
                    kingPull = false;
                    guarding = true;
                    bfsFrom({{-1, t, king.len}}, kingDist, nullptr);
                    return;
                }
                if (!pull && (zoneMode() == 1 || zoneMode() == 3) && nearKing(params::ZONE_COMM_R)) {
                    // Zone HOLD/FLEE: escort the king (it is the moving rally point) instead of feeding.
                    int t = idxOf(king.x, king.y);
                    for (auto const& a : allyHeads)
                        if (a.id == king.id) {
                            t = a.tile;
                            kingVisible = true;
                        }
                    kingTile = t;
                    kingLen = king.len;
                    kingPull = false;
                    escorting = true;
                    bfsFrom({{-1, t, king.len}}, kingDist, nullptr);
                    return;
                }
                if (!pull && anchorFeeder(length, units)) {
                    // Anchored king at the cap: small dragons nearby go and feed it.
                    int t = idxOf(king.x, king.y);
                    for (auto const& a : allyHeads)
                        if (a.id == king.id)
                            t = a.tile;
                    bfsFrom({{-1, t, king.len}}, kingDist, nullptr);
                    pull = kingDist[body.front()] <= params::ANCHOR_PULL_DIST;
                }
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
            // Our queen sealed off (Schooltime boxes each queen in kelp): nobody can feed her,
            // and with queens tied the longest dragon decides (we lost 22 v 59 to Deer Park with 44
            // dragons left). Feed the biggest ally in view instead.
            if (params::QUEEN_SEALED_FALLBACK && isQueenId(king.id) && kingDist[body.front()] == INF &&
                round >= params::FEED_ROUND) {
                kingTile = -1;
                kingLen = 0;
                kingVisible = false;
                biggerAllyKing(length, true);
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
        biggerAllyKing(length, false);
    }

    // Any ally in view bigger than us (ties to the lower id) is a king: feeding it makes it bigger
    // still, so feeders converge on one dragon instead of all waiting for a big one. Our queen in
    // view comes first, unless she is the one we cannot reach (skipQueen).
    void biggerAllyKing(int length, bool skipQueen) {
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

    // v19: a small dragon next to one of our local kings dies for it (its drops land at the king's
    // head), with chance LK_FEED_PROB a turn, while we have LK_FEED_MIN_UNITS dragons and see no
    // enemy head (they would take the drops).
    bool tryLkFeed(int length, int units) {
        if (!lkPhase() || length > params::LK_FEED_MAX_LEN || units < params::LK_FEED_MIN_UNITS || amQueen() ||
            (!params::LK_ANY_KING && lkCandidateId(myId)) || !enemyHeads.empty())
            return false;
        int const head = body.front();
        for (auto const& a : allyHeads) {
            if (!lkCandidateId(a.id) || a.len < params::LK_FEED_KING_LEN || a.len < length + params::RECYCLE_MIN_GAIN)
                continue;
            int dx = std::abs(a.tile % W - head % W), dy = std::abs(a.tile / W - head / W);
            if (std::min(dx, W - dx) + std::min(dy, H - dy) > params::LK_FEED_DIST)
                continue;
            if (std::uniform_real_distribution<double>(0.0, 1.0)(rng) >= params::LK_FEED_PROB)
                return false;
            ct.do_split(1); // an illegal split: we die, dropping pearls in front of the king
            plannedPath.clear();
            ct.set_indicator_string("lk feed");
            return true;
        }
        return false;
    }

    // Die next to the king: our body becomes pearls right where it is about to go.
    bool tryFeed(int length) {
        if (kingTile < 0 || !kingVisible || kingDist[body.front()] > params::FEED_DIST)
            return false;
        if (zoneMode() != 0 || guarding) // zone alarm: no feeding next to a king under threat; guards guard
            return false;
        // Early king at the dragon cap: our pearls can't become a new dragon anyway.
        bool const capFeed = ((params::EARLY_KING_FEED_AT_CAP && earlyKingPhase() && length <= 3 &&
                               unitsNow >= game.get_unit_limit() - 1) || anchorFeeder(length, unitsNow)) &&
                             kingLen >= length + params::RECYCLE_MIN_GAIN;
        bool const midFeed = earlyKingPhase() && round >= params::EARLY_FEED_ROUND &&
                             length <= params::EARLY_FEED_MAX_LEN && unitsNow >= params::EARLY_FEED_MIN_UNITS &&
                             kingLen >= length + params::RECYCLE_MIN_GAIN;
        bool const qfeed = isQueenId(king.id) && qfeedPuller(length, unitsNow) && enemyHeads.empty();
        if (round < params::FEED_ROUND && !capFeed && !midFeed && !qfeed)
            return false;
        ct.do_split(1); // an illegal split: we die with no valid action
        plannedPath.clear();
        ct.set_indicator_string("feed");
        return true;
    }

    // Swarm phase: split in half whenever long enough and both halves are safe.
    void splitLog(char const* why, int length) {
        if (!params::DEBUG_SPLITS || length < params::SWARM_SPLIT_LEN || round >= params::CONSOLIDATE_ROUND)
            return;
        int const e = distE[body.front()];
        ct.output_log("SPLITDBG", why, "len", length, "round", round, "units", unitsNow, "enemy", e < INF ? e : 99);
    }

    bool trySwarmSplit(int length, int units, Candidate const& best, bool bodyKnown) {
        if (params::DEBUG_SPLITS && length >= params::SWARM_SPLIT_LEN && round < params::CONSOLIDATE_ROUND) {
            char const* why = !bodyKnown ? "body-unknown"
                              : units >= game.get_unit_limit() ? "dragon-cap"
                              : (best.eats && !params::SPLIT_WHILE_EATING) ? "eating"
                              : (params::SONAR && round >= params::KING_GROW_ROUND && king.id == myId) ? "king"
                              : nullptr;
            if (why) {
                splitLog(why, length);
            } else {
                bool anySafe = false, anyLegal = false;
                for (int c : {length / 2, length / 2 + 1, length / 2 - 1}) {
                    if (!ct.can_split(c))
                        continue;
                    anyLegal = true;
                    if (splitValue(c) == length)
                        anySafe = true;
                }
                // the actual decision below; log what blocks it if it fails
                bool ok = false;
                for (int c : {length / 2, length / 2 + 1, length / 2 - 1})
                    if (ct.can_split(c) && splitValue(c) == length &&
                        (!params::SONAR || !params::SPLIT_WAITS_FOR_HANDOFF || childKnowsBody(c)))
                        ok = true;
                splitLog(ok ? "split" : !anyLegal ? "no-legal-size" : !anySafe ? "halves-not-safe" : "waits-handoff", length);
            }
        }
        if (round >= params::CONSOLIDATE_ROUND || length < params::SWARM_SPLIT_LEN || !bodyKnown ||
            units >= game.get_unit_limit() - params::SPLIT_HEADROOM - (anchorPhase() ? params::ANCHOR_HEADROOM : 0) ||
            (best.eats && !params::SPLIT_WHILE_EATING))
            return false;
        // The parent stands still for a turn, so not where an enemy head can reach it.
        if (params::SPLIT_THREAT_VETO && threatLen[body.front()] < INF)
            return false;
        // Early king (sparring style): from KING_GROW_ROUND the king stops halving itself.
        if (params::SONAR && round >= params::KING_GROW_ROUND && king.id == myId)
            return false;
        if (iAmEarlyKing())
            return false;
        // v19: a local king grows instead of halving itself.
        if (lkPhase() && lkCandidateId(myId) && length >= params::LK_MIN_LEN)
            return false;
        int const queenSplitUntil = W * H < params::QUEEN_SMALL_AREA ? params::QUEEN_SPLIT_UNTIL_SMALL : params::QUEEN_SPLIT_UNTIL;
        if (amQueen() && (round >= queenSplitUntil || length > params::QUEEN_SPLIT_MAX_LEN)) {
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
        if (round >= params::LOCAL_KING_ROUND && length >= params::LOCAL_KING_MIN_LEN) {
            bool longerAlly = false;
            for (auto const& a : allyHeads)
                if (a.len > length || (a.len == length && a.id < myId))
                    longerAlly = true;
            if (!longerAlly)
                return false; // the longest around: grow instead of halving
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

    // Multi-step paths within MULTI_VISIT_BUDGET tiles of search: every 2-step path (never
    // straight back), then 3-step extensions of the best of them, best first, while the
    // budget lasts. `reach`: tiles one path's search is expected to cost.
    void multiOnBudget(Candidate const (&cands)[4], int length, int maxSteps, int reach, std::vector<Candidate>& multi) {
        long used = 0;
        auto affordable = [&] { return used + reach <= params::MULTI_VISIT_BUDGET; };
        for (int d1 = 0; d1 < 4; d1++) {
            if (cands[d1].fatal)
                continue;
            for (int d2 = 0; d2 < 4; d2++) {
                if (d2 == (d1 + 2) % 4)
                    continue;
                if (!affordable())
                    return;
                Candidate c;
                c.steps = {d1, d2};
                evaluate(c, length);
                used += c.visits;
                if (!c.fatal)
                    multi.push_back(std::move(c));
            }
        }
        if (maxSteps < 3)
            return;
        std::vector<int> order(multi.size());
        for (int i = 0; i < static_cast<int>(order.size()); i++)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return multi[a].score > multi[b].score; });
        for (int i : order) {
            std::vector<int> const prefix = multi[i].steps; // (multi grows below)
            for (int d = 0; d < 4; d++) {
                if (d == (prefix.back() + 2) % 4)
                    continue;
                if (!affordable())
                    return;
                Candidate c;
                c.steps = prefix;
                c.steps.push_back(d);
                evaluate(c, length);
                used += c.visits;
                if (!c.fatal)
                    multi.push_back(std::move(c));
            }
        }
    }

    void decide() {
        round = game.get_round_num();
        if (bornRound < 0)
            bornRound = round;
        absorbView();
        readSonar();
        if (params::ANCHOR_HOME && homeX < 0 && round <= 2) {
            homeX = body.front() % W;
            homeY = body.front() / W;
        }
        if (params::ZONE && anchorPhase()) {
            std::erase_if(threats, [&](auto const& t) { return round - t.round > params::THREAT_STALE; });
            ownThreat = {};
            if (!enemyHeads.empty()) {
                int const hx = body.front() % W, hy = body.front() / W;
                int sx = 0, sy = 0, ml = 0;
                for (auto const& e : enemyHeads) {
                    int dx = e.tile % W - hx, dy = e.tile / W - hy;
                    if (dx > W / 2) dx -= W;
                    if (dx < -W / 2) dx += W;
                    if (dy > H / 2) dy -= H;
                    if (dy < -H / 2) dy += H;
                    sx += dx;
                    sy += dy;
                    ml = std::max(ml, e.len);
                }
                int const n = static_cast<int>(enemyHeads.size());
                ownThreat.cx = ((hx + static_cast<int>(std::lround(static_cast<double>(sx) / n))) % W + W) % W;
                ownThreat.cy = ((hy + static_cast<int>(std::lround(static_cast<double>(sy) / n))) % H + H) % H;
                ownThreat.e = n;
                ownThreat.a = static_cast<int>(allyHeads.size()) + 1;
                ownThreat.round = round;
                ownThreat.maxlen = ml;
                noteThreat(ownThreat);
            }
        }
        inferSymmetry();
        inferMirrorPortals();
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
        if (params::QUEEN_FOOD_RADIUS >= 0 && !amQueen() && !(params::QUEEN_FOOD_SMALL_OFF && W * H < params::QUEEN_SMALL_AREA)) {
            for (auto const& a : allyHeads)
                if (isQueenId(a.id))
                    allyQueenTile = a.tile;
            if (allyQueenTile >= 0)
                bfsFrom({{-1, allyQueenTile, 0}}, allyQueenDist, nullptr);
        }
        // v19: local kings in view (other than us): their food is theirs.
        lkKingSeen = false;
        if (lkPhase() && params::LK_FOOD_RADIUS >= 0) {
            std::vector<Head> kings;
            for (auto const& a : allyHeads)
                if (lkCandidateId(a.id) && a.len >= params::LK_FEED_KING_LEN)
                    kings.push_back(a);
            if (!kings.empty()) {
                lkKingSeen = true;
                bfsFrom(kings, lkDist, nullptr);
            }
        }
        updateKing(length);
        if (params::ZONE && iAmEarlyKing()) {
            // The king's zone: enemy groups within ZONE_R (radar), our heads there (the most any
            // reporter or we ourselves see).
            int const hx = body.front() % W, hy = body.front() / W;
            zoneE = 0;
            zoneA = static_cast<int>(allyHeads.size());
            for (auto const& t : threats) {
                if (wrapDist(t.cx, t.cy, hx, hy) <= params::ZONE_R) {
                    zoneE += t.e;
                    zoneA = std::max(zoneA, t.a);
                }
            }
            king.mode = zoneE == 0                           ? 0
                        : zoneA >= params::FIGHT_RATIO * zoneE ? 2
                        : zoneA < params::FLEE_RATIO * zoneE   ? 3
                                                               : 1;
        }
        adaptToGame(units);
        chooseKing(length, units);
        // Zone FIGHT: near the king, where we see the intruders, we fight.
        if (zoneMode() == 2 && !iAmEarlyKing() && !enemyHeads.empty() && nearKing(params::ZONE_R + 3))
            aggressive = true;
        if (params::DEBUG_SONAR && round >= params::KING_PULL_ROUND && round % 25 == 0)
            ct.output_log("king", king.id, king.len, "age", round - king.round, "me", myId, length,
                          "dist", kingTile >= 0 ? kingDist[body.front()] : -1, "vis", kingVisible ? 1 : 0);
        kingCrowded = false;
        if (params::ANCHOR_CROWD_RADIUS > 0 && kingVisible && kingTile >= 0)
            for (auto const& a : allyHeads)
                if (a.id != king.id && kingDist[a.tile] <= params::ANCHOR_CROWD_RADIUS)
                    kingCrowded = true;
        choosePrey(length, units);
        chooseFarTarget(length);
        updatePrey(length, units);
        if (params::DEBUG_ANCHOR && anchorPhase() && round % 10 == 0)
            ct.output_log("ANCHORDBG", round, "king", king.id, king.len, "age", round - king.round, "me", myId, length,
                          "self", iAmEarlyKing() ? 1 : 0, "units", units, "at", body.front() % W, body.front() / W,
                          "kat", king.x, king.y, "born", bornRound, "mode", king.mode, "zE", zoneE, "zA", zoneA,
                          "nthreat", static_cast<int>(threats.size()), "esc", escorting ? 1 : 0, "home", homeX >= 0 ? 1 : 0,
                          "guard", guarding ? 1 : 0);
        if (params::ANCHOR_KING) {
            if (!iAmEarlyKing() || !anchorPhase())
                anchorTile = -1;
            else if (params::ANCHOR_HOME && homeX >= 0)
                anchorTile = idxOf(homeX, homeY);
            else if (anchorTile < 0)
                anchorTile = body.front();
        }
        if (params::DEBUG_PACK && round >= params::PACK_ROUND - params::PACK_SHARE_LEAD && round % 10 == 0)
            ct.output_log("PACKDBG", round, "len", length, "units", units, "prey", havePrey() ? packPrey.len : -1,
                          "age", havePrey() ? round - packPrey.round : -1, "hunter", isHunter ? 1 : 0,
                          "dist", isHunter ? packDist[body.front()] : -1, "sees", static_cast<int>(enemyHeads.size()));
        chooseScouting(length);

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
            int const maxSteps = std::min(freeSteps, params::MULTI_MAX_STEPS);
            // Each path costs about one search of the room a single step reaches. On a big open
            // map every path of a 9+ dragon cost up to 97M points (limit 100M): past
            // MULTI_VISIT_BUDGET tiles, score every 2-step path, then extend only the best.
            int reach = 1;
            for (auto const& c : cands)
                reach = std::max(reach, c.visits);
            int const allPaths = maxSteps >= 3 ? 48 : 12;
            if (params::MULTI_VISIT_BUDGET > 0 && static_cast<long>(allPaths) * reach > params::MULTI_VISIT_BUDGET) {
                multiOnBudget(cands, length, maxSteps, reach, multi);
            } else {
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
            }
            for (auto const& c : multi)
                if (!best || c.score > best->score)
                    best = &c;
        }
        // v19 eat-sprints (tactic 7): a dragon with one free step may pay a segment to take a second
        // pearl next to the first in the same turn, when an enemy head is near that pearl. The top
        // teams did this 5-22 times a game, we once.
        if (params::EAT_SPRINT && freeSteps == 1 && length >= 2 && static_cast<int>(body.size()) == length &&
            !amQueen() && !enemyHeads.empty()) {
            int const h0 = body.front();
            for (int d1 = 0; d1 < 4; d1++) {
                if (cands[d1].fatal || !cands[d1].eats || cands[d1].dest < 0)
                    continue;
                int const n1 = nbr[h0 * 4 + d1];
                if (n1 < 0 || !inView[n1])
                    continue;
                for (int d2 = 0; d2 < 4; d2++) {
                    if (d2 == (d1 + 2) % 4)
                        continue;
                    int const n2 = nbr[n1 * 4 + d2];
                    if (n2 < 0 || !inView[n2] || !pearlNow[n2] || distE[n2] > params::EAT_SPRINT_ENEMY_DIST)
                        continue;
                    Candidate c;
                    c.steps = {d1, d2};
                    evaluate(c, length);
                    if (!c.fatal)
                        multi.push_back(std::move(c));
                }
            }
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

        if (tryStrike(length, units)) {
            splitLog("strike", length);
            return;
        }

        // Anchored king: the last slot(s) under the cap are the king's (escape splits included),
        // and our queen's even while she is not the king.
        bool const slotReserved = anchorPhase() && !iAmEarlyKing() && !(params::QUEEN_SAFE && amQueen()) &&
                                  units >= game.get_unit_limit() - params::ANCHOR_HEADROOM;
        bool const canSplit = ct.can_split(length / 2) && !slotReserved;
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
            // Our queen keeps all of herself while any move leaves her a little room: splitting her
            // to 2 "to be safe" (13 -> 2, 14 -> 2, 37 -> 2 on the ladder) killed her soon after,
            // and her length is our score. She splits only when boxed in for real.
            if (queenWhole && params::QUEEN_KEEP_LENGTH && best && best->space >= params::QUEEN_KEEP_MIN_SPACE)
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
                splitLog("escape-split", length);
                return;
            }
        }
        // Every move kills us and we don't know our whole body, so the escape split above was
        // skipped: split anyway. Our front is known, and the rear leaves from our old tail.
        if (!best && params::DOOMED_BLIND_SPLIT && canSplit && !bodyKnown && length >= 4) {
            split(params::QUEEN_SAFE && amQueen() ? 2 : length - 2);
            ct.set_indicator_string("blind escape split");
            splitLog("escape-split", length);
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
            splitLog("doomed", length);
            if (params::DEBUG_SONAR && length >= 8)
                ct.output_log("doomed", length, "body", body.size(), "canSplit", canSplit ? 1 : 0, "units", units,
                              "limit", game.get_unit_limit(), "round", round);
            return;
        }

        if (tryFeed(length) && (splitLog("feed", length), true))
            return;
        if (tryRecycle(length, units))
            return;
        if (tryLkFeed(length, units))
            return;

        // Insurance split: a lone dragon dying loses the game, whatever its length.
        if (ct.get_unit_count() < params::MIN_UNITS && length >= params::INSURANCE_MIN_LEN &&
            !(params::FINAL_MERGE && round >= params::FINAL_MERGE_ROUND) &&
            ct.can_split(params::INSURANCE_CHILD) && bodyKnown && best->safe && !enemyNear(head, 3) &&
            splitValue(params::INSURANCE_CHILD) == length) {
            split(params::INSURANCE_CHILD);
            ct.set_indicator_string("insurance split");
            splitLog("insurance", length);
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
