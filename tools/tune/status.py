"""Where the tuning pipeline is: python -m tools.tune.status [--out results/tune]"""

from __future__ import annotations

import argparse
import json
import pathlib
import sqlite3
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="results/tune")
    ap.add_argument("--log", type=int, default=12)
    a = ap.parse_args()
    out = ROOT / a.out
    con = sqlite3.connect(out / "tune.db")
    n, ok = con.execute("SELECT COUNT(*), SUM(ok) FROM games").fetchone()
    last_hour = con.execute("SELECT COUNT(*) FROM games WHERE ok=1 AND ts > ?", (time.time() - 3600,)).fetchone()[0]
    print(f"games: {n} played, {ok} valid, {n - (ok or 0)} invalid; {last_hour} in the last hour")
    for exp, cnt, avg in con.execute("SELECT experiment, COUNT(*), AVG(score) FROM games WHERE ok=1 GROUP BY experiment "
                                     "ORDER BY MIN(ts)"):
        print(f"  {exp:<22} {cnt:>6} games, mean score {avg:.3f}")
    for name in ("current_baseline", "best_overall", "best_vs_v19", "best_vs_v20", "most_robust"):
        r = con.execute("SELECT config, reason FROM pointers WHERE name=? ORDER BY rowid DESC LIMIT 1", (name,)).fetchone()
        print(f"{name:<17} {r[0] if r else '-'}  {r[1] if r else ''}")
    for e in sorted((out / "experiments").glob("*.json"), key=lambda p: p.stat().st_mtime):
        d = json.loads(e.read_text(encoding="utf-8"))
        b = d["batches"]
        print(f"  {e.stem:<22} {'done' if d['done'] else 'running'}  batches {sum(x['complete'] for x in b)}/{len(b)}")
    bad = con.execute("SELECT errors FROM games WHERE ok=0 ORDER BY rowid DESC LIMIT 3").fetchall()
    for (err,) in bad:
        print("  invalid:", (err or "")[:160])
    log = (out / "driver.log").read_text(encoding="utf-8").splitlines()
    print("\n".join(l[:400] for l in log[-a.log:]))


if __name__ == "__main__":
    main()
