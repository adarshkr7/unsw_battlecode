// Fast results for many replays (reads only the result section, ~10 ms a file):
//
//   node tools/replay_results.cjs results/some_folder > results.json
//   node tools/replay_results.cjs a.replay b.replay ...       (just these files)
//
// Prints a JSON list: file (the file name), bots (A/B, the bot paths the engine recorded), winner
// (A/B/null), endReason, and each team's dragons / longest / total at the end. A file that cannot
// be parsed (e.g. still being written) gets an error entry. tools/replay_summary.py turns it into
// per-map tables.

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

const args = process.argv.slice(2);
const paths = args.length === 1 && fs.statSync(args[0]).isDirectory()
    ? fs.readdirSync(args[0]).filter(f => f.endsWith('.replay')).map(f => path.join(args[0], f))
    : args;
const team = t => ({ dragons: t.dragonCount, longest: t.longestDragon, total: t.totalLength });
const out = [];
for (const p of paths) {
    const f = path.basename(p);
    try {
        const r = globalThis.__replay.parse(new Uint8Array(fs.readFileSync(p)).buffer);
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
