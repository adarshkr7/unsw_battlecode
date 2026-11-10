"""Parameter inventory of the bot we tune (bots/v22 = the live v21 plus switches that are off).

Every tunable setting lives in the bot's params.hpp as `inline [constexpr] T NAME = value;`. Since v21
the bot plays two parameter sets: maps of SMALL_MAP_AREA (1,000) tiles or more use the declared values,
smaller maps the values set in params::useSmallMapValues(). The harness tunes the two sets separately
(a "scope" is "big" or "small"): a big-map game only says something about the big-map values.

Three kinds of entries:
  Num     a continuous or integer setting, searched in a box around the baseline value
  Switch  a strategy switch: a named bundle of settings with an alternative value per scope
          (bools, "999 = off" rounds, categorical choices); the design turns it on or off
  derived settings kept consistent by `derive()` (KING_PULL_ROUND = FEED_ROUND, ...)
"""

from __future__ import annotations

import dataclasses

SCOPES = ("big", "small")


@dataclasses.dataclass(frozen=True)
class Num:
    name: str
    typ: type          # int or float
    lo: float          # legal range
    hi: float
    span: float        # half-width of the search box around the baseline (in setting units)
    module: str
    scopes: tuple = SCOPES   # scopes where the setting is searched
    note: str = ""


@dataclasses.dataclass(frozen=True)
class Switch:
    name: str
    alt: dict          # {"big": {SETTING: value}, "small": {...}}; a scope missing = inert there
    module: str        # aggressive / defensive / economic / adaptive / structure
    note: str = ""


def both(**kv):
    return {"big": dict(kv), "small": dict(kv)}


# ---------------------------------------------------------------------------------------------
# Continuous / integer settings, grouped into modules.
NUMS = [
    # Combat: head trades, threat avoidance, hunting.
    Num("THREAT_PER_LEN", float, 10, 150, 25, "combat", note="penalty per segment we would lose to a reachable enemy head"),
    Num("THREAT_EVEN", float, 0, 60, 10, "combat", note="penalty for a tile where an even trade is possible"),
    Num("THREAT_DENSITY_PENALTY", float, 0, 400, 60, "combat", note="even-trade tile where their heads outnumber ours"),
    Num("HUNT_ATTRACT", float, 0, 250, 40, "combat", note="pull towards enemy heads worth trading with"),
    Num("HUNT_ATTRACT_AGGRO", float, 50, 400, 70, "combat", note="the same in aggressive mode"),
    Num("AGGRO_RATIO", float, 1.1, 4.0, 0.4, "combat", note="our heads / theirs in view to turn aggressive"),
    Num("TRADE_MIN_UNITS", int, 2, 14, 3, "combat", note="dragons we need before taking even trades"),
    Num("DIVE_SEG_COST", float, 50, 800, 100, "combat", note="cost per segment of a small dragon dying in a pocket"),
    Num("QUEEN_KILL_GAIN", float, 2, 30, 6, "combat", note="value of trading heads with the enemy queen"),
    Num("TRADE_DOWN_GAIN", float, 0, 1.5, 0.3, "combat", note="value of a trade-down (we lose 1 more than them)"),
    # Economy: pearl valuation, movement, exploration.
    Num("EAT_NOW", float, 150, 800, 80, "economy", note="stepping onto a pearl this turn"),
    Num("FOOD_BEST", float, 100, 700, 80, "economy", note="best discounted pearl reachable"),
    Num("FOOD_SUM", float, 0, 10, 2, "economy", note="sum of discounted pearls (richness tie-break)"),
    Num("GAMMA_BEST", float, 0.70, 0.95, 0.04, "economy", note="per-step discount for the best pearl"),
    Num("ALLY_CLOSER", float, 0.02, 0.8, 0.1, "economy", note="pearl value when an ally gets there first"),
    Num("ENEMY_CLOSER", float, 0.0, 0.6, 0.08, "economy", note="pearl value when an enemy gets there first"),
    Num("REVISIT", float, 0, 150, 20, "economy", note="penalty per unit of recent-visit heat"),
    Num("EAT_SPRINT_COST", float, 100, 800, 80, "economy", scopes=("big",), note="score cost of a paid eat-sprint step (sprints are off on small maps)"),
    Num("EXPLORE_W", float, 0, 40, 8, "economy", scopes=("big",), note="per never-seen tile brought into view (off under 900 tiles)"),
    Num("OUT_OF_VIEW_SMALL", float, 0, 600, 100, "economy", note="small dragon landing out of view through a portal"),
    Num("SWARM_SPLIT_LEN", int, 4, 6, 1, "economy", note="swarm dragons split at this length"),
    # Queen and endgame.
    Num("QUEEN_KEEPAWAY", float, 0, 400, 50, "queen", note="queen: per step inside 4 of an enemy head"),
    Num("QUEEN_LONGER_P", float, 0.2, 0.95, 0.12, "queen", note="queen: chance an unseen tail is one longer"),
    Num("QUEEN_SPLIT_UNTIL", int, 0, 200, 30, "queen", note="queen swarm-splits until this round"),
    Num("QUEEN_FREEDOM", float, 0, 200, 25, "queen", note="queen: per tile short of 16 within 3 steps"),
    Num("QUEEN_UNSEEN", float, 0, 3000, 300, "queen", note="queen: per step closer than 3 to the view edge"),
    Num("QUEEN_FOOD_RADIUS", int, -1, 6, 2, "queen", note="others leave pearls this close to our queen"),
    Num("FEED_ROUND", int, 300, 470, 25, "queen", note="feeding the king (queen) starts"),
    Num("CONSOLIDATE_ROUND", int, 360, 490, 20, "queen", note="no more swarm splits; feed down"),
    Num("FEED_MARGIN", int, 0, 10, 2, "queen", note="extra length a king needs over a feeder"),
    Num("KING_APPROACH", float, 5, 150, 15, "queen", note="feeders: per step closer to the king"),
    Num("FEED_ATTRACT", float, 100, 1000, 150, "queen", note="feeders: pull towards a king they could feed"),
    Num("QFEED_ROUND", int, 150, 420, 40, "queen", scopes=("big",), note="early queen feeding starts (off on small maps)"),
    Num("QFEED_PULL_PCT", int, 0, 40, 6, "queen", scopes=("big",), note="share of small dragons that feed her early"),
    Num("QFEED_MIN_UNITS", int, 8, 40, 6, "queen", scopes=("big",), note="dragons we need for early feeding"),
]

