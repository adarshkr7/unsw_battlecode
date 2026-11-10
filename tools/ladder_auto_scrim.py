"""Keep the team's hand-started allowance busy with ranked scrims against teams rated above us.

Run with the toolkit's Python (it uses unswbc's own auth and API helpers):
    <unswbc python> tools/auto_scrim.py                   (runs until Ctrl+C)
    <unswbc python> tools/auto_scrim.py --dry-run --once  (show the count and the next pick only)

A team may start 60 games an hour by hand (12 ranked 5-game scrims); teammates' challenges spend
the same allowance. Every cycle (2 minutes by default) this counts the games our team requested
in the last hour, then, while 5 or more are left, challenges one team to a ranked scrim.

- Who asked: our battle list does not say, so the public Games page is used. A requested game
  has `challenge` set and the requester is side A; autoscrim draws have `challenge` unset.
  Battles against DEV teams have their own allowance and are not counted.
- Who to ask: teams rated above us (not DEV) whose ranked switch is on and that have a bot,
  the one we have played least recently first, then the nearest in rating. `--max-gap` keeps to
  teams at most that many points above us.
- The hour is a rolling 60 minutes (checked 2 Oct: the server's 429 says "More free up in 5
  min" 55 minutes after our oldest request). If the server refuses a request for the limit, the
  cycle stops and the next one tries again; any other refusal skips that team for an hour.

Requests go to results/auto_scrim/log.csv, finished scrims to results/auto_scrim/results.csv,
and a copy of what it prints to results/auto_scrim/console.log.
"""

import argparse
import csv
import datetime
import json
import pathlib
import sys
import time

from unswbc import api, auth

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from team_games import games_page  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "results" / "auto_scrim"
LOG = OUT / "log.csv"
RESULTS = OUT / "results.csv"
CONSOLE = OUT / "console.log"
HOUR = datetime.timedelta(hours=1)
ALLOWANCE = 60     # games a team may start by hand in an hour
SERIES = 5         # games in a ranked scrim
LADDER_EVERY = datetime.timedelta(minutes=10)   # /ratings is an expensive read
PROFILE_EVERY = datetime.timedelta(hours=1)     # the ranked switch can flip only every 8 hours


def now():
    return datetime.datetime.now(datetime.timezone.utc)


def stamp(t):
    return t.strftime("%Y-%m-%dT%H:%M:%S.") + f"{t.microsecond // 1000:03d}Z"


def utc(text):
    return datetime.datetime.fromisoformat(text.replace("Z", "+00:00")) if text else now()


