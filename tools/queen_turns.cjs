// usage: node tools/queen_turns.cjs <game.replay> <dragon id> <from round> <to round>
// The last rounds of one dragon: its head, length, action, and enemy heads within 7 steps
// (Manhattan, wrapped) as id:len@dx,dy, plus what those enemies did that round.
const lib = require(require('path').join(__dirname, 'replaylib.cjs'));
const [file, id, from, to] = [process.argv[2], +process.argv[3], +process.argv[4], +process.argv[5]];
const rep = lib.decode(file); const m = rep.match; const b = new lib.Board(rep.map);
for (let r = from; r <= to && r < m.maxRound; r++) {
    const st = m.roundAt(r);
    const me = st.bodies.getById(id);
    if (!me) { console.log('r' + r, 'dead'); break; }
    const hx = me.body[0].x, hy = me.body[0].y;
    const near = [];
    const acts = {};
    for (const ev of m.deltaAt(r)) if (ev.type === 'dragonAction') acts[ev.id] = ev.action.kind === 'move' ? ev.action.steps.join('') : ev.action.kind[0].toUpperCase() + (ev.action.childSegmentCount || '');
    const deaths = {};
    for (const ev of m.deltaAt(r)) if (ev.type === 'dragonDeath') deaths[ev.id] = ev.reason;
    for (const d of st.bodies.dragons.values()) {
        if (d.team === me.team) continue;
        let dx = d.body[0].x - hx, dy = d.body[0].y - hy;
        if (dx > b.W / 2) dx -= b.W; if (dx < -b.W / 2) dx += b.W;
        if (dy > b.H / 2) dy -= b.H; if (dy < -b.H / 2) dy += b.H;
        if (Math.abs(dx) + Math.abs(dy) <= 7) near.push(`${d.id}:${d.body.length}@${dx},${dy}>${acts[d.id] || '?'}${deaths[d.id] ? '+' + deaths[d.id] : ''}`);
    }
    console.log(`r${r} len ${me.body.length} at ${hx},${hy} act ${acts[id] || '?'}${deaths[id] ? ' DIED ' + deaths[id] : ''} | ${near.join(' ')}`);
}
