"""Per-map death and points breakdown for bot1 from a tourney --json file."""
import collections, json, sys
R = json.load(open(sys.argv[1]))
d = collections.defaultdict(collections.Counter); pts = collections.defaultdict(float); games = collections.Counter()
for r in R:
    t1 = r["bot1_team"]; games[r["name"]] += 1
    for team, why in r["deaths"]:
        if team == t1: d[r["name"]][why] += 1
    if t1 in r["points"]: pts[r["name"]] = max(pts[r["name"]], r["points"][t1]["max"])
for m in sorted(games, key=lambda m: -sum(d[m].values())):
    print(f"{m:16s} deaths/game {sum(d[m].values())/games[m]:6.1f}  {dict(d[m])}  maxpts {pts[m]/1e6:.1f}M")
