// usage: node tools/ladder_deaths.cjs <team folder> <game id>... [--until 150]
// Both teams' deaths by cause up to a round (default 150), split into head-on deaths that
// traded with an enemy head and ones that hit something else, plus each team's splits.
const fs = require('fs'); const path = require('path');
const lib = require(path.join(__dirname, 'replaylib.cjs'));
const args = process.argv.slice(2);
const dir = args[0];
const until = args.includes('--until') ? +args[args.indexOf('--until') + 1] : 150;
const ids = args.slice(1).filter((a, i, all) => a !== '--until' && all[i - 1] !== '--until');
const games = JSON.parse(fs.readFileSync(path.join(dir, 'games.json'), 'utf8'));
for (const id of ids) {
    const g = games.find(x => String(x.id) === id);
    const us = g.side.toUpperCase();
    const m = lib.decode(path.join(dir, `${id}.replay`)).match;
    const stat = { A: {}, B: {} }, splits = { A: 0, B: 0 }, ate = { A: 0, B: 0 };
    const team = new Map();
    for (const d of m.roundAt(0).bodies.dragons.values()) team.set(d.id, d.team);
    for (let r = 0; r < Math.min(until, m.maxRound); r++) {
        let cur = null;
        for (const e of m.deltaAt(r)) {
            if (e.type === 'turnStart') cur = e.id;
            if (e.type === 'dragonSplit') { team.set(e.childId, team.get(e.parentId)); splits[team.get(e.parentId)]++; }
            if (e.type === 'tileChange' && !e.hasPearl && cur !== null && team.get(cur)) ate[team.get(cur)]++;
            if (e.type === 'dragonDeath') { const t = team.get(e.id); stat[t][e.reason] = (stat[t][e.reason] || 0) + 1; }
        }
    }
    const fmt = t => Object.entries(stat[t]).sort((a, b) => b[1] - a[1]).map(([k, v]) => `${k}${v}`).join(' ') + ` | splits ${splits[t]} pearls ${ate[t]}`;
    console.log(`${id} ${g.mapName} vs ${g.opp} (to r${until}): ours ${fmt(us)}  ||  theirs ${fmt(us === 'A' ? 'B' : 'A')}`);
}
