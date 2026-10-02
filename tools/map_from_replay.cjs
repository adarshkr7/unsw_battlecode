// usage: node tools/map_from_replay.cjs <game.replay>... --out <folder>
// Rebuild a playable .map from ladder replays. Ladder replays carry the map's walls, portals,
// symmetry and start bodies but blank every tile's pearl gaps (TILE x y 0 0), so the gaps are
// estimated from the pearl spawns the replay shows: a pearl appearing at the start of a round,
// before any dragon moves, is a spawn (death drops happen during turns). Mirrored tiles share a
// countdown, so their spawns are pooled. A tile seen to spawn gets [min, max] of the intervals
// between its spawns (one spawn only: its first round, as both bounds); tiles never seen to
// spawn get 0 0. Several replays of the same map are pooled. Writes <folder>/<name>.map.
const fs = require('fs');
const path = require('path');
const lib = require(path.join(__dirname, 'replaylib.cjs'));

const args = process.argv.slice(2);
const out = args.includes('--out') ? args[args.indexOf('--out') + 1] : 'maps_ladder';
const files = args.filter((a, i) => a !== '--out' && args[i - 1] !== '--out');
fs.mkdirSync(out, { recursive: true });

const maps = new Map(); // name -> {text, W, H, sym, spawns: Map(tile -> [rounds per replay])}
for (const file of files) {
    const rep = lib.decode(file);
    const text = rep.map;
    const lines = text.split('\n');
    const name = (lines.find(l => l.startsWith('MAP_NAME')) || 'MAP_NAME unknown').slice(9).trim();
    const [, W, H] = lines[0].trim().split(/\s+/).map(Number);
    const sym = (lines.find(l => l.startsWith('SYMMETRY')) || 'SYMMETRY none').split(/\s+/)[1];
    if (!maps.has(name)) maps.set(name, { text, W, H, sym, series: [] });
    const entry = maps.get(name);
    const m = rep.match;
    const spawns = new Map();
    for (let r = 0; r < m.maxRound; r++) {
        for (const e of m.deltaAt(r)) {
            if (e.type === 'turnStart') break; // spawns come before the first turn of the round
            if (e.type === 'tileChange' && e.hasPearl) {
                const t = e.tile.y * W + e.tile.x;
                if (!spawns.has(t)) spawns.set(t, []);
                spawns.get(t).push(r);
            }
        }
    }
    entry.series.push(spawns);
}

const mirror = (t, W, H, sym) => {
    const x = t % W, y = Math.floor(t / W);
    if (sym === 'x') return y * W + (W - 1 - x);
    if (sym === 'y') return (H - 1 - y) * W + x;
    if (sym === 'xy') return (H - 1 - y) * W + (W - 1 - x);
    return t;
};

for (const [name, e] of maps) {
    const { W, H, sym } = e;
    const gap = new Map();
    for (let t = 0; t < W * H; t++) {
        const m = mirror(t, W, H, sym);
        let lo = Infinity, hi = 0, seen = false;
        for (const spawns of e.series) {
            // Mirror tiles share one countdown: an attempt on either is an attempt on both.
            const rounds = [...new Set([...(spawns.get(t) || []), ...(m !== t ? spawns.get(m) || [] : [])])].sort((a, b) => a - b);
            if (!rounds.length) continue;
            seen = true;
            if (rounds.length === 1) { lo = Math.min(lo, Math.max(1, rounds[0])); hi = Math.max(hi, Math.max(1, rounds[0])); }
            for (let i = 1; i < rounds.length; i++) {
                const d = rounds[i] - rounds[i - 1];
                lo = Math.min(lo, d); hi = Math.max(hi, d);
            }
        }
        if (seen) gap.set(t, [lo, Math.max(lo, hi)]);
    }
    const lines = e.text.split('\n').map(l => {
        if (!l.startsWith('TILE ')) return l;
        const p = l.trim().split(/\s+/);
        const t = +p[2] * W + +p[1];
        const g = gap.get(t);
        return g ? `TILE ${p[1]} ${p[2]} ${g[0]} ${g[1]}` : `TILE ${p[1]} ${p[2]} 0 0`;
    });
    const file = path.join(out, name.toLowerCase().replace(/[^a-z0-9]+/g, '_') + '.map');
    fs.writeFileSync(file, lines.join('\n'));
    console.log(`${file}: ${W}x${H} sym ${sym}, ${e.series.length} replay(s), ${gap.size} spawning tiles`);
}
