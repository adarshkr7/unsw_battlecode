// Summarise a .replay file: result, a timeline of each team's dragons, and per-team event counts.
//
//   python tools/replay_setup.py              (once, and after upgrading unswbc)
//   node tools/replay.cjs game.replay [--every 50] [--json]
//
// Works on local replays and on ladder games downloaded from the site. Uses the toolkit's own
// viewer code to decode, so it always matches the current replay format.

const fs = require('fs');
const path = require('path');

const decoderPath = path.join(__dirname, '..', 'results', 'replay_decoder.cjs');
if (!fs.existsSync(decoderPath)) {
    console.error('run `python tools/replay_setup.py` first');
    process.exit(1);
}

// The viewer expects a browser; give it inert stand-ins for the parts it touches at load time.
const noop = () => {};
const inert = { get: (t, k) => (k in t ? t[k] : new Proxy(noop, inert)), apply: () => new Proxy(noop, inert), construct: () => new Proxy({}, inert) };
globalThis.window = new Proxy({ addEventListener: noop, removeEventListener: noop, matchMedia: () => ({ matches: false, addEventListener: noop }) }, inert);
globalThis.document = new Proxy({}, inert);
globalThis.navigator = { userAgent: 'node' };
globalThis.requestAnimationFrame = noop;
globalThis.acquireVsCodeApi = () => ({ postMessage: noop, setState: noop, getState: () => null });
require(decoderPath);

function summarise(file, every, resultOnly) {
    const bytes = fs.readFileSync(file);
    const replay = globalThis.__replay.decode(new Uint8Array(bytes).buffer);
    if (resultOnly) {
        const at = {};
        for (const r of atRounds) {
            const k = Math.min(r, replay.match.maxRound);
            const row = { A: { alive: 0, total: 0, longest: 0 }, B: { alive: 0, total: 0, longest: 0 } };
            for (const d of replay.match.roundAt(k).bodies.dragons.values()) {
                row[d.team].alive++;
                row[d.team].total += d.body.length;
                row[d.team].longest = Math.max(row[d.team].longest, d.body.length);
            }
            at[r] = row;
        }
        return { file: path.basename(file), result: replay.result, at };
    }
    const match = replay.match;
    const teamOf = new Map();
    const timeline = [];
    for (let r = 0; r <= match.maxRound; r++) {
        const dragons = match.roundAt(r).bodies.dragons;
        const row = { round: r, A: { alive: 0, longest: 0, total: 0 }, B: { alive: 0, longest: 0, total: 0 } };
        for (const d of dragons.values()) {
            teamOf.set(d.id, d.team);
            const t = row[d.team];
            t.alive++;
            t.total += d.body.length;
            t.longest = Math.max(t.longest, d.body.length);
        }
        if (r % every === 0 || r === match.maxRound)
            timeline.push(row);
    }
    const counts = { A: {}, B: {} };
    const bump = (team, key) => {
        if (team === 'A' || team === 'B')
            counts[team][key] = (counts[team][key] || 0) + 1;
    };
    for (const e of replay.events) {
        if (e.type === 'dragonDeath')
            bump(teamOf.get(e.id), 'death:' + e.reason);
        else if (e.type === 'dragonSplit')
            bump(e.team ?? teamOf.get(e.parentId), 'splits');
        else if (e.type === 'dragonIndicator')
            bump(teamOf.get(e.id), 'label:' + e.text.replace(/[0-9 ]+$/, ''));
        else if (e.type === 'sonarPing')
            bump(teamOf.get(e.sender ?? e.id), 'sonar');
        else if (e.type === 'dragonAction') {
            const a = e.action || {};
            if (a.kind === 'move' && a.steps && a.steps.length > 1)
                bump(teamOf.get(e.id), 'sprints');
            if (a.kind === 'suicide')
                bump(teamOf.get(e.id), 'suicides');
        }
    }
    return { file: path.basename(file), result: replay.result, bots: { A: replay.teams.A.botId, B: replay.teams.B.botId }, timeline, counts };
}

const args = process.argv.slice(2);
const atRounds = args.includes('--at') ? args[args.indexOf('--at') + 1].split(',').map(Number) : [];
const every = args.includes('--every') ? Number(args[args.indexOf('--every') + 1]) : 50;
const files = args.filter((a, i) => a.endsWith('.replay') || a.endsWith('.public'));
const out = files.map(f => summarise(f, every, args.includes('--result-only')));
if (args.includes('--json')) {
    console.log(JSON.stringify(out, null, 1));
} else {
    for (const s of out) {
        const r = s.result;
        console.log(`${s.file}: ${s.bots.A} (A) vs ${s.bots.B} (B) -- winner ${r.winner ?? 'none'} by ${r.endReason}`);
        console.log(`  final   A: ${r.teamA.dragonCount} dragons, longest ${r.teamA.longestDragon}, total ${r.teamA.totalLength}` +
                    `   B: ${r.teamB.dragonCount} dragons, longest ${r.teamB.longestDragon}, total ${r.teamB.totalLength}`);
        for (const row of s.timeline)
            console.log(`  R${String(row.round).padStart(3)}  A ${String(row.A.alive).padStart(2)} alive, longest ${String(row.A.longest).padStart(3)}, total ${String(row.A.total).padStart(4)}` +
                        `   B ${String(row.B.alive).padStart(2)} alive, longest ${String(row.B.longest).padStart(3)}, total ${String(row.B.total).padStart(4)}`);
        for (const team of ['A', 'B'])
            console.log(`  ${team}: ` + Object.entries(s.counts[team]).sort().map(([k, v]) => `${k} ${v}`).join(', '));
    }
}
