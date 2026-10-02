#!/bin/bash
# Fetch our team's (AllRed, id 40) games since a UTC time, download replays, and print the
# record plus an analysis of every loss (tools/ladder_losses.cjs).
# usage: tools/check_ladder.sh 2026-10-02T05:30Z
cd /d/CODE/unsw_battlecode
since=${1:?usage: check_ladder.sh <UTC time, e.g. 2026-10-02T05:30Z>}
PY=~/AppData/Roaming/uv/tools/unswbc/Scripts/python.exe
PYTHONIOENCODING=utf-8 $PY tools/team_games.py 40 --since "$since" --replays > /dev/null 2>&1
PYTHONIOENCODING=utf-8 python - "$since" <<'PYEOF'
import json, sys, collections
since = sys.argv[1].rstrip('Z')
g = [x for x in json.load(open('results/teams/AllRed/games.json', encoding='utf-8')) if x['at'] >= since and x['status'] == 'completed']
c = collections.Counter(x['result'] for x in g)
print(f"{len(g)} games since {since}: {c['win']} won, {c['loss']} lost, {c.get('draw', 0)} drawn")
by_map = collections.defaultdict(lambda: [0, 0])
for x in g:
    by_map[x['mapName']][0 if x['result'] == 'win' else 1] += 1
print('  ' + ', '.join(f"{m} {w}-{l}" for m, (w, l) in sorted(by_map.items())))
PYEOF
node tools/ladder_losses.cjs results/teams/AllRed --since "${since%Z}"
