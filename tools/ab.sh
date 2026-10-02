#!/bin/bash
# Head to head under the new rules: candidate vs the frozen reference v9 (both queen-aware).
# MAPS=ladder (default): the current ladder maps rebuilt from replays (maps_ladder, see
#   tools/map_from_replay.cjs), 2 seeds, both sides.
# MAPS=classic: 7 bundled ladder maps (the doomed-queen spawns Autarky, Dilemma, Slithery Fight
#   left out), 3 seeds, both sides.
# usage: [MAPS=classic] tools/ab.sh <name> <candidate dir> [reference dir] [extra tourney args]
cd /d/CODE/unsw_battlecode
name=$1; cand=$2; ref=${3:-results/tmp_bots/v9_ref}; shift $(( $# < 3 ? $# : 3 ))
if [ "${MAPS:-ladder}" = classic ]; then
  set -- --maps schooltime,portals,queen_of_spades,default,trophy,devil,trauma --seeds 3 "$@"
else
  set -- --maps-dir maps_ladder --seeds 2 "$@"
fi
rm -rf results/v9/ab_$name
python tools/tourney.py $cand $ref "$@" --seed-base 5000 --replays results/v9/ab_$name --keep-replays --json results/v9/ab_$name.json > results/v9/ab_$name.txt 2>&1
echo "== $name: $(grep TOTAL results/v9/ab_$name.txt)"
grep -E "ended|surviving|max points|errors" results/v9/ab_$name.txt
node tools/queen_stats.cjs results/v9/ab_$name/_tmp | tail -1
