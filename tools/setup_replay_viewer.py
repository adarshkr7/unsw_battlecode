"""Extract the replay decoder from the installed toolkit's VS Code viewer, for tools/replay.mjs.

The viewer bundle ends by mounting its UI; we swap that for a line that exposes its decoder
(`Hf`: replay bytes -> {match, events, result, ...}). The output goes to results/, which git
ignores, since the code belongs to the toolkit. Re-run after upgrading unswbc.

    python tools/replay_setup.py
"""

import pathlib
import re
import subprocess
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "results" / "replay_decoder.cjs"

tool_dir = subprocess.run(["uv", "tool", "dir"], capture_output=True, text=True, check=True).stdout.strip()
vsix = next(pathlib.Path(tool_dir).glob("unswbc/**/replay-viewer.vsix"), None)
if vsix is None:
    sys.exit("replay-viewer.vsix not found; is unswbc installed with uv?")

source = zipfile.ZipFile(vsix).read("extension/dist/webview/webview.js").decode("utf-8")
mount = re.search(r"var (\w+)=document\.getElementById\(`app`\);[^;]*\}\)\}\)\(\);\s*$", source)
if mount is None:
    sys.exit("viewer bundle changed shape: cannot find the UI mount to replace")
patched = source[: mount.start()] + "globalThis.__replay={decode:Hf,parse:Bf};})();\n"
OUT.parent.mkdir(exist_ok=True)
OUT.write_text(patched, encoding="utf-8")
print(f"wrote {OUT} from {vsix.name}")
