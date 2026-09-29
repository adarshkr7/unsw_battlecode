// Fast results for many replays (reads only the result section, ~10 ms a file):
//
//   node tools/replay_results.cjs results/some_folder > results.json
//
// Prints a JSON list: file, bots (A/B, the bot paths the engine recorded), winner (A/B/null),
// endReason, and each team's dragons / longest / total at the end. tools/replay_summary.py
// turns it into per-map tables.

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

const dir = process.argv[2];
const files = fs.readdirSync(dir).filter(f => f.endsWith('.replay'));
const team = t => ({ dragons: t.dragonCount, longest: t.longestDragon, total: t.totalLength });
const out = [];
for (const f of files) {
    try {
        const r = globalThis.__replay.parse(new Uint8Array(fs.readFileSync(path.join(dir, f))).buffer);
        const res = r.result;
        out.push({
            file: f,
            bots: { A: path.basename(String(r.botA)), B: path.basename(String(r.botB)) },
            winner: res._isWinner ? (res.winner === 0 ? 'A' : 'B') : null,
            endReason: res.endReason === 0 ? 'elimination' : 'length',
            A: team(res.teamA),
            B: team(res.teamB),
        });
    } catch (e) {
        out.push({ file: f, error: String(e).slice(0, 120) });
    }
}
console.log(JSON.stringify(out));
