// usage: node tools/queen_stats.cjs <folder of replays named ...-bot1A/-bot2A.replay>
// Queen stats for one bot over a folder of replays from tourney --keep-replays/--replays.
// Replay names end in -bot1A / -bot2A: which bot played team A. Prints, per game, bot1's queen
// fate and the other's, and totals: survival to 500, mean length at death/end, enemy queen kills.
const path = require('path'); const fs = require('fs');
const lib = require(require('path').join(__dirname, 'replaylib.cjs'));
const dir = process.argv[2];
const files = fs.readdirSync(dir).filter(f => f.endsWith('.replay'));
const tot = { n: 0, alive: 0, len: 0, early: 0, oppAlive: 0, oppLen: 0, max: 0 };
for (const f of files) {
    const m = lib.decode(path.join(dir, f)).match;
    const bot1A = /bot1A/.test(f);
    const q = {};
    for (const d of m.roundAt(0).bodies.dragons.values()) if (d.id <= 1) q[d.team] = { id: d.id, died: null, len: d.body.length, max: d.body.length };
    for (let r = 0; r < m.maxRound; r++) {
        const st = m.roundAt(r);
        for (const t of ['A', 'B']) {
            const d = st.bodies.getById(q[t].id);
            if (d) { q[t].len = d.body.length; q[t].max = Math.max(q[t].max, d.body.length); }
        }
        for (const ev of m.deltaAt(r)) if (ev.type === 'dragonDeath' && ev.id <= 1) for (const t of ['A', 'B']) if (q[t].id === ev.id && !q[t].died) q[t].died = { r, reason: ev.reason };
    }
    const me = q[bot1A ? 'A' : 'B'], op = q[bot1A ? 'B' : 'A'];
    const alive = !me.died && m.maxRound >= 499;
    tot.n++; tot.alive += alive ? 1 : 0; tot.len += me.len; tot.max += me.max; tot.early += me.died && me.died.r < 10 ? 1 : 0;
    tot.oppAlive += !op.died ? 1 : 0;
    console.log(f.padEnd(36), 'ours', me.died ? `died r${me.died.r} ${me.died.reason} len ${me.len} (max ${me.max})` : `alive len ${me.len}`,
        '| theirs', op.died ? `died r${op.died.r} ${op.died.reason}` : `alive len ${op.len}`, '| rounds', m.maxRound);
}
console.log(`\n${tot.n} games: our queen alive at end ${tot.alive}, died before r10 ${tot.early}, mean final/death length ${(tot.len / tot.n).toFixed(1)}, mean max ${(tot.max / tot.n).toFixed(1)}; their queen alive ${tot.oppAlive}`);
