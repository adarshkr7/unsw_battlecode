// usage: node tools/queen_kills.cjs <folder of replays named ...-bot1A/-bot2A.replay>
// Every head-on death of bot1's queen: the killer's length, how many steps it moved, where it
// stood relative to our queen's head at round start, and whether that was inside her 7x7 view.
const path = require('path'); const fs = require('fs');
const lib = require(require('path').join(__dirname, 'replaylib.cjs'));
const dir = process.argv[2];
let n = 0, inView = 0, sure = 0;
for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.replay'))) {
    const rep = lib.decode(path.join(dir, f)); const m = rep.match; const b = new lib.Board(rep.map);
    const team = /bot1A/.test(f) ? 'A' : 'B';
    let qid = -1;
    for (const d of m.roundAt(0).bodies.dragons.values()) if (d.id <= 1 && d.team === team) qid = d.id;
    for (let r = 0; r < m.maxRound; r++) {
        const ev = m.deltaAt(r);
        const death = ev.find(e => e.type === 'dragonDeath' && e.id === qid);
        if (!death) continue;
        if (death.reason !== 'H' && !/head/i.test(death.reason)) break;
        const st = m.roundAt(r);
        const q = st.bodies.getById(qid);
        // the killer: another dragon of the other team dying head-on this round, last to act
        const killers = ev.filter(e => e.type === 'dragonDeath' && e.id !== qid && (e.reason === 'H' || /head/i.test(e.reason)));
        const acts = {}; for (const e of ev) if (e.type === 'dragonAction') acts[e.id] = e.action;
        for (const k of killers) {
            const kd = st.bodies.getById(k.id);
            if (!kd || kd.team === team) continue;
            let dx = kd.body[0].x - q.body[0].x, dy = kd.body[0].y - q.body[0].y;
            if (dx > b.W / 2) dx -= b.W; if (dx < -b.W / 2) dx += b.W; if (dy > b.H / 2) dy -= b.H; if (dy < -b.H / 2) dy += b.H;
            const a = acts[k.id]; const steps = a && a.kind === 'move' ? a.steps.length : 0;
            const len = kd.body.length; const reach = Math.ceil(len / 4) + Math.max(0, len - 2);
            const vis = Math.max(Math.abs(dx), Math.abs(dy)) <= 3;
            const qa = acts[qid]; const qsteps = qa && qa.kind === 'move' ? qa.steps.join('') : '?';
            n++; inView += vis ? 1 : 0;
            console.log(`${f.padEnd(34)} r${r} queen len ${q.body.length} moved ${qsteps.padEnd(4)} killer id ${k.id} len ${len} at ${dx},${dy} ${vis ? 'IN VIEW ' : 'unseen  '} moved ${steps} (reach ${reach}) acted ${k.id < qid ? 'before' : 'after'}`);
            break;
        }
        break;
    }
}
console.log(`${n} head-on queen deaths: killer in view at round start ${inView}`);
