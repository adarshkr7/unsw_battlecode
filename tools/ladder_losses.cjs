// usage: node tools/ladder_losses.cjs <team folder from team_games.py> [--since 2026-10-02T05:30] [--all]
// For each of the team's games (losses only unless --all): how it ended, both queens' fates
// (death round, cause, length, and for a head-on death the killer's length, steps moved and
// where it stood), and both teams' dragon counts and longest dragon over time.
const fs = require('fs');
const path = require('path');
const lib = require(path.join(__dirname, 'replaylib.cjs'));

const args = process.argv.slice(2);
const dir = args[0];
const since = args.includes('--since') ? args[args.indexOf('--since') + 1] : '';
const all = args.includes('--all');
const games = JSON.parse(fs.readFileSync(path.join(dir, 'games.json'), 'utf8'))
    .filter(g => g.status === 'completed' && g.at >= since && (all || g.result === 'loss'))
    .sort((a, b) => a.at.localeCompare(b.at));

const CAUSE = { W: 'wall', S: 'self', O: 'other body', H: 'head-on', A: 'no valid action' };
for (const g of games) {
    const file = path.join(dir, `${g.id}.replay`);
    if (!fs.existsSync(file)) { console.log(`${g.id}: no replay`); continue; }
    const rep = lib.decode(file); const m = rep.match; const b = new lib.Board(rep.map);
    const us = g.side.toUpperCase(), them = us === 'A' ? 'B' : 'A';
    const q = {};
    for (const d of m.roundAt(0).bodies.dragons.values()) if (d.id <= 1) q[d.team] = { id: d.id, len: d.body.length, max: d.body.length, died: null };
    const timeline = [];
    for (let r = 0; r < m.maxRound; r++) {
        const st = m.roundAt(r);
        for (const t of ['A', 'B']) {
            const d = q[t] && st.bodies.getById(q[t].id);
            if (d) { q[t].len = d.body.length; q[t].max = Math.max(q[t].max, d.body.length); }
        }
        if (r % 100 === 0 || r === m.maxRound - 1) {
            const row = { r, A: [0, 0], B: [0, 0] };
            for (const d of st.bodies.dragons.values()) { row[d.team][0]++; row[d.team][1] = Math.max(row[d.team][1], d.body.length); }
            timeline.push(row);
        }
        const ev = m.deltaAt(r);
        for (const e of ev) {
            if (e.type !== 'dragonDeath' || e.id > 1) continue;
            const t = q.A && q.A.id === e.id ? 'A' : 'B';
            if (q[t].died) continue;
            q[t].died = { r, reason: e.reason };
            if (e.reason === 'H') { // the other head that died head-on this round
                const me = st.bodies.getById(e.id);
                const acts = {}; for (const x of ev) if (x.type === 'dragonAction') acts[x.id] = x.action;
                for (const x of ev) {
                    if (x.type !== 'dragonDeath' || x.id === e.id || x.reason !== 'H') continue;
                    const k = st.bodies.getById(x.id);
                    if (!k || k.team === t || !me) continue;
                    let dx = k.body[0].x - me.body[0].x, dy = k.body[0].y - me.body[0].y;
                    if (dx > b.W / 2) dx -= b.W; if (dx < -b.W / 2) dx += b.W; if (dy > b.H / 2) dy -= b.H; if (dy < -b.H / 2) dy += b.H;
                    const a = acts[x.id], qa = acts[e.id];
                    q[t].died.killer = `len ${k.body.length} at ${dx},${dy}${Math.max(Math.abs(dx), Math.abs(dy)) <= 3 ? ' (in view)' : ' (unseen)'} moved ${a && a.kind === 'move' ? a.steps.join('') : '?'}; queen moved ${qa && qa.kind === 'move' ? qa.steps.join('') : qa ? qa.kind : '?'}`;
                    break;
                }
            }
        }
    }
    const fate = t => {
        const x = q[t];
        if (!x) return '?';
        return x.died ? `died r${x.died.r} ${CAUSE[x.died.reason] || x.died.reason} at len ${x.len} (max ${x.max})${x.died.killer ? ' — killer ' + x.died.killer : ''}`
            : `alive, len ${x.len} (max ${x.max})`;
    };
    console.log(`\n${g.id} ${g.at.slice(0, 16)} ${g.mapName} vs ${g.opp}: ${g.result.toUpperCase()} at round ${m.maxRound} (we were ${us}, ${b.W}x${b.H})`);
    console.log(`  our queen:   ${fate(us)}`);
    console.log(`  their queen: ${fate(them)}`);
    console.log('  dragons/longest: ' + timeline.map(x => `r${x.r} ${x[us][0]}/${x[us][1]} v ${x[them][0]}/${x[them][1]}`).join('  '));
}
