"""Our ranked ladder history from the contest API, and replays of recent series.

Run with the toolkit's Python (it holds the API key via `unswbc auth`):
    "$(uv tool dir)/unswbc/Scripts/python.exe" tools/ladder_history.py [--replays 40]

Writes results/ladder_history.json (one row per game) and, with --replays N, downloads the
replays of the last N finished ranked series to replays/ladder/<battle>-<game>.replay.
Read-only: it never submits or challenges.
"""

from __future__ import annotations

import argparse
import gzip
import json
import pathlib
import time
import urllib.error
import urllib.request

from unswbc import api, auth

ROOT = pathlib.Path(__file__).resolve().parent.parent
US = "AllRed"
PAUSE = 0.6  # 120 requests a minute per key


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def replay_link(game_id: int, key: str) -> str | None:
    """The signed download link (the API answers with a redirect we must not follow with our key)."""
    req = urllib.request.Request(api.url(f"battles/{game_id}/replay"))
    req.add_header("Authorization", f"Bearer {key}")
    req.add_header("Origin", api.server())
    opener = urllib.request.build_opener(NoRedirect)
    try:
        opener.open(req, timeout=30)
    except urllib.error.HTTPError as e:
        if e.code in (301, 302, 303, 307, 308):
            return e.headers.get("Location")
        return None
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=200)
    ap.add_argument("--replays", type=int, default=0, help="download replays of the last N finished ranked series")
    args = ap.parse_args()
    key = auth.key()
    if not key:
        raise SystemExit("no API key: run `unswbc auth`")
    battles = api.request(f"battles?limit={args.limit}", key)
    done = [b for b in battles if b.get("ranked") and b.get("outcome") not in ("live", "pending", None)]
    print(f"{len(battles)} battles, {len(done)} finished ranked series")
    rows = []
    out = ROOT / "results" / "ladder_history.json"
    old = {}
    if out.exists():
        for r in json.loads(out.read_text()):
            old.setdefault(r["battle"], []).append(r)
    rdir = ROOT / "replays" / "ladder"
    rdir.mkdir(parents=True, exist_ok=True)
    want_replays = {b["id"] for b in done[: args.replays]}
    for i, b in enumerate(done):
        if b["id"] in old and b["id"] not in want_replays:
            rows += old[b["id"]]
            continue
        time.sleep(PAUSE)
        d = api.request(f"battles/{b['id']}", key)
        m = d["match"]
        we_a = d.get("teamAName") == US
        our_elo = d.get("teamAElo") if we_a else d.get("teamBElo")
        for g in d.get("games", []):
            won = None if g.get("winner") is None else (g["winner"] == ("a" if we_a else "b"))
            rows.append({
                "battle": b["id"], "game": g["id"], "at": b.get("at"), "opponent": b.get("opponent"),
                "opp_elo": b.get("opponentElo"), "our_elo": our_elo, "elo_change": b.get("eloChange"),
                "series": b.get("outcome"), "map": g.get("mapName"), "won": won, "we_are": "A" if we_a else "B",
            })
            if b["id"] in want_replays and g.get("hasReplay"):
                path = rdir / f"{b['id']}-{g['id']}.replay"
                if not path.exists():
                    time.sleep(PAUSE)
                    link = replay_link(g["id"], key)
                    if link:
                        try:
                            data = urllib.request.urlopen(link, timeout=60).read()  # no key sent to the signed link
                            if data[:2] == bytes([0x1F, 0x8B]):  # downloads come gzip-compressed
                                data = gzip.decompress(data)
                            path.write_bytes(data)
                        except OSError as e:
                            print(f"   replay {g['id']}: {e}")
        if (i + 1) % 10 == 0:
            print(f"   {i + 1}/{len(done)} series", flush=True)
    out.write_text(json.dumps(rows, indent=1))
    print(f"saved {len(rows)} games to {out}")


if __name__ == "__main__":
    main()
