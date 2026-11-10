// Dump one team's turns from a folder of ladder replays, one JSON line per dragon turn:
//
//   node tools/turns.cjs results/teams/<team> [--since 2026-09-26T16:49] [--until ...] > turns.jsonl
//
// The folder is what tools/team_games.py writes: games.json (which side the team played) and
// <id>.replay files. Each line holds what the dragon saw when its turn started and what it did.
//   g game id, m map, W H map size, r round, id, par parent id (-1 for a spawn dragon), age rounds since birth,
//   len, x y head, f facing, uc/ec living dragons on its team / the other team,
//   nb what lies one step N,E,S,W: K kelp, . free, * pearl, b own body, t own tail tip,
//      a ally body, A ally head, e enemy body, E enemy head (a portal edge is followed),
//   room free tiles reachable from each neighbour (flood fill on the true board, capped at 40),
//   pv pearls in the 7x7 view, pd BFS steps to the nearest one in view (-1 if none), pdir its first steps,
//   pdd per first step N,E,S,W: BFS steps to the nearest pearl in view through that step (-1 if none),
//   cdd per first step: earliest round offset a pearl can be collected in view through that step, over
//       pearls and tiles that spawn (max of distance and the tile's countdown, rebuilt from the replay's
//       spawn events and the map's mirror pairs; -1 if none),
//   eh enemy heads in view as [dx, dy, length], ah ally heads in view, ahs them as [dx, dy, length, id],
//   ehd per step: Manhattan distance from the step's tile to the nearest enemy head in view (99 if none),
//   king the team's longest dragon at round start as [id, length], kd BFS distance (kelp and portals,
//       bodies ignored) from our head to its head, kdd the same from each step's tile (-1 across kelp),
//   tail [x, y], tf the direction the tail points away from the body (a split child's facing),
//   tnb the tile codes around the tail, N,E,S,W,
//   act the action: move steps ("N", "EEN"), "P<k>" split off k, "X" suicide,
//   ate pearls eaten, die death reason (W wall, S self, O other body, H head-on, A no valid action),
//   son sonars sent as "<dir><hit>" (k kelp, a ally, aH ally head, e enemy, eH enemy head, - nothing).

const fs = require('fs');
const path = require('path');

const decoderPath = path.join(__dirname, '..', 'results', 'replay_decoder.cjs');
if (!fs.existsSync(decoderPath)) {
    console.error('run `python tools/replay_setup.py` first');
    process.exit(1);
}
const noop = () => {};
const inert = { get: (t, k) => (k in t ? t[k] : new Proxy(noop, inert)), apply: () => new Proxy(noop, inert), construct: () => new Proxy({}, inert) };
globalThis.window = new Proxy({ addEventListener: noop, removeEventListener: noop, matchMedia: () => ({ matches: false, addEventListener: noop }) }, inert);
globalThis.document = new Proxy({}, inert);
globalThis.navigator = { userAgent: 'node' };
globalThis.requestAnimationFrame = noop;
globalThis.acquireVsCodeApi = () => ({ postMessage: noop, setState: noop, getState: () => null });
require(decoderPath);

const DIRS = ['N', 'E', 'S', 'W'];
const OFF = { N: [0, -1], E: [1, 0], S: [0, 1], W: [-1, 0] };
const KELP = 1, PORTAL = 2;

