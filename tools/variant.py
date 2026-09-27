"""Copy mybot into bots/NAME with some params changed, for A/B tests with tools/tourney.py.

    python tools/variant.py v2d_nodive DIVE_MAX_LEN=0 LOW_UNITS=0
"""

import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
name, changes = sys.argv[1], sys.argv[2:]
dest = ROOT / "bots" / name
shutil.rmtree(dest, ignore_errors=True)
dest.mkdir(parents=True)
for f in (ROOT / "mybot").iterdir():
    if f.is_file() and f.suffix in (".cpp", ".hpp", ".toml"):
        shutil.copy(f, dest / f.name)
params = (dest / "params.hpp").read_text()
for change in changes:
    key, value = change.split("=", 1)
    params, n = re.subn(rf"(inline constexpr \w[\w ]* {key} = )[^;]+;", rf"\g<1>{value};", params)
    if n != 1:
        sys.exit(f"no single param named {key}")
(dest / "params.hpp").write_text(params, newline="\n")
print(f"bots/{name}: " + (", ".join(changes) or "same as mybot"))
