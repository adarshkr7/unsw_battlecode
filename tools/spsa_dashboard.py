"""A local page that shows the SPSA tuning run (tools/spsa.py) as it goes.

    python tools/spsa_dashboard.py [--out results/spsa] [--port 8765] [--iters 200]

Then open http://localhost:8765. The page reads the tuner's files on every refresh (every 30 s):
state.json, log.csv, avg.json, last.json, run.json and checks/ (tests against v19) in --out, and the
tuner's working folders in results/tmp_bots (a spsa_p<k> folder means step k is being played). While a
test runs, its results so far come from the replays of its finished games (tools/replay_results.cjs,
each parsed once). It only reads; it never changes anything, and it listens on this machine only.
"""

from __future__ import annotations

import argparse
import csv
import http.server
import json
import pathlib
import re
import subprocess
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import spsa_tune as spsa  # noqa: E402  (the parameter list and ranges)
import spsa_validate as spsa_check  # noqa: E402  (the same tally as the test's own summary)

PAGE = ROOT / "tools" / "spsa_dashboard.html"
REPLAY_TAG = re.compile(r"^(?P<map>.+)-s(?P<seed>\d+)-(?P<who>bot1A|bot2A)\.replay$")
REPLAY_CACHE: dict[str, tuple] = {}  # path -> ((size, mtime), result): each replay is parsed once
CACHE_LOCK = threading.Lock()


