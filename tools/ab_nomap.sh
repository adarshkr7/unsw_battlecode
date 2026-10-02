#!/bin/bash
# Head to head without map data (as in the qualifiers): both bots get USE_MAP_DATA=false copies,
# played on the 12 generated maps in maps_worldgen, 2 seeds, both sides.
# usage: tools/ab_nomap.sh <name> <candidate dir> [reference dir]
cd /d/CODE/unsw_battlecode
name=$1; cand=$2; ref=${3:-results/tmp_bots/v9_ref}
S=tools
for b in cand ref; do
  src=${!b}; dst=results/tmp_bots/nomap_${name}_$b
  rm -rf $dst; cp -r $src $dst
  sed -i 's/^inline constexpr bool USE_MAP_DATA = true;/inline constexpr bool USE_MAP_DATA = false;/' $dst/params.hpp
  grep -q "USE_MAP_DATA = false" $dst/params.hpp || { echo "no-map switch failed for $dst"; exit 1; }
done
rm -rf results/v9/abn_$name
python tools/tourney.py results/tmp_bots/nomap_${name}_cand results/tmp_bots/nomap_${name}_ref --maps-dir maps_worldgen --seeds 2 --seed-base 6000 --replays results/v9/abn_$name --keep-replays --json results/v9/abn_$name.json > results/v9/abn_$name.txt 2>&1
echo "== $name (no map, worldgen): $(grep TOTAL results/v9/abn_$name.txt)"
grep -E "ended|surviving|max points|errors" results/v9/abn_$name.txt
node $S/queen_stats.cjs results/v9/abn_$name/_tmp | tail -1
