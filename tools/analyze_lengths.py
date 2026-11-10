"""Per-map mean final length / surviving dragons for bot1 vs bot2 from a tourney --json file."""
import collections, json, sys
R = json.load(open(sys.argv[1]))
L = collections.defaultdict(lambda: {1: [], 2: []}); D = collections.defaultdict(lambda: {1: [], 2: []})
W = collections.defaultdict(collections.Counter)
for r in R:
    f = r.get("final")
    if not f: continue
    t1 = r["bot1_team"]; t2 = "B" if t1 == "A" else "A"
    for k, t in ((1, t1), (2, t2)):
        L[r["name"]][k].append(f["a_length" if t == "A" else "b_length"])
        D[r["name"]][k].append(f["a_dragons" if t == "A" else "b_dragons"])
    W[r["name"]]["W" if r["winner"] == t1 else "D" if r["winner"] == "draw" else "L"] += 1
mean = lambda xs: sum(xs) / len(xs) if xs else 0
print(f"{'map':16s} {'W-D-L':>7s}  {'len1':>6s} {'len2':>6s}  {'drg1':>5s} {'drg2':>5s}")
for m in sorted(L):
    w = W[m]
    print(f"{m:16s} {w['W']:>2d}-{w['D']}-{w['L']:<2d}  {mean(L[m][1]):6.1f} {mean(L[m][2]):6.1f}  {mean(D[m][1]):5.1f} {mean(D[m][2]):5.1f}")