# ---------------------------------------------------------------------------------------------
# Strategy switches. "alt" is the value turned on; for settings whose baseline differs by scope
# (EAT_SPRINT, QFEED_ROUND) the switch flips the baseline in each scope.
SWITCHES = [
    # aggressive
    Switch("KAMIKAZE", both(KAMIKAZE=True), "aggressive", "2-3 long dragons ram any enemy head at least as long"),
    Switch("TRADE_DOWN_2", both(TRADE_DOWN_MAX=2), "aggressive", "trade down by up to 2 (baseline 1)"),
    Switch("TRADE_DOWN_OFF", both(TRADE_DOWN_MAX=0), "aggressive", "no trade-downs (v15)"),
    Switch("QUEEN_HUNT_SHARED", both(QUEEN_SHARE_AGE=15, QUEEN_HUNT_APPROACH=20.0, QUEEN_HUNT_RADIUS=12, QUEEN_HUNT_UNTIL=250),
           "aggressive", "enemy-queen sightings over sonar; hunters within 12 steps close in"),
    Switch("PACK_HUNT", both(PACK_ROUND=400), "aggressive", "from r400 half the small dragons hunt the longest enemy"),
    Switch("INTERCEPT", both(INTERCEPT=True), "aggressive", "small dragons trade with enemies near our big ones"),
    Switch("NO_SPLIT_VETO", both(SPLIT_THREAT_VETO=False), "aggressive", "swarm-split even with an enemy head in reach"),
    # defensive
    Switch("KEEPAWAY", both(KEEPAWAY=True), "defensive", "big dragons keep 4+ from enemy heads"),
    Switch("BIG_GUARD", both(BIG_GUARD_PER_LEN=40.0), "defensive", "big dragons keep a reach margin"),
    Switch("QUEEN_GUARD", both(QUEEN_GUARD=True), "defensive", "small dragons trade with enemies near our queen"),
    Switch("QUEEN_CORNERED", both(QUEEN_CORNERED=3000.0), "defensive", "queen two-round look-ahead"),
    Switch("QUEEN_STALL", both(QUEEN_STALL=True), "defensive", "queen never escape-splits while a move is open"),
    Switch("SPLIT_HEADROOM", both(SPLIT_HEADROOM=2), "defensive", "swarm splits stop 2 short of the dragon cap"),
    Switch("QUEEN_PORTAL_AVOID", both(QUEEN_PORTAL_PENALTY=2000.0), "defensive", "queen avoids portals"),
    Switch("THREAT_ON_HEADS", both(THREAT_ON_OUR_HEADS=True), "defensive", "no swarm split while an enemy can reach the head"),
    Switch("DIVE_OFF", both(DIVE=False), "defensive", "no dives into dead ends"),
    # economic
    Switch("EAT_SPRINT_FLIP", {"big": dict(EAT_SPRINT=False), "small": dict(EAT_SPRINT=True)}, "economic",
           "eat-sprints off on big maps / on on small maps"),
    Switch("SPREAD", both(SPREAD_WEIGHT=30.0), "economic", "swarm keeps away from ally heads"),
    Switch("SPAWN_CLEAR", both(SPAWN_CLEAR=100.0), "economic", "don't stand on tiles about to spawn"),
    Switch("EXPLORE_FAR", both(EXPLORE_FAR_W=20.0), "economic", "small dragons head for unseen ground (900+ tiles)"),
    Switch("SCOUTS", both(SCOUT_FRACTION=0.15), "economic", "15% of small dragons scout portals"),
    Switch("CHAIN_SPLIT", both(CHAIN_SPLIT=True), "economic", "children of 4+ split again at once"),
    Switch("QUEEN_NO_EXPLORE", both(QUEEN_EXPLORE=False), "economic", "queen gets no exploration bonus"),
    Switch("PORTAL_EXPLORE_OFF", both(PORTAL_EXPLORE_MAX_LEN=0), "economic", "small dragons never take unknown portals"),
    Switch("QFEED_FLIP", {"big": dict(QFEED_ROUND=999), "small": dict(QFEED_ROUND=250)}, "economic",
           "early queen feeding off on big maps / on (r250) on small maps"),
    # structure: kings, feeding, endgame
    Switch("LOCAL_KINGS", both(LK_ROUND=220, LK_ANY_KING=True), "structure", "local kings v2 from r220 (1,500+ tiles)"),
    Switch("EARLY_KING", both(EARLY_KING=True), "structure", "longest dragon stops splitting from r150 (900+ tiles)"),
    Switch("LOCAL_RECYCLE", both(LOCAL_RECYCLE=True), "structure", "small dragons die in front of long allies after the switch"),
    Switch("ANCHOR_OFF", both(ANCHOR_KING=False), "structure", "no anchored king (1,500+ tiles)"),
    Switch("GUARDS", both(GUARD_PCT=20), "structure", "20% of small dragons near the anchored king guard it"),
    Switch("FEED_RING", both(FEED_RING=2), "structure", "one feeder at a time near the king"),
    Switch("MERGE_PULL", both(MERGE_APPROACH_MULT=3.0), "structure", "second kings travel to the first 3x harder"),
    Switch("QUEEN_BUD", both(QUEEN_BUD_LEN=8), "structure", "queen of 8+ buds off 4-long children until r300"),
    Switch("QUEEN_SPLIT_SMALL", {"small": dict(QUEEN_SPLIT_UNTIL_SMALL=150)}, "structure", "small maps: queen splits until r150"),
    Switch("QUEEN_FOOD_SMALL_OFF", {"small": dict(QUEEN_FOOD_SMALL_OFF=True)}, "structure", "small maps: no pearls left for the queen"),
    # adaptive
    Switch("ADAPT_NUMBERS", both(ADAPT_NUMBERS=True), "adaptive", "trade/threat by heads-in-view ratio"),
    Switch("RACE_AWARE", both(RACE_AWARE=True), "adaptive", "from r340 the king plays safe when ahead, bold when behind"),
    Switch("NC_QFEED", both(NC_QFEED_ROUND=100), "adaptive", "feed the queen from r100 while no enemy contact"),
]

# Groups of switches that must not be on together (they set the same setting).
EXCLUSIVE = [("TRADE_DOWN_2", "TRADE_DOWN_OFF")]

NUM_BY_NAME = {n.name: n for n in NUMS}
SWITCH_BY_NAME = {s.name: s for s in SWITCHES}
MODULES = sorted({n.module for n in NUMS})
STRATEGY_GROUPS = ["aggressive", "defensive", "economic", "adaptive", "structure"]


def derive(values: dict) -> dict:
    """Keep the round settings consistent (as tools/spsa.py did). values: {SETTING: v} for one scope."""
    out = dict(values)
    if "FEED_ROUND" in out:
        out["KING_PULL_ROUND"] = out["FEED_ROUND"]
    if "CONSOLIDATE_ROUND" in out or "FEED_ROUND" in out:
        feed = out.get("FEED_ROUND")
        cons = out.get("CONSOLIDATE_ROUND")
        if feed is not None and cons is not None and cons < feed:
            out["CONSOLIDATE_ROUND"] = cons = feed
        if cons is not None:
            out["FINAL_MERGE_ROUND"] = cons + 10
    return out
