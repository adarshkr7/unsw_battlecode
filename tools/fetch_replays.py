"""Download ladder replays through the API, with the key `unswbc auth set` stored.

Run with the toolkit's Python (it uses unswbc's own auth and API helpers):
    <unswbc python> tools/fetch_replays.py GAME_ID [GAME_ID ...]
    <unswbc python> tools/fetch_replays.py 457724-457728        (a range of game ids)

Replays land in results/ladder/<id>.replay; summarise them with tools/replay.cjs or
tools/replay_results.cjs. The key is only ever sent to the game server: the replay link the
server redirects to is signed, and gets no Authorization header.
"""

import gzip
import pathlib
import sys
import urllib.error
import urllib.request

from unswbc import __version__, api, auth

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "results" / "ladder"
GZIP_MAGIC = bytes([0x1F, 0x8B])


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def game_ids(args):
    for a in args:
        if "-" in a:
            lo, hi = map(int, a.split("-"))
            yield from range(lo, hi + 1)
        else:
            yield int(a)


def main():
    key = auth.key()
    if not key:
        sys.exit("no API key: run `unswbc auth set <key>` first")
    OUT.mkdir(parents=True, exist_ok=True)
    opener = urllib.request.build_opener(_NoRedirect)
    for gid in game_ids(sys.argv[1:]):
        ask = urllib.request.Request(api.url(f"/battles/{gid}/replay"))
        ask.add_header("User-Agent", f"unswbc/{__version__}")
        ask.add_header("Authorization", f"Bearer {key}")
        try:
            with opener.open(ask) as reply:
                data = reply.read()
        except urllib.error.HTTPError as error:
            location = error.headers.get("Location")
            if error.code not in (301, 302, 303, 307, 308) or not location:
                print(f"{gid}: HTTP {error.code}")
                continue
            with urllib.request.urlopen(location) as reply:  # signed link: no key
                data = reply.read()
        if data[:2] == GZIP_MAGIC:  # the API sends replays gzipped
            data = gzip.decompress(data)
        (OUT / f"{gid}.replay").write_bytes(data)
        print(f"{gid}: {len(data) // 1024} KB")


if __name__ == "__main__":
    main()
