"""Play one match through the unswbc toolkit and print its full result as JSON.

Runs under the toolkit's own Python (tools/tourney.py finds it). Prints the same lines as
`unswbc run`, then one line `RESULT {...}` with the engine's end-of-game numbers, which the
CLI does not print: each team's surviving dragons and total length.

    <unswbc python> tools/match.py MAP BOT_A BOT_B SEED [--native] [--replay PATH]
"""

import json
import sys

import unswbc.engine as engine
from unswbc import run as runmod

captured = {}
original = engine.EngineModule.run


def capture(self, *args, **kwargs):
    result = original(self, *args, **kwargs)
    captured["result"] = result
    return result


engine.EngineModule.run = capture

args = sys.argv[1:]
native = "--native" in args
replay = None
if "--replay" in args:
    i = args.index("--replay")
    replay = args[i + 1]
    del args[i : i + 2]
args = [a for a in args if a != "--native"]
map_path, bot_a, bot_b, seed = args[0], args[1], args[2], int(args[3])

code = runmod.execute(map_path, [bot_a, bot_b], verbose=False, replay_path=replay, no_replay=replay is None,
                      sandbox=not native, seed=seed)
if "result" in captured:
    print("RESULT " + json.dumps(vars(captured["result"])), flush=True)
sys.exit(code)
