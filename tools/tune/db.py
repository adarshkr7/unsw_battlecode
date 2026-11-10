"""Experiment database: every configuration, every game, every decision. Nothing is ever overwritten.

SQLite file results/tune/tune.db (WAL), plus an append-only JSON-lines copy of every write
(results/tune/journal.jsonl) so the data survives even a damaged database file.

Tables
  configs   one row per configuration (id, overrides, parent, optimizer, experiment, created)
  games     one row per played game (cache key: config, opponent, map, seed, side)
  events    decisions and stage changes (promotions, rejections, notes), with a JSON payload
  pointers  history of named pointers (current_baseline, best_overall, best_vs_v19, ...); the
            latest row per name is the current value
"""

from __future__ import annotations

import json
import pathlib
import sqlite3
import subprocess
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCHEMA = """
CREATE TABLE IF NOT EXISTS configs (
  id TEXT PRIMARY KEY, created REAL, overrides TEXT, parent TEXT, optimizer TEXT, experiment TEXT,
  module TEXT, note TEXT, base TEXT, git TEXT, design TEXT);
CREATE TABLE IF NOT EXISTS games (
  rowid INTEGER PRIMARY KEY AUTOINCREMENT, ts REAL, config TEXT, opponent TEXT, map TEXT, size TEXT,
  seed INTEGER, side TEXT, ok INTEGER, score REAL, winner TEXT, end_reason TEXT, rounds INTEGER,
  our_queen INTEGER, opp_queen INTEGER, our_longest INTEGER, opp_longest INTEGER, our_total INTEGER,
  opp_total INTEGER, our_dragons INTEGER, opp_dragons INTEGER, our_maxpts REAL, errors TEXT, seconds REAL,
  experiment TEXT, stage TEXT, git TEXT);
CREATE INDEX IF NOT EXISTS games_key ON games(config, opponent, map, seed, side);
CREATE INDEX IF NOT EXISTS games_exp ON games(experiment);
CREATE TABLE IF NOT EXISTS events (
  rowid INTEGER PRIMARY KEY AUTOINCREMENT, ts REAL, kind TEXT, experiment TEXT, config TEXT, payload TEXT);
CREATE TABLE IF NOT EXISTS pointers (
  rowid INTEGER PRIMARY KEY AUTOINCREMENT, ts REAL, name TEXT, config TEXT, reason TEXT);
"""


def git_commit() -> str:
    try:
        out = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True)
        dirty = subprocess.run(["git", "status", "--porcelain", "tools/tune"], cwd=ROOT, capture_output=True, text=True)
        return out.stdout.strip() + ("+tune-dirty" if dirty.stdout.strip() else "")
    except OSError:
        return "?"


class DB:
    def __init__(self, path: str | pathlib.Path = "results/tune/tune.db"):
        self.path = (ROOT / path).resolve()
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.journal = self.path.parent / "journal.jsonl"
        self.lock = threading.Lock()
        self.con = sqlite3.connect(self.path, check_same_thread=False, timeout=60)
        self.con.execute("PRAGMA journal_mode=WAL")
        self.con.executescript(SCHEMA)
        self.con.commit()
        self.git = git_commit()

    def _journal(self, table: str, row: dict) -> None:
        with self.journal.open("a", encoding="utf-8") as f:
            f.write(json.dumps({"table": table, **row}) + "\n")

    # -- configs -----------------------------------------------------------------------------
    def add_config(self, cid: str, overrides: dict, parent=None, optimizer=None, experiment=None, module=None,
                   note=None, base=None, design=None) -> None:
        with self.lock:
            if self.con.execute("SELECT 1 FROM configs WHERE id=?", (cid,)).fetchone():
                return
            row = dict(id=cid, created=time.time(), overrides=json.dumps(overrides, sort_keys=True), parent=parent,
                       optimizer=optimizer, experiment=experiment, module=module, note=note, base=base, git=self.git,
                       design=json.dumps(design) if design is not None else None)
            self.con.execute(f"INSERT INTO configs ({','.join(row)}) VALUES ({','.join('?' * len(row))})", list(row.values()))
            self.con.commit()
            self._journal("configs", row)

    def config(self, cid: str) -> dict | None:
        r = self.con.execute("SELECT id, overrides, parent, optimizer, experiment, module, note, design FROM configs WHERE id=?",
                             (cid,)).fetchone()
        if not r:
            return None
        return dict(id=r[0], overrides=json.loads(r[1]), parent=r[2], optimizer=r[3], experiment=r[4], module=r[5],
                    note=r[6], design=json.loads(r[7]) if r[7] else None)

    # -- games -------------------------------------------------------------------------------
    def cached(self, config: str, opponent: str, map_: str, seed: int, side: str) -> dict | None:
        r = self.con.execute("SELECT score FROM games WHERE config=? AND opponent=? AND map=? AND seed=? AND side=? AND ok=1 "
                             "LIMIT 1", (config, opponent, map_, seed, side)).fetchone()
        return {"score": r[0]} if r else None

    def add_game(self, row: dict) -> None:
        row = dict(row, ts=time.time(), git=self.git)
        with self.lock:
            self.con.execute(f"INSERT INTO games ({','.join(row)}) VALUES ({','.join('?' * len(row))})", list(row.values()))
            self.con.commit()
            self._journal("games", row)

    def games(self, where: str = "1", args=()) -> list[dict]:
        cur = self.con.execute(f"SELECT * FROM games WHERE ok=1 AND ({where})", args)
        cols = [c[0] for c in cur.description]
        return [dict(zip(cols, r)) for r in cur.fetchall()]

    # -- events and pointers -----------------------------------------------------------------
    def event(self, kind: str, experiment=None, config=None, **payload) -> None:
        row = dict(ts=time.time(), kind=kind, experiment=experiment, config=config, payload=json.dumps(payload, default=str))
        with self.lock:
            self.con.execute("INSERT INTO events (ts, kind, experiment, config, payload) VALUES (?,?,?,?,?)", list(row.values()))
            self.con.commit()
            self._journal("events", row)

    def events(self, kind: str | None = None, experiment: str | None = None) -> list[dict]:
        q, a = "SELECT ts, kind, experiment, config, payload FROM events WHERE 1", []
        if kind:
            q, a = q + " AND kind=?", a + [kind]
        if experiment:
            q, a = q + " AND experiment=?", a + [experiment]
        return [dict(ts=r[0], kind=r[1], experiment=r[2], config=r[3], **json.loads(r[4])) for r in self.con.execute(q, a)]

    def set_pointer(self, name: str, config: str, reason: str = "") -> None:
        if self.pointer(name) == config:
            return
        row = dict(ts=time.time(), name=name, config=config, reason=reason)
        with self.lock:
            self.con.execute("INSERT INTO pointers (ts, name, config, reason) VALUES (?,?,?,?)", list(row.values()))
            self.con.commit()
            self._journal("pointers", row)

    def pointer(self, name: str) -> str | None:
        r = self.con.execute("SELECT config FROM pointers WHERE name=? ORDER BY rowid DESC LIMIT 1", (name,)).fetchone()
        return r[0] if r else None

    def pointer_history(self, name: str) -> list[tuple]:
        return self.con.execute("SELECT ts, config, reason FROM pointers WHERE name=? ORDER BY rowid", (name,)).fetchall()
