#!/bin/bash
# Head to head for v10 changes: candidate vs a frozen reference, never with map data: the reference
# is played as a USE_MAP_DATA=false copy if it has that switch (v10 has no map data at all).
#   ladder:   the ladder maps rebuilt from replays ($LADDER, default maps_ladder), 2 seeds, both sides
#   worldgen: maps_worldgen (generated, unseen-style), 2 seeds, both sides
# usage: tools/ab_v10.sh <name> <candidate dir> [reference dir, default results/tmp_bots/v10_ref] [ladder|worldgen|both]
# Refresh the reference with `cp -r bots/v10 results/tmp_bots/v10_ref` whenever a change is accepted.
cd /d/CODE/unsw_battlecode
name=$1; cand=$2; ref=${3:-results/tmp_bots/v10_ref}; which=${4:-both}
LADDER=${LADDER:-maps_ladder}
OUT=results/v10; mkdir -p $OUT
for b in cand ref; do
  src=${!b}; dst=results/tmp_bots/n10_${name}_$b
  rm -rf $dst; cp -r $src $dst
  if grep -q "USE_MAP_DATA" $dst/params.hpp; then
    sed -i 's/^inline constexpr bool USE_MAP_DATA = true;/inline constexpr bool USE_MAP_DATA = false;/' $dst/params.hpp
    grep -q "USE_MAP_DATA = false" $dst/params.hpp || { echo "no-map switch failed for $dst"; exit 1; }
  fi
done
run() { # tag mapsdir seedbase
  rm -rf $OUT/$1_$name
  python tools/tourney.py results/tmp_bots/n10_${name}_cand results/tmp_bots/n10_${name}_ref --maps-dir "$2" --seeds 2 --seed-base $3 --replays $OUT/$1_$name --keep-replays --json $OUT/$1_$name.json > $OUT/$1_$name.txt 2>&1
  echo "== $name ($1, no map data): $(grep TOTAL $OUT/$1_$name.txt)"
  grep -E "ended|surviving|max points|errors" $OUT/$1_$name.txt
  node tools/queen_stats.cjs $OUT/$1_$name/_tmp | tail -1
}
[ "$which" != worldgen ] && run ladder "$LADDER" 5000
[ "$which" != ladder ] && run worldgen maps_worldgen 6000
exit 0
