"""Simulated games for testing the pipeline (python -m tools.tune.pipeline --dry --out results/tune_dry).

Hidden truth: a few switches and settings matter; everything else does nothing. Map and side matter a lot.
"""

from __future__ import annotations

import math
import random

from . import runner as runmod

TRUE_SWITCH = {("big", "KAMIKAZE"): -0.4, ("small", "KAMIKAZE"): 0.3, ("big", "QUEEN_STALL"): 0.25,
               ("small", "SPREAD_WEIGHT"): -0.6, ("big", "LK_ROUND"): 0.2}
TRUE_NUM = {("big", "THREAT_PER_LEN"): (45.0, 30.0), ("small", "EAT_NOW"): (500.0, 150.0), ("big", "FEED_ROUND"): (450, 40)}


class FakeRunner:
    def __init__(self, db, base):
        self.db = db
        self.base = base

    def run(self, jobs, configs, progress=""):
        out = []
        for j in jobs:
            key = (j["config"], j["opponent"], j["map"], j["seed"], j["side"])
            c = self.db.games("config=? AND opponent=? AND map=? AND seed=? AND side=?", key)
            if c:
                out.append(c[0])
                continue
            size = runmod.MAPS[j["map"]]["size"]
            eff = self.base.effective(configs[j["config"]], size)
            base = self.base.values(size)
            eta = (0.8 if j["side"] == "A" else -0.8) + (hash(j["map"]) % 7 - 3) * 0.2 + (0.1 if j["opponent"] == "v19" else -0.1)
            for (sc, name), b in TRUE_SWITCH.items():
                if sc == size and eff[name] != base[name]:
                    eta += b
            for (sc, name), (opt, width) in TRUE_NUM.items():
                if sc == size:
                    eta += 0.3 * (1 - ((eff[name] - opt) / width) ** 2) - 0.3 * (1 - ((base[name] - opt) / width) ** 2)
            rng = random.Random(hash(key))
            p = 1 / (1 + math.exp(-eta))
            score = 1.0 if rng.random() < p else 0.0
            row = dict(config=j["config"], opponent=j["opponent"], map=j["map"], size=size, seed=j["seed"], side=j["side"],
                       ok=1, score=score, winner=j["side"] if score else "X", end_reason="sim", rounds=500, our_queen=5,
                       opp_queen=4, our_longest=5, opp_longest=4, our_total=50, opp_total=40, our_dragons=3, opp_dragons=3,
                       our_maxpts=6e6, errors=None, seconds=0.0, experiment=j.get("experiment"), stage=j.get("stage"))
            self.db.add_game(row)
            out.append(row)
        return out
