#!/bin/bash
# Head to head under the new rules: candidate vs the frozen reference v9 (both queen-aware).
# 7 maps (the doomed-queen spawns Autarky, Dilemma, Slithery Fight left out), 3 seeds, both sides.
# usage: tools/ab.sh <name> <candidate dir> [reference dir] [extra tourney args]
cd /d/CODE/unsw_battlecode
name=$1; cand=$2; ref=${3:-results/tmp_bots/v9_ref}; shift $(( $# < 3 ? $# : 3 ))
M=schooltime,portals,queen_of_spades,default,trophy,devil,trauma
S=tools
rm -rf results/v9/ab_$name
python tools/tourney.py $cand $ref --maps $M --seeds 3 --seed-base 5000 --replays results/v9/ab_$name --keep-replays --json results/v9/ab_$name.json "$@" > results/v9/ab_$name.txt 2>&1
echo "== $name: $(grep TOTAL results/v9/ab_$name.txt)"
grep -E "ended|surviving|max points|errors" results/v9/ab_$name.txt
node $S/queen_stats.cjs results/v9/ab_$name/_tmp | tail -1
