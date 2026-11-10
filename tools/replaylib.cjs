// Replay loader and board model shared by the replay study tools (needs `python tools/replay_setup.py` once).
const fs = require('fs');
const path = require('path');
const noop = () => {};
const inert = { get: (t, k) => (k in t ? t[k] : new Proxy(noop, inert)), apply: () => new Proxy(noop, inert), construct: () => new Proxy({}, inert) };
globalThis.window = new Proxy({ addEventListener: noop, removeEventListener: noop, matchMedia: () => ({ matches: false, addEventListener: noop }) }, inert);
globalThis.document = new Proxy({}, inert);
globalThis.navigator = { userAgent: 'node' };
globalThis.requestAnimationFrame = noop;
globalThis.acquireVsCodeApi = () => ({ postMessage: noop, setState: noop, getState: () => null });
require(path.join(__dirname, '..', 'results', 'replay_decoder.cjs'));

const DIRS = ['N', 'E', 'S', 'W'];
const DX = { N: 0, E: 1, S: 0, W: -1 };
const DY = { N: -1, E: 0, S: 1, W: 0 };
const OPP = { N: 'S', S: 'N', E: 'W', W: 'E' };

function decode(file) {
    return globalThis.__replay.decode(new Uint8Array(fs.readFileSync(file)).buffer);
}

// Static map: edges (0 open, 1 kelp, 2 portal) with portal partners, and spawn rates.
class Board {
    constructor(text) {
        this.hEdge = null; this.vEdge = null; this.portal = new Map(); this.gap = null;
        const portals = new Map();
        for (const line of text.split('\n')) {
            const p = line.trim().split(/\s+/);
            if (p[0] === 'MAP') {
                this.W = +p[1]; this.H = +p[2];
                this.hEdge = new Int32Array(this.W * this.H); // north side of (x, y)
                this.vEdge = new Int32Array(this.W * this.H); // west side of (x, y)
                this.gap = new Int32Array(this.W * this.H);
            } else if (p[0] === 'MAP_NAME') this.name = p.slice(1).join(' ');
            else if (p[0] === 'SYMMETRY') this.sym = p[1];
            else if (p[0] === 'TILE') this.gap[+p[2] * this.W + +p[1]] = +p[4];
            else if (p[0] === 'EDGE') {
                const i = +p[1], k = +p[2], pid = +p[3];
                const r = Math.floor(i / (this.W + 1)), c = i % (this.W + 1);
                let key;
                if (r % 2 === 0) { const x = c % this.W, y = (r / 2) % this.H; this.hEdge[y * this.W + x] = k; key = { side: 'N', x, y }; }
                else { const x = c % this.W, y = (r - 1) / 2; this.vEdge[y * this.W + x] = k; key = { side: 'W', x, y }; }
                if (k === 2) {
                    if (!portals.has(pid)) portals.set(pid, []);
                    portals.get(pid).push(key);
                }
            }
        }
        for (const [, ends] of portals) {
            if (ends.length !== 2) continue;
            const [a, b] = ends;
            this.portal.set(`${a.side}${a.x},${a.y}`, b);
            this.portal.set(`${b.side}${b.x},${b.y}`, a);
        }
    }
    wrap(x, y) { return [((x % this.W) + this.W) % this.W, ((y % this.H) + this.H) % this.H]; }
    // The edge crossed leaving (x, y) toward d, as {side, x, y} of the tile whose N/W side it is.
    edgeKey(x, y, d) {
        if (d === 'N') return { side: 'N', x, y };
        if (d === 'S') { const [, yy] = this.wrap(x, y + 1); return { side: 'N', x, y: yy }; }
        if (d === 'W') return { side: 'W', x, y };
        const [xx] = this.wrap(x + 1, y); return { side: 'W', x: xx, y };
    }
    edgeKind(x, y, d) {
        const e = this.edgeKey(x, y, d);
        return (e.side === 'N' ? this.hEdge : this.vEdge)[e.y * this.W + e.x];
    }
    // Destination tile of a step, or null for kelp. Portals keep the direction of travel.
    step(x, y, d) {
        const e = this.edgeKey(x, y, d);
        const k = (e.side === 'N' ? this.hEdge : this.vEdge)[e.y * this.W + e.x];
        if (k === 1) return null;
        if (k === 2) {
            const q = this.portal.get(`${e.side}${e.x},${e.y}`);
            if (q) {
                if (q.side === 'W') return this.wrap(d === 'E' ? q.x : q.x - 1, q.y);
                return this.wrap(q.x, d === 'S' ? q.y : q.y - 1);
            }
        }
        return this.wrap(x + DX[d], y + DY[d]);
    }
}

