"""Builds configurations and plays games in the judge's sandbox, with a cache: a (config, opponent, map,
seed, side) that already has a valid result is never played again.

Games run through tools/match.py (the toolkit's own engine, exactly as tools/tourney.py does), one
process per game, `workers` at a time. A game that reports a wall-clock timeout ("ran out of time")
or an engine failure is invalid (CPU starvation, not the bot): it is logged with ok=0 and replayed.
"""

from __future__ import annotations

import concurrent.futures as futures
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import threading
import time

from . import config as cfgmod

ROOT = pathlib.Path(__file__).resolve().parents[2]
ANSI = re.compile(r"\x1b\[[0-9;]*m")
WIN = re.compile(r"team (A|B) wins after (\d+) rounds \((.*?)\)")
DRAW = re.compile(r"draw after (\d+) rounds \((.*?)\)")
POINTS = re.compile(r"team (A|B) points per turn: p50 (\S+)\s+p99 (\S+)\s+mean (\S+)\s+max (\S+)")
BOT_LINE = re.compile(r"round (\S+): bot (\d+) \(team (A|B)\) (.*)")

# Frozen opponents (copied once into results/tune/opponents/<name>, never edited).
OPPONENTS = {
    "v19": "results/tmp_bots/v19_live",   # ladder v17 (3 Oct): v15 + TRADE_DOWN_MAX 1 + FEED_MARGIN 4
    "v20": "bots/v20",                    # ladder v18 (4 Oct): v19 code + SPSA step-100 settings
    "v15": "results/tmp_bots/v15_live",   # secondary: ladder v15 (bots/v11 at 5015023)
    "v18": "results/tmp_bots/v18_live",   # secondary: ladder v16 (v15 + pearl auction)
}
PRIMARY = ("v19", "v20")
SECONDARY = ("v15", "v18")

MAPSETS = {
    "tune": "maps_official",       # the 17 ladder maps (exact, from the API)
    "heldout": "maps_heldout",     # 5 official maps never used for tuning
}
EXTRA_MAPS = ["maps_ladder3/schooltime_open", "maps_ladder3/prisoners_dilemma6"]  # second ladder layouts
SMALL_MAP_AREA = 1000


def map_info() -> dict:
    """map key ("maps_official/devil") -> {"path", "area", "size"}."""
    out = {}
    keys = []
    for d in MAPSETS.values():
        keys += [f"{d}/{p.stem}" for p in sorted((ROOT / d).glob("*.map"))]
    keys += EXTRA_MAPS
    for k in keys:
        p = ROOT / f"{k}.map"
        w, h = map(int, p.read_text(encoding="utf-8").split("\n", 1)[0].split()[1:3])
        out[k] = {"path": str(p), "area": w * h, "size": "big" if w * h >= SMALL_MAP_AREA else "small"}
    return out


MAPS = map_info()


def maps_of(mapset: str, size: str | None = None) -> list[str]:
    d = MAPSETS[mapset]
    return [k for k, v in MAPS.items() if k.startswith(d + "/") and (size is None or v["size"] == size)]


def tool_python() -> str:
    tool_dir = subprocess.run(["uv", "tool", "dir"], capture_output=True, text=True, check=True).stdout.strip()
    for rel in ("unswbc/Scripts/python.exe", "unswbc/bin/python"):
        c = pathlib.Path(tool_dir) / rel
        if c.is_file():
            return str(c)
    sys.exit("cannot find the unswbc tool's python")


def points(text: str) -> float:
    return float(text[:-1]) * 1e6 if text.endswith("M") else float(text.replace(",", ""))


