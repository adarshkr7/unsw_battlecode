"""Per-map head-to-head from a folder of replays (e.g. a tourney's replay folder).

    python tools/replay_summary.py results/tourney_tmp_1234 v4 v2d

BOT1/BOT2 match the start of the bot names the engine recorded (e.g. "v4" matches v4-842ae....wasm).
File names must start with the map name followed by "-s" (tourney.py names them that way).
"""

import collections
import json
import math
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
folder, bot1, bot2 = sys.argv[1], sys.argv[2], sys.argv[3]
done = subprocess.run(["node", str(ROOT / "tools" / "replay_results.cjs"), folder], capture_output=True, text=True,
                      check=True)
games = json.loads(done.stdout)


def side_of(bots, name):
    return next((t for t in "AB" if bots[t].startswith(name + "-") or bots[t] == name + ".wasm"), None)


def wilson(w, n, z=1.96):
    if n == 0:
        return 0.0, 0.0
    p = w / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return c - h, c + h


per_map = collections.OrderedDict()
tot = collections.Counter()
longest = collections.defaultdict(list)
for g in games:
    if "error" in g:
        print("unreadable:", g["file"], g["error"])
        continue
    s1, s2 = side_of(g["bots"], bot1), side_of(g["bots"], bot2)
    if not s1 or not s2 or s1 == s2:
        continue
    name = g["file"].split("-s")[0]
    m = per_map.setdefault(name, collections.Counter())
    res = "D" if g["winner"] is None else ("W" if g["winner"] == s1 else "L")
    m[res] += 1
    tot[res] += 1
    tot[(res, g["endReason"])] += 1
    if g["endReason"] == "length":
        longest[name].append((g[s1]["longest"], g[s2]["longest"]))

print(f"{'map':16s} {bot1}-{bot2} W-D-L   longest at 500 ({bot1}/{bot2})")
for name, c in sorted(per_map.items(), key=lambda kv: kv[0].lower()):
    ls = longest.get(name, [])
    lstr = f"{sum(a for a, _ in ls) / len(ls):5.1f} / {sum(b for _, b in ls) / len(ls):5.1f}" if ls else "  (eliminations)"
    print(f"{name:16s} {c['W']:2d}-{c['D']}-{c['L']:<2d}          {lstr}")
n = tot["W"] + tot["D"] + tot["L"]
score = tot["W"] + 0.5 * tot["D"]
lo, hi = wilson(score, n)
print(f"TOTAL {tot['W']}-{tot['D']}-{tot['L']} of {n}: {bot1} scores {score / n:.1%} (95% CI {lo:.1%}-{hi:.1%})")
print(f"  by elimination W {tot[('W', 'elimination')]} L {tot[('L', 'elimination')]};"
      f" at round 500 W {tot[('W', 'length')]} L {tot[('L', 'length')]}")