def read_json(path: pathlib.Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def replay_results(files: list[pathlib.Path]) -> dict[str, dict]:
    """Result of each finished game from its replay (tools/replay_results.cjs). The engine writes a
    replay in one go when the game ends; one still being written fails to parse and is tried again."""
    out, todo = {}, []
    with CACHE_LOCK:
        for f in files:
            try:
                st = f.stat()
            except OSError:
                continue
            key = (st.st_size, st.st_mtime)
            hit = REPLAY_CACHE.get(str(f))
            if hit and hit[0] == key:
                out[f.name] = hit[1]
            else:
                todo.append((f, key))
        for i in range(0, len(todo), 40):
            chunk = todo[i:i + 40]
            try:
                done = subprocess.run(["node", str(ROOT / "tools" / "replay_results.cjs"), *[str(f) for f, _ in chunk]],
                                      capture_output=True, text=True, cwd=ROOT, timeout=120)
                items = json.loads(done.stdout) if done.returncode == 0 else []
            except (OSError, ValueError, subprocess.TimeoutExpired):
                items = []
            by_name = {it.get("file"): it for it in items}
            for f, key in chunk:
                it = by_name.get(f.name)
                if it and "error" not in it:
                    REPLAY_CACHE[str(f)] = (key, it)
                    out[f.name] = it
    return out


def live_test(out: pathlib.Path, testing: dict) -> dict:
    """Results so far of the running test against v19 (tools/spsa_check.py): a finished set from its
    game list, the set being played from the replays of its finished games."""
    checks = out / "checks"
    k = testing.get("step")
    phase = testing.get("phase")
    plan = testing.get("plan")
    if not plan:  # a test started before the plan was recorded: the standard two sets
        seeds = max(1, int(testing.get("games", 0)) // max(1, 2 * int(testing.get("maps", 1))))
        held = len(list((ROOT / "maps_heldout").glob("*.map")))
        plan = [{"name": "tuning maps", "tag": "tuning", "maps": testing.get("maps") if phase == "tuning maps" else None,
                 "games": testing.get("games") if phase == "tuning maps" else None},
                {"name": "held-out maps", "tag": "heldout", "maps": held, "games": held * seeds * 2}]
    sets, all_games = [], []
    for p in plan:
        running = p["name"] == phase
        games, total = [], p.get("games")
        games_file = checks / f"step{k}_{p['tag']}_games.json"
        if running:
            tmp = checks / f"step{k}_{p['tag']}" / "_tmp"
            found = replay_results(sorted(tmp.glob("*.replay"))) if tmp.is_dir() else {}
            for name, it in found.items():
                m = REPLAY_TAG.match(name)
                if not m:
                    continue
                games.append({"ok": True, "winner": it.get("winner") or "draw",
                              "bot1_team": "A" if m["who"] == "bot1A" else "B", "name": m["map"],
                              "final": {"a_length": it["A"]["total"], "b_length": it["B"]["total"]},
                              "longest": {"A": it["A"]["longest"], "B": it["B"]["longest"]}})
            total = testing.get("games") or total
        elif games_file.is_file():
            games = read_json(games_file) or []
            total = len(games)
        t = spsa_check.tally(games)
        t.update(name=p["name"], maps=p.get("maps") or len({g["name"] for g in games}) or None,
                 total=total, running=running, done=len(games))
        sets.append(t)
        all_games += games
    t = spsa_check.tally(all_games)
    totals = [s["total"] for s in sets]
    t.update(name="all maps", done=len(all_games), total=sum(totals) if all(x is not None for x in totals) else None)
    return {"step": k, "phase": phase, "sets": sets, "all": t}


def snapshot(out: pathlib.Path, iters: int, base: pathlib.Path) -> dict:
    state = read_json(out / "state.json") or {}
    avg = read_json(out / "avg.json") or {}
    # The starting values always come from the base bot itself.
    try:
        state["start"] = spsa.read_values((base / "params.hpp").read_text(encoding="utf-8"))
    except (OSError, SystemExit):
        state["start"] = {}
    if not state.get("theta"):  # before the first step finishes
        state["theta"] = dict(state["start"])
    log = []
    try:
        with (out / "log.csv").open(encoding="utf-8", newline="") as f:
            for row in csv.DictReader(f):
                log.append({k: row[k] for k in ("iter", "time", "minutes", "games", "w", "d", "l", "errors", "r", "lr", "ck")})
    except OSError:
        pass
    done = int(state.get("k", 0))
    start = state.get("start") or {}
    theta = state.get("theta") or {}
    params = []
    for name, typ, lo, hi, step in spsa.PARAMS:
        params.append({"name": name, "int": typ is int, "lo": lo, "hi": hi, "step": step,
                       "start": start.get(name), "current": theta.get(name), "avg": avg.get(name)})
    running = None
    folder = ROOT / "results" / "tmp_bots" / f"spsa_p{done}"
    if folder.is_dir():
        running = {"step": done, "since": folder.stat().st_mtime}
    log_age = None
    if (out / "log.csv").is_file():
        log_age = time.time() - (out / "log.csv").stat().st_mtime
    last = []
    for g in read_json(out / "last.json") or []:
        if isinstance(g, dict):
            last.append({"map": g.get("name"), "plusTeam": g.get("bot1_team"), "winner": g.get("winner"),
                         "rounds": g.get("rounds"), "reason": g.get("reason"), "ok": bool(g.get("ok"))})
    err_tail = []
    try:
        err_tail = [l for l in (out / "console.err").read_text(encoding="utf-8", errors="replace").splitlines() if l.strip()][-5:]
    except OSError:
        pass
    # Tests of the averaged values against v19 (tools/spsa_check.py, run by the tuner at --check-at steps).
    checks = []
    for f in (out / "checks").glob("step*.json"):
        if re.fullmatch(r"step\d+\.json", f.name):
            c = read_json(f)
            if c:
                checks.append({key: c.get(key) for key in ("step", "finished", "minutes", "reference", "seedBase", "seeds", "sets", "all")})
    checks.sort(key=lambda c: c["step"])
    testing = read_json(out / "checks" / "status.json")
    if testing and time.time() - testing.get("phaseStarted", 0) > 3 * 3600:
        testing = None  # left behind by a check that was killed
    test_live = None
    if testing:
        try:
            test_live = live_test(out, testing)
        except Exception as e:  # the page must keep working whatever the files look like
            test_live = {"error": repr(e)[:200]}
    run = read_json(out / "run.json") or {}
    return {"now": time.time(), "done": done, "iters": iters, "params": params, "log": log,
            "history": state.get("history") or [], "running": running, "logAge": log_age,
            "lastGames": last, "errTail": err_tail, "started": (out / "console.log").stat().st_mtime
            if (out / "console.log").is_file() else None,
            "checks": checks, "testing": testing, "testLive": test_live, "checkAt": run.get("checkAt") or []}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="results/spsa")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--iters", type=int, default=200)
    ap.add_argument("--base", default="results/tmp_bots/v19_tune_base", help="the bot being tuned (its starting values)")
    args = ap.parse_args()
    out = (ROOT / args.out).resolve()
    base = (ROOT / args.base).resolve()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path.split("?")[0] == "/data":
                body = json.dumps(snapshot(out, args.iters, base)).encode()
                ctype = "application/json"
            elif self.path.split("?")[0] in ("/", "/index.html"):
                body = PAGE.read_bytes()
                ctype = "text/html; charset=utf-8"
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *a):  # keep the console quiet
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"tuning page on http://localhost:{args.port} (reading {out})", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