class Runner:
    def __init__(self, db, base: cfgmod.Base, workers: int = 16, builders: int = 3, log=print):
        self.db = db
        self.base = base
        self.workers = workers
        self.log = log
        self.python = tool_python()
        self.wasm: dict[str, str] = {}
        self.build_lock = threading.Semaphore(builders)
        self.bots_dir = ROOT / "results" / "tune" / "bots"
        self.opp_dir = ROOT / "results" / "tune" / "opponents"
        self.bots_dir.mkdir(parents=True, exist_ok=True)
        self.stop = False

    # -- building --------------------------------------------------------------------------------
    def _build(self, folder: pathlib.Path) -> str:
        code = ("import pathlib, sys\nfrom unswbc import clangtool\nfrom unswbc.sandbox import _cache_dir\n"
                "d = pathlib.Path(sys.argv[1])\n"
                "src = ('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx')\n"
                "newest = max(p.stat().st_mtime for p in d.rglob('*') if p.is_file() and p.suffix in src)\n"
                "for w in (_cache_dir() / 'wasmbots').glob(d.resolve().name + '-*.wasm'):\n"
                "    if w.stat().st_mtime < newest:\n"
                "        w.unlink()\n"
                "print(clangtool.build(d))")
        with self.build_lock:
            for attempt in range(3):
                done = subprocess.run([self.python, "-c", code, str(folder)], capture_output=True, text=True, cwd=ROOT,
                                      env=dict(os.environ, UNSWBC_NO_UPDATE="1", NO_COLOR="1"))
                if done.returncode == 0:
                    return done.stdout.strip().splitlines()[-1]
                time.sleep(5)
        raise RuntimeError(f"build of {folder} failed:\n{done.stdout[-1500:]}\n{done.stderr[-1500:]}")

    def opponent_wasm(self, name: str) -> str:
        key = "opp:" + name
        if key not in self.wasm:
            dest = self.opp_dir / name
            if not dest.is_dir():
                dest.mkdir(parents=True)
                src = ROOT / OPPONENTS[name]
                for f in src.iterdir():
                    if f.is_file() and f.suffix in (".cpp", ".hpp", ".toml"):
                        shutil.copy(f, dest / f.name)
            # A frozen opponent folder gets a unique name so its cached wasm never collides.
            self.wasm[key] = self._build(dest)
        return self.wasm[key]

    def config_wasm(self, cid: str, overrides: dict) -> str:
        if cid not in self.wasm:
            dest = self.bots_dir / f"c_{cid}"
            self.base.make_bot(overrides, dest)
            self.wasm[cid] = self._build(dest)
        return self.wasm[cid]

    # -- playing ---------------------------------------------------------------------------------
    def _play(self, job: dict, a: str, b: str) -> dict:
        info = MAPS[job["map"]]
        cmd = [self.python, str(ROOT / "tools" / "run_match.py"), info["path"], a, b, str(job["seed"])]
        env = dict(os.environ, UNSWBC_NO_UPDATE="1", NO_COLOR="1")
        started = time.monotonic()
        done = subprocess.run(cmd, capture_output=True, text=True, env=env, cwd=ROOT, errors="replace")
        out = ANSI.sub("", done.stdout + done.stderr)
        res = {"winner": None, "rounds": 0, "reason": "", "errors": [], "final": None, "points": {},
               "seconds": time.monotonic() - started}
        for line in out.splitlines():
            if line.startswith("RESULT "):
                res["final"] = json.loads(line[7:])
            elif m := WIN.search(line):
                res.update(winner=m[1], rounds=int(m[2]), reason=m[3])
            elif m := DRAW.search(line):
                res.update(winner="draw", rounds=int(m[1]), reason=m[2])
            elif m := POINTS.search(line):
                res["points"][m[1]] = points(m[5])
            elif (m := BOT_LINE.search(line)) and "died:" not in m[4] and "stdout" not in m[4] and "stderr" not in m[4]:
                res["errors"].append(f"{m[3]}: {m[4].strip()}"[:200])
        if res["winner"] is None:
            res["errors"].append("engine: " + out.strip()[-300:])
        return res

    def _job(self, job: dict) -> dict:
        cand = job["wasm"]
        opp = self.opponent_wasm(job["opponent"])
        ours = job["side"]
        theirs = "B" if ours == "A" else "A"
        a, b = (cand, opp) if ours == "A" else (opp, cand)
        for attempt in range(3):
            if self.stop:
                return {}
            r = self._play(job, a, b)
            timeout = any("ran out of time" in e or e.startswith("engine") for e in r["errors"])
            f = r["final"] or {}
            ok = r["winner"] is not None and not timeout
            row = dict(config=job["config"], opponent=job["opponent"], map=job["map"], size=MAPS[job["map"]]["size"],
                       seed=job["seed"], side=ours, ok=int(ok),
                       score=None if not ok else (0.5 if r["winner"] == "draw" else float(r["winner"] == ours)),
                       winner=r["winner"], end_reason=r["reason"], rounds=r["rounds"],
                       our_queen=f.get(f"{ours.lower()}_queen"), opp_queen=f.get(f"{theirs.lower()}_queen"),
                       our_longest=f.get(f"{ours.lower()}_longest"), opp_longest=f.get(f"{theirs.lower()}_longest"),
                       our_total=f.get(f"{ours.lower()}_length"), opp_total=f.get(f"{theirs.lower()}_length"),
                       our_dragons=f.get(f"{ours.lower()}_dragons"), opp_dragons=f.get(f"{theirs.lower()}_dragons"),
                       our_maxpts=r["points"].get(ours), errors=json.dumps(r["errors"][:5]) if r["errors"] else None,
                       seconds=round(r["seconds"], 1), experiment=job.get("experiment"), stage=job.get("stage"))
            self.db.add_game(row)
            if ok:
                return row
            self.log(f"  invalid game ({job['config']} vs {job['opponent']} {job['map']} s{job['seed']} {ours}): "
                     f"{r['errors'][:2]}; retry {attempt + 1}")
            time.sleep(10 * (attempt + 1))
        return {}

    def run(self, jobs: list[dict], configs: dict[str, dict], progress: str = "") -> list[dict]:
        """jobs: dicts with config, opponent, map, seed, side (+ experiment, stage).
        configs: config id -> overrides. Returns the valid result rows (cached or new)."""
        results, todo = [], []
        seen = set()
        for j in jobs:
            key = (j["config"], j["opponent"], j["map"], j["seed"], j["side"])
            if key in seen:
                continue
            seen.add(key)
            c = self.db.games("config=? AND opponent=? AND map=? AND seed=? AND side=?", key)
            if c:
                results.append(c[0])
            else:
                todo.append(j)
        if not todo:
            return results
        # Builds run a few at a time while the games of configurations already built are played.
        for o in sorted({j["opponent"] for j in todo}):
            self.opponent_wasm(o)
        need = []
        for j in todo:
            if j["config"] not in need:
                need.append(j["config"])
        builders = futures.ThreadPoolExecutor(len(need) if all(c in self.wasm for c in need) else 3)
        built = {c: builders.submit(self.config_wasm, c, configs[c]) for c in need}
        # Configurations in build order; within one, the longest games (big maps) first.
        order = {c: i for i, c in enumerate(need)}
        todo.sort(key=lambda j: (order[j["config"]], -MAPS[j["map"]]["area"]))

        def job(j):
            try:
                j["wasm"] = built[j["config"]].result()
            except Exception as e:  # a configuration that does not build loses nothing else
                self.log(f"  build failed for {j['config']}: {str(e)[:300]}")
                return {}
            return self._job(j)

        started = time.monotonic()
        n = 0
        with futures.ThreadPoolExecutor(self.workers) as pool:
            for row in pool.map(job, todo):
                n += 1
                if row:
                    results.append(row)
                if n % 100 == 0 or n == len(todo):
                    self.log(f"  {progress}{n}/{len(todo)} games ({(time.monotonic() - started) / 60:.1f} min)")
        builders.shutdown()
        return results