function game(file, T, meta, out) {
    const replay = globalThis.__replay.decode(new Uint8Array(fs.readFileSync(file)).buffer);
    const match = replay.match;
    const sm = match.map;
    const W = sm.width, H = sm.height;
    const wrap = (x, y) => [((x % W) + W) % W, ((y % H) + H) % H];
    const idx = (x, y) => y * W + x;
    const norm = (x, y, side) => {
        if (side === 'S') { y += 1; side = 'N'; }
        if (side === 'E') { x += 1; side = 'W'; }
        [x, y] = wrap(x, y);
        return `${x},${y},${side}`;
    };
    const partner = new Map();
    for (const [a, b] of sm.portals) {
        partner.set(norm(a.x, a.y, a.side), b);
        partner.set(norm(b.x, b.y, b.side), a);
    }
    // tile reached by one step from (x, y) in dir d; null across kelp
    const step = (map, x, y, d) => {
        const e = map.edgeAt(x, y, d);
        if (e === KELP) return null;
        if (e === PORTAL) {
            const p = partner.get(norm(x, y, d));
            if (p) {
                if (p.side === 'W' || p.side === 'E') {
                    const ex = p.side === 'W' ? p.x : p.x + 1;
                    return wrap(d === 'E' ? ex : ex - 1, p.y);
                }
                const ey = p.side === 'N' ? p.y : p.y + 1;
                return wrap(p.x, d === 'S' ? ey : ey - 1);
            }
        }
        return wrap(x + OFF[d][0], y + OFF[d][1]);
    };

    // spawn attempts seen per tile: pearls appearing before the first turn of a round, merged over mirror pairs
    const attempts = Array.from({ length: W * H }, () => []);
    for (let r = 0; r < match.maxRound; r++) {
        for (const ev of match.deltaAt(r)) {
            if (ev.type === 'turnStart') break;
            if (ev.type === 'tileChange' && ev.hasPearl) {
                const m = sm.mirrorTile(ev.tile.x, ev.tile.y);
                for (const t of [idx(ev.tile.x, ev.tile.y), idx(m.x, m.y)])
                    if (attempts[t][attempts[t].length - 1] !== r) attempts[t].push(r);
            }
        }
    }
    const countdown = (t, r) => { // rounds until the next seen attempt after round r; -1 if it never spawns
        const a = attempts[t];
        if (!a.length) return -1;
        for (const x of a) if (x > r) return x - r;
        return 99;
    };

    const born = new Map(), parent = new Map();
    for (const d of match.roundAt(0).bodies.dragons.values()) { born.set(d.id, 0); parent.set(d.id, -1); }

    for (let r = 0; r < match.maxRound; r++) {
        const state = match.roundAt(r).copy();
        // the team's longest dragon at round start, and BFS distances from its head on the static map
        let king = null; // copied: the state's dragon objects change as the round's events apply
        for (const d of state.bodies.dragons.values())
            if (d.team === T && (!king || d.body.length > king.len || (d.body.length === king.len && d.id < king.id)))
                king = { id: d.id, len: d.body.length, x: d.body[0].x, y: d.body[0].y };
        const kdist = king ? bfsStatic(state.map, [king.x, king.y]) : null;
        let cur = null;
        const flush = () => { if (cur) out.push(JSON.stringify(cur)); cur = null; };
        for (const ev of match.deltaAt(r)) {
            if (ev.type === 'turnStart') {
                flush();
                const me = state.bodies.getById(ev.id);
                if (me && me.team === T) cur = snapshot(state, me, r, king, kdist);
            } else if (cur) {
                if (ev.type === 'dragonAction' && ev.id === cur.id) {
                    const a = ev.action;
                    cur.act = a.kind === 'move' ? a.steps.join('') : a.kind === 'split' ? 'P' + a.childSegmentCount : 'X';
                } else if (ev.type === 'tileChange' && !ev.hasPearl) {
                    cur.ate++; // only the dragon on turn can remove a pearl
                } else if (ev.type === 'dragonDeath' && ev.id === cur.id) {
                    cur.die = ev.reason;
                } else if (ev.type === 'sonarPing' && ev.senderId === cur.id) {
                    const h = ev.hitKind ? ev.hitKind[0] + (ev.hitKind.endsWith('Head') ? 'H' : '') : '-';
                    cur.son.push(ev.direction + h);
                }
            }
            if (ev.type === 'dragonSplit') { born.set(ev.childId, r); parent.set(ev.childId, ev.parentId); }
            state.applyEvents([ev]);
        }
        flush();
    }

    function bfsStatic(map, [sx, sy]) {
        const dist = new Int16Array(W * H).fill(-1);
        dist[idx(sx, sy)] = 0;
        const q = [[sx, sy]];
        for (let i = 0; i < q.length; i++) {
            const [x, y] = q[i];
            const dd = dist[idx(x, y)];
            for (const d of DIRS) {
                const n = step(map, x, y, d);
                if (!n || dist[idx(n[0], n[1])] >= 0) continue;
                dist[idx(n[0], n[1])] = dd + 1;
                q.push(n);
            }
        }
        return dist;
    }

    function snapshot(state, me, r, king, kdist) {
        const map = state.map;
        const occ = new Map(); // "x,y" -> part
        let uc = 0, ec = 0;
        for (const d of state.bodies.dragons.values()) {
            if (d.team === T) uc++; else ec++;
            d.body.forEach((p, i) => occ.set(`${p.x},${p.y}`, { id: d.id, team: d.team, head: i === 0, tail: i === d.body.length - 1, len: d.body.length }));
        }
        const [hx, hy] = [me.body[0].x, me.body[0].y];
        const code = (x, y) => {
            const o = occ.get(`${x},${y}`);
            if (o) {
                if (o.id === me.id) return o.tail ? 't' : 'b';
                if (o.team === T) return o.head ? 'A' : 'a';
                return o.head ? 'E' : 'e';
            }
            return map.tileAt(x, y) === 2 ? '*' : '.';
        };
        const around = (x, y) => DIRS.map(d => { const s = step(map, x, y, d); return s ? code(s[0], s[1]) : 'K'; }).join('');
        const dest = DIRS.map(d => step(map, hx, hy, d));
        const nb = around(hx, hy);
        const room = dest.map((s, i) => (s && '.*'.includes(nb[i]) ? flood(map, occ, s, 40) : 0));

        const rel = (x, y) => { // offset from the head, wrapped into [-W/2, W/2)
            let dx = x - hx, dy = y - hy;
            dx = ((dx + Math.floor(W / 2)) % W + W) % W - Math.floor(W / 2);
            dy = ((dy + Math.floor(H / 2)) % H + H) % H - Math.floor(H / 2);
            return [dx, dy];
        };
        const inView = (x, y) => { const [dx, dy] = rel(x, y); return Math.abs(dx) <= 3 && Math.abs(dy) <= 3; };
        let pv = 0; const eh = [], ehAbs = []; let ah = 0; const ahs = [];
        for (let dy = -3; dy <= 3; dy++) for (let dx = -3; dx <= 3; dx++) {
            const [x, y] = wrap(hx + dx, hy + dy);
            if (map.tileAt(x, y) === 2) pv++;
            const o = occ.get(`${x},${y}`);
            if (o && o.head && o.id !== me.id) {
                if (o.team === T) { ah++; ahs.push([dx, dy, o.len, o.id]); } else { eh.push([dx, dy, o.len]); ehAbs.push([dx, dy]); }
            }
        }
        // BFS inside the view from each first step; ordinary edges only (kelp and portals stop it), bodies block
        const pdd = [], cdd = [];
        dest.forEach((s, i) => {
            pdd.push(-1); cdd.push(-1);
            if (!s || !'.*'.includes(nb[i]) || map.edgeAt(hx, hy, DIRS[i]) !== 0) return;
            const seen = new Set([`${hx},${hy}`, `${s[0]},${s[1]}`]);
            let frontier = [s];
            for (let dist = 1; frontier.length && dist <= 12; dist++) {
                const next = [];
                for (const [x, y] of frontier) {
                    const t = idx(x, y);
                    if (map.tileAt(x, y) === 2) {
                        if (pdd[i] < 0) pdd[i] = dist;
                        if (cdd[i] < 0 || dist < cdd[i]) cdd[i] = dist;
                    } else {
                        const c = countdown(t, r);
                        if (c >= 0) { const v = Math.max(dist, c); if (cdd[i] < 0 || v < cdd[i]) cdd[i] = v; }
                    }
                    for (const d of DIRS) {
                        if (map.edgeAt(x, y, d) !== 0) continue;
                        const [nx, ny] = wrap(x + OFF[d][0], y + OFF[d][1]);
                        const k = `${nx},${ny}`;
                        if (seen.has(k) || occ.has(k) || !inView(nx, ny)) continue;
                        seen.add(k); next.push([nx, ny]);
                    }
                }
                frontier = next;
            }
        });
        let pd = -1, pdir = '';
        pdd.forEach((v, i) => { if (v > 0 && (pd < 0 || v < pd)) pd = v; });
        pdd.forEach((v, i) => { if (v > 0 && v === pd) pdir += DIRS[i]; });
        const ehd = DIRS.map((d, i) => {
            const [sx, sy] = [OFF[d][0], OFF[d][1]];
            let best = 99;
            for (const [ex, ey] of ehAbs) best = Math.min(best, Math.abs(ex - sx) + Math.abs(ey - sy));
            return best;
        });
        const tail = me.body[me.body.length - 1];
        let tf = '';
        if (me.body.length >= 2) {
            const b = me.body[me.body.length - 2];
            tf = DIRS.find(d => { const s = step(map, b.x, b.y, d); return s && s[0] === tail.x && s[1] === tail.y; }) || '';
        }
        return {
            g: meta.id, m: meta.map, W, H, r, id: me.id, par: parent.get(me.id) ?? -1, age: r - (born.get(me.id) ?? 0),
            len: me.body.length, x: hx, y: hy, f: me.facing, uc, ec, nb, room, pv, pd, pdir, pdd, cdd, eh, ah, ahs, ehd,
            king: king ? [king.id, king.len] : null,
            kd: kdist ? kdist[idx(hx, hy)] : -1,
            kdd: dest.map(s => (s && kdist ? kdist[idx(s[0], s[1])] : -1)),
            tail: [tail.x, tail.y], tf, tnb: around(tail.x, tail.y),
            act: '', ate: 0, die: '', son: [],
        };
    }
    function flood(map, occ, s, cap) {
        const seen = new Set([`${s[0]},${s[1]}`]);
        const q = [s];
        for (let i = 0; i < q.length && seen.size < cap; i++) {
            const [x, y] = q[i];
            for (const d of DIRS) {
                const n = step(map, x, y, d);
                if (!n) continue;
                const k = `${n[0]},${n[1]}`;
                if (seen.has(k) || occ.has(k)) continue;
                seen.add(k); q.push(n);
            }
        }
        return Math.min(seen.size, cap);
    }
}

const dir = process.argv[2];
const arg = (k) => (process.argv.includes(k) ? process.argv[process.argv.indexOf(k) + 1] : null);
const since = arg('--since') || '', until = arg('--until') || '9999';
const games = JSON.parse(fs.readFileSync(path.join(dir, 'games.json'), 'utf8'));
for (const g of games) {
    const file = path.join(dir, `${g.id}.replay`);
    if (!fs.existsSync(file) || g.at < since || g.at >= until) continue;
    const out = [];
    game(file, g.side.toUpperCase(), { id: g.id, map: g.mapName }, out);
    process.stdout.write(out.join('\n') + '\n');
    process.stderr.write(`${g.id} ${g.mapName}: ${out.length} turns\n`);
}
