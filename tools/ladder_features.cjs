// usage: node tools/ladder_features.cjs <team folder> [--since 2026-10-02T11:22] [--out file.json]
// One row of numbers per finished game, for comparing wins with losses and us with each
// opponent: how it ended, both queens (alive at the end, length at the end, longest she ever
// was, round she died and how), both teams' dragon count and longest dragon at rounds 50, 100,
// 200, 300, 400 and the end, pearls eaten by rounds 100 and 300, and both teams' deaths by cause.
const fs = require('fs');
const path = require('path');
const lib = require(path.join(__dirname, 'replaylib.cjs'));

const args = process.argv.slice(2);
const dir = args[0];
const since = args.includes('--since') ? args[args.indexOf('--since') + 1] : '';
const outFile = args.includes('--out') ? args[args.indexOf('--out') + 1] : null;
const games = JSON.parse(fs.readFileSync(path.join(dir, 'games.json'), 'utf8'))
    .filter(g => g.status === 'completed' && g.at >= since && fs.existsSync(path.join(dir, `${g.id}.replay`)))
    .sort((a, b) => a.at.localeCompare(b.at));

const rows = [];
for (const g of games) {
    const m = lib.decode(path.join(dir, `${g.id}.replay`)).match;
    const us = g.side.toUpperCase(), them = us === 'A' ? 'B' : 'A';
    const team = new Map();
    const q = {};
    for (const d of m.roundAt(0).bodies.dragons.values()) {
        team.set(d.id, d.team);
        if (d.id <= 1) q[d.team] = { id: d.id, start: d.body.length, len: d.body.length, max: d.body.length, died: null, cause: null };
    }
    const snap = {};
    const ate = { A: 0, B: 0 }, ate100 = {}, ate300 = {};
    const deaths = { A: {}, B: {} };
    const splits = { A: 0, B: 0 };
    for (let r = 0; r < m.maxRound; r++) {
        const st = m.roundAt(r);
        for (const t of ['A', 'B']) {
            const d = q[t] && st.bodies.getById(q[t].id);
            if (d && !q[t].died) { q[t].len = d.body.length; q[t].max = Math.max(q[t].max, d.body.length); }
        }
        if ([50, 100, 200, 300, 400].includes(r) || r === m.maxRound - 1) {
            const row = { A: [0, 0], B: [0, 0] };
            for (const d of st.bodies.dragons.values()) { row[d.team][0]++; row[d.team][1] = Math.max(row[d.team][1], d.body.length); }
            snap[r === m.maxRound - 1 ? 'end' : r] = row;
        }
        if (r === 100) Object.assign(ate100, ate);
        if (r === 300) Object.assign(ate300, ate);
        let cur = null;
        for (const e of m.deltaAt(r)) {
            if (e.type === 'turnStart') cur = e.id;
            else if (e.type === 'dragonSplit') { team.set(e.childId, team.get(e.parentId)); splits[team.get(e.parentId)]++; }
            else if (e.type === 'tileChange' && !e.hasPearl && cur !== null && team.get(cur)) ate[team.get(cur)]++;
            else if (e.type === 'dragonDeath') {
                const t = team.get(e.id);
                if (t) deaths[t][e.reason] = (deaths[t][e.reason] || 0) + 1;
                if (e.id <= 1) for (const tt of ['A', 'B']) if (q[tt] && q[tt].id === e.id && !q[tt].died) { q[tt].died = r; q[tt].cause = e.reason; }
            }
        }
    }
    if (!ate100.A && ate100.A !== 0) Object.assign(ate100, ate);
    if (!ate300.A && ate300.A !== 0) Object.assign(ate300, ate);
    const side = t => ({
        queenAlive: !q[t].died, queenLen: q[t].died ? 0 : q[t].len, queenMax: q[t].max, queenStart: q[t].start,
        queenDied: q[t].died, queenCause: q[t].cause,
        snap: Object.fromEntries(Object.entries(snap).map(([k, v]) => [k, v[t]])),
        ate100: ate100[t], ate300: ate300[t], ate: ate[t], deaths: deaths[t], splits: splits[t],
    });
    rows.push({ id: g.id, at: g.at, map: g.mapName, opp: g.opp, oppElo: (g.side === 'a' ? g.b : g.a).elo, result: g.result,
                rounds: m.maxRound, us: side(us), them: side(them) });
}
if (outFile) fs.writeFileSync(outFile, JSON.stringify(rows, null, 1));
console.log(`${rows.length} games`);