// Live state replayed from the event stream: bodies (head first), pearls, round.
class Game {
    constructor(replay) {
        this.replay = replay;
        this.board = new Board(replay.map);
        const W = this.board.W, H = this.board.H;
        this.pearl = new Uint8Array(W * H);
        this.occ = new Int32Array(W * H).fill(-1); // dragon id per tile
        this.dragons = new Map(); // id -> {id, team, body: [[x,y]...], facing}
        const s0 = replay.match.roundAt(0);
        for (const d of s0.bodies.dragons.values()) this.add(d.id, d.team, d.facing, d.body.map(p => [p.x, p.y]));
        this.round = -1;
    }
    add(id, team, facing, body) {
        const d = { id, team, facing, body, born: this.round };
        this.dragons.set(id, d);
        for (const [x, y] of body) this.occ[y * this.board.W + x] = id;
        return d;
    }
    remove(id) {
        const d = this.dragons.get(id);
        if (!d) return;
        for (const [x, y] of d.body) if (this.occ[y * this.board.W + x] === id) this.occ[y * this.board.W + x] = -1;
        this.dragons.delete(id);
    }
    // Apply one event; returns the event for chaining.
    apply(e) {
        const W = this.board.W;
        switch (e.type) {
            case 'roundStart': this.round = e.round; break;
            case 'tileChange': this.pearl[e.tile.y * W + e.tile.x] = e.hasPearl ? 1 : 0; break;
            case 'dragonUpdate': {
                const d = this.dragons.get(e.id);
                if (!d || this.round < 0) break; // the replay's init updates restate the round-0 bodies
                d.facing = e.facing;
                d.body.unshift([e.head.x, e.head.y]);
                this.occ[e.head.y * W + e.head.x] = e.id;
                while (d.body.length > 1) {
                    const t = d.body[d.body.length - 1];
                    if (t[0] === e.tail.x && t[1] === e.tail.y) break;
                    d.body.pop();
                    if (this.occ[t[1] * W + t[0]] === e.id && !d.body.some(p => p[0] === t[0] && p[1] === t[1])) this.occ[t[1] * W + t[0]] = -1;
                }
                break;
            }
            case 'dragonSplit': {
                const d = this.dragons.get(e.parentId);
                const team = e.team ?? (d && d.team);
                if (d) {
                    for (const [x, y] of d.body) if (this.occ[y * W + x] === d.id) this.occ[y * W + x] = -1;
                    d.body = e.parentBody.map(p => [p.x, p.y]);
                    for (const [x, y] of d.body) this.occ[y * W + x] = d.id;
                }
                this.add(e.childId, team, e.childFacing, e.childBody.map(p => [p.x, p.y]));
                break;
            }
            case 'dragonDeath': this.remove(e.id); break;
        }
        return e;
    }
}

function loadGames(dir) {
    const list = JSON.parse(fs.readFileSync(path.join(dir, 'games.json'), 'utf8'));
    return list.filter(g => fs.existsSync(path.join(dir, `${g.id}.replay`)));
}

// Which replay team letter the target played, checked against the site's recorded winner.
function targetTeam(g, replay) {
    const T = g.side === 'a' ? 'A' : 'B';
    const w = replay.result && replay.result.winner;
    const siteW = g.winner === 'a' ? 'A' : g.winner === 'b' ? 'B' : null;
    return { T, consistent: !siteW || !w || w === siteW };
}

module.exports = { DIRS, DX, DY, OPP, decode, Board, Game, loadGames, targetTeam };
