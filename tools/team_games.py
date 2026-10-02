"""List another team's games from the public Games page, and optionally download their replays.

Run with the toolkit's Python (replay downloads use unswbc's own auth and API helpers):
    <unswbc python> tools/team_games.py "cheji bt" --since 2026-09-26T16:49Z
    <unswbc python> tools/team_games.py 70 --since 2026-09-26T16:49Z --ranked --replays

The game list comes from the site's /games page data (public, no key). It is written to
results/teams/<team>/games.json. With --replays, each finished game's replay goes to
results/teams/<team>/<id>.replay, skipping ones already there. The API allows 120 requests a
minute per key, so downloads are paced to stay under it.
"""

import argparse
import datetime
import json
import pathlib
import re
import sys
import time
import urllib.request

from unswbc import __version__, api, auth

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from fetch_replays import download  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[1]
SITE = "https://game.battlecode.au"


def unflatten(data):
    """Decode SvelteKit's devalue encoding: a flat array whose containers point at indices."""
    special = {-1: None, -2: None, -3: float("nan"), -4: float("inf"), -5: float("-inf"), -6: -0.0}
    memo = {}

    def at(i):
        if i in special:
            return special[i]
        if i in memo:
            return memo[i]
        v = data[i]
        if isinstance(v, list):
            if v and v[0] == "Date":
                out = v[1]
            else:
                out = []
                memo[i] = out
                out.extend(at(x) for x in v)
        elif isinstance(v, dict):
            out = {}
            memo[i] = out
            out.update({k: at(x) for k, x in v.items()})
        else:
            out = v
        memo[i] = out
        return out

    return at(0)


def games_page(team_id, page):
    ask = urllib.request.Request(f"{SITE}/games/__data.json?teams={team_id}&page={page}")
    ask.add_header("User-Agent", f"unswbc/{__version__}")
    ask.add_header("Cookie", "rows-games=100")
    with urllib.request.urlopen(ask, timeout=60) as reply:
        body = json.loads(reply.read())
    for node in body["nodes"]:
        if node and node.get("type") == "data" and isinstance(node["data"][0], dict) \
                and "matches" in node["data"][0]:
            return unflatten(node["data"])
    raise RuntimeError("no games table in the page data")


def find_team(key, name_or_id):
    teams = api.request("teams", key=key)
    for t in teams:
        if str(t["id"]) == name_or_id or t["name"].lower() == name_or_id.lower():
            return t
    if name_or_id.isdigit():  # the API's list leaves some teams out; their games still name them
        for g in games_page(int(name_or_id), 1)["matches"]:
            for t in (g["a"], g["b"]):
                if t["id"] == int(name_or_id):
                    return t
    sys.exit(f"no team named {name_or_id!r} (give a team missing from the API list by id)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("team", help="team name or id")
    ap.add_argument("--since", required=True, help="UTC time, e.g. 2026-09-26T16:49Z")
    ap.add_argument("--ranked", action="store_true", help="keep ranked games only")
    ap.add_argument("--replays", action="store_true", help="also download each finished game's replay")
    args = ap.parse_args()

    key = auth.key()
    if not key:
        sys.exit("no API key: run `unswbc auth set <key>` first")
    team = find_team(key, args.team)
    since = datetime.datetime.fromisoformat(args.since.replace("Z", "+00:00"))
    out = ROOT / "results" / "teams" / re.sub(r"[^\w.-]+", "_", team["name"])
    out.mkdir(parents=True, exist_ok=True)

    games, page = [], 1
    while True:
        d = games_page(team["id"], page)
        rows = d["matches"]
        for g in rows:
            if datetime.datetime.fromisoformat(g["at"].replace("Z", "+00:00")) >= since \
                    and (g["ranked"] or not args.ranked):
                games.append(g)
        last = rows[-1]["at"] if rows else None
        print(f"page {page}: {len(rows)} games, oldest {last}", file=sys.stderr)
        if not rows or datetime.datetime.fromisoformat(last.replace("Z", "+00:00")) < since \
                or page * d["perPage"] >= d["total"]:
            break
        page += 1

    for g in games:  # tag the side our target team played
        g["side"] = "a" if g["a"]["id"] == team["id"] else "b"
        g["opp"] = g["b" if g["side"] == "a" else "a"]["name"]
        g["result"] = ("win" if g["winner"] == g["side"] else "draw" if g["winner"] == "draw"
                       else "loss" if g["winner"] else g["status"])
    (out / "games.json").write_text(json.dumps(games, indent=1, ensure_ascii=False), encoding="utf-8")
    print(f"{len(games)} games since {args.since} -> {out / 'games.json'}")

    if args.replays:
        todo = [g["id"] for g in games if g["hasReplay"] and not (out / f"{g['id']}.replay").exists()]
        print(f"downloading {len(todo)} replays")
        for n, gid in enumerate(todo, 1):
            started = time.monotonic()
            print(f"[{n}/{len(todo)}] {download(key, gid, out)}", flush=True)
            time.sleep(max(0.0, 0.55 - (time.monotonic() - started)))  # < 120 requests a minute


if __name__ == "__main__":
    main()