def read_csv(path):
    if not path.exists():
        return []
    with path.open(encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def say(text):
    """Print a line, and keep a copy in results/auto_scrim/console.log."""
    print(text, flush=True)
    with CONSOLE.open("a", encoding="utf-8") as f:
        f.write(text + "\n")


def append_csv(path, row):
    new = not path.exists()
    with path.open("a", encoding="utf-8", newline="") as f:
        out = csv.DictWriter(f, fieldnames=list(row))
        if new:
            out.writeheader()
        out.writerow(row)


class Api:
    """unswbc's request helper, paced to stay under 120 requests a minute."""

    def __init__(self, key):
        self.key, self.last = key, 0.0

    def __call__(self, path, **kw):
        time.sleep(max(0.0, self.last + 0.55 - time.monotonic()))
        try:
            return api.request(path, key=self.key, **kw)
        finally:
            self.last = time.monotonic()


class Scrimmer:
    def __init__(self, call, args):
        self.call, self.args = call, args
        self.details = {}               # battle id -> /battles/:id match fields we need
        self.profiles = {}              # team id -> (fetched at, public team profile)
        self.ladder, self.ladder_at = {}, None
        self.skip = {}                  # team id -> do not ask them before this time
        self.started = {int(r["battle_id"]): r for r in read_csv(LOG)}
        self.reported = {int(r["battle_id"]) for r in read_csv(RESULTS)}
        self.tally = [0, 0, 0, 0]       # scrims, games won, games lost, rating change

    # ---- reads ----
    def refresh_ladder(self):
        if self.ladder_at and now() - self.ladder_at < LADDER_EVERY:
            return
        self.ladder = {t["id"]: {k: t[k] for k in ("id", "name", "elo", "dev")}
                       for t in self.call("ratings")["ladder"]}
        self.ladder_at = now()

    def profile(self, team_id):
        got = self.profiles.get(team_id)
        if not got or now() - got[0] > PROFILE_EVERY:
            got = (now(), self.call(f"teams/{team_id}")["team"])
            self.profiles[team_id] = got
        return got[1]

    def is_dev(self, team_id):
        if team_id in self.ladder:
            return self.ladder[team_id]["dev"]
        return bool(self.profile(team_id).get("dev"))

    def detail(self, battle_id):
        if battle_id not in self.details:  # who asked and when never changes, so cache it
            d = self.call(f"battles/{battle_id}")
            m = d["match"]
            self.details[battle_id] = {"teamAId": m["teamAId"], "requestedAt": m["requestedAt"],
                                       "games": [g["id"] for g in d["games"]]}
        return self.details[battle_id]

    def challenge_flags(self, us, since):
        """Game id -> whether it was requested (rather than drawn), from the public Games page."""
        flags = {}
        for page in range(1, 6):
            rows = games_page(us, page)["matches"]
            flags.update({g["id"]: bool(g["challenge"]) for g in rows})
            if not rows or utc(rows[-1]["at"]) < since:
                break
        return flags

    # ---- the allowance ----
    def games_used(self, us, battles):
        """Games our team started by hand (not against DEV teams) in the last hour."""
        since = now() - HOUR
        flags = self.challenge_flags(us, since - datetime.timedelta(minutes=10))
        used, seen = 0, set()
        for b in battles:
            if utc(b["at"]) < since:  # a battle's time is never earlier than its request
                continue
            d = self.detail(b["id"])
            seen.update([b["id"], *d["games"]])
            if d["teamAId"] != us or utc(d["requestedAt"]) < since or self.is_dev(b["opponentId"]):
                continue
            asked = next((flags[g] for g in d["games"] if g in flags), None)
            if asked is False:  # an autoscrim draw; a game missing from the page counts, to be safe
                continue
            used += len(d["games"])
        for bid, row in self.started.items():  # our own requests the battle list has not shown yet
            if bid not in seen and utc(row["requested_at"]) >= since:
                used += int(row.get("games") or SERIES)
        return used

    # ---- choosing and asking ----
    def candidates(self, us, elo, battles):
        last = {}
        for b in battles:
            last[b["opponentId"]] = max(last.get(b["opponentId"], ""), b["at"] or "")
        for r in self.started.values():
            tid = int(r["team_id"])
            last[tid] = max(last.get(tid, ""), r["requested_at"])
        t = now()
        above = [x for x in self.ladder.values()
                 if x["elo"] > elo and not x["dev"] and x["id"] != us
                 and (self.args.max_gap is None or x["elo"] - elo <= self.args.max_gap)
                 and self.skip.get(x["id"], t) <= t]
        above.sort(key=lambda x: (last.get(x["id"], ""), x["elo"]))
        for x in above:
            p = self.profile(x["id"])
            if p.get("hasBot") and p.get("settings", {}).get("ranked", p.get("ranked")) \
                    and p.get("elo", x["elo"]) > elo:
                yield {**x, "elo": p.get("elo", x["elo"])}
            else:
                self.skip[x["id"]] = t + PROFILE_EVERY

    def challenge(self, team, elo, left):
        try:
            reply = self.call("battles", method="POST", content_type="application/json",
                              body=json.dumps({"teamId": team["id"], "ranked": True}).encode())
        except api.ApiError as error:
            text = str(error)
            if error.status == 429 or any(w in text.lower() for w in ("hour", "limit", "allowance", "too many")):
                say(f"  the server refused: {text}; trying again next cycle")
                return "stop"
            say(f"  {team['name']} refused: {text}; skipping them for an hour")
            self.skip[team["id"]] = now() + HOUR
            return "next"
        ids = reply.get("ids") or []  # one id per game; the battle's id is its first game's
        if not ids:
            say(f"  {team['name']}: the server started nothing ({reply}); trying again next cycle")
            return "stop"
        row = {"requested_at": stamp(now()), "battle_id": ids[0], "games": len(ids), "team_id": team["id"],
               "team": team["name"], "their_elo": team["elo"], "our_elo": elo, "games_left": left}
        self.started[int(ids[0])] = row
        append_csv(LOG, row)
        say(f"  challenged {team['name']} ({team['elo']}, +{team['elo'] - elo}) -> battle {ids[0]}")
        return "ok"

    def report(self, battles):
        before = self.tally[0]
        for b in reversed(battles):
            if b["id"] not in self.started or b["id"] in self.reported \
                    or b["outcome"] not in ("win", "loss", "draw"):
                continue
            self.reported.add(b["id"])
            change = b.get("eloChange") or 0
            append_csv(RESULTS, {"battle_id": b["id"], "at": b["at"], "team": b["opponent"],
                                 "their_elo": b["opponentElo"], "wins": b["wins"],
                                 "losses": b["losses"], "elo_change": change})
            self.tally = [self.tally[0] + 1, self.tally[1] + b["wins"], self.tally[2] + b["losses"],
                          self.tally[3] + change]
            say(f"  finished: battle {b['id']} vs {b['opponent']} ({b['opponentElo']}): "
                  f"{b['wins']}-{b['losses']}, rating {change:+d}")
        if self.tally[0] > before:
            n, w, l, c = self.tally
            say(f"  this run: {n} scrims, games {w}-{l}, rating {c:+d}")

    def cycle(self):
        team = self.call("me")["team"]
        us, elo = team["id"], team["elo"]
        self.refresh_ladder()
        battles = self.call("battles?limit=200")
        say(f"[{now():%H:%M:%S}] rating {elo}")
        self.report(battles)
        used = self.games_used(us, battles)
        left = ALLOWANCE - used - self.args.reserve
        say(f"  {used}/{ALLOWANCE} games started by hand in the last hour, {max(left, 0)} free"
              + (f" after keeping {self.args.reserve}" if self.args.reserve else ""))
        asked = 0
        while left >= SERIES and asked < self.args.per_cycle:
            outcome = "none"
            for team_above in self.candidates(us, elo, battles):
                if self.args.dry_run:
                    say(f"  would challenge {team_above['name']} ({team_above['elo']}, "
                          f"+{team_above['elo'] - elo})")
                    return
                outcome = self.challenge(team_above, elo, left)
                if outcome != "next":
                    break
            if outcome == "none" or outcome == "next":
                say("  no team above us can be challenged right now")
            if outcome != "ok":
                return
            left -= SERIES
            asked += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--interval", type=float, default=120, help="seconds between cycles (default 120)")
    ap.add_argument("--per-cycle", type=int, default=1, help="scrims to start at most per cycle (default 1)")
    ap.add_argument("--max-gap", type=int, help="only teams at most this many points above us")
    ap.add_argument("--reserve", type=int, default=0, help="games a hour to leave for teammates (default 0)")
    ap.add_argument("--dry-run", action="store_true", help="count and pick, but challenge nobody")
    ap.add_argument("--once", action="store_true", help="run one cycle and exit")
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # team names are often not Latin

    key = auth.key()
    if not key:
        sys.exit("no API key: run `unswbc auth set <key>` first")
    OUT.mkdir(parents=True, exist_ok=True)
    bot = Scrimmer(Api(key), args)
    while True:
        try:
            bot.cycle()
        except Exception as error:  # left alone overnight: one bad reply must not end the run
            say(f"[{now():%H:%M:%S}] cycle failed: {error!r}; trying again next cycle")
        if args.once:
            break
        time.sleep(args.interval)


if __name__ == "__main__":
    main()
