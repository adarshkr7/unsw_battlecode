"""Configurations: a set of setting overrides per scope on top of the base bot, rendered into params.hpp.

A configuration is {"big": {SETTING: value}, "small": {SETTING: value}} relative to the base bot's own
values in that scope (big = the declared values, small = after params::useSmallMapValues()). Its id is a
hash of the canonical overrides and the base bot's source files, so the same bot always gets the same id
(and the same results from the cache).
"""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import shutil

from . import space

ROOT = pathlib.Path(__file__).resolve().parents[2]
DECL = re.compile(r"^inline (constexpr )?([a-z: ]+?) ([A-Z_][A-Z0-9_]*) = ([^;]+);", re.M)
SMALL_FN = re.compile(r"(inline void useSmallMapValues\(\) \{\n)(.*?)(\n\})", re.S)
SMALL_LINE = re.compile(r"^\s+([A-Z_][A-Z0-9_]*) = ([^;]+);", re.M)


def parse_value(lit: str, typ: str):
    lit = lit.strip()
    if typ == "bool":
        return lit == "true"
    if typ in ("int", "long"):
        return int(lit)
    return float(lit.rstrip("f"))


def literal(v, typ: str) -> str:
    if typ == "bool":
        return "true" if v else "false"
    if typ in ("int", "long"):
        return str(int(round(v)))
    r = repr(float(round(float(v), 6)))
    return r if ("." in r or "e" in r) else r + ".0"


class Base:
    """The bot every configuration is built from (default bots/v22)."""

    def __init__(self, folder: str = "bots/v22"):
        self.folder = (ROOT / folder).resolve()
        self.text = (self.folder / "params.hpp").read_text(encoding="utf-8")
        self.decl = {}  # name -> (constexpr, type)
        self.big = {}
        for m in DECL.finditer(self.text):
            self.decl[m[3]] = (bool(m[1]), m[2].strip())
            self.big[m[3]] = parse_value(m[4], m[2].strip())
        fn = SMALL_FN.search(self.text)
        self.small_listed = {}
        if fn:
            for m in SMALL_LINE.finditer(fn[2]):
                self.small_listed[m[1]] = parse_value(m[2], self.decl[m[1]][1])
        self.small = dict(self.big)
        self.small.update(self.small_listed)
        files = sorted(p for p in self.folder.iterdir() if p.is_file() and p.suffix in (".cpp", ".hpp", ".toml"))
        h = hashlib.sha1()
        for p in files:
            h.update(p.name.encode())
            h.update(p.read_bytes())
        self.fingerprint = h.hexdigest()[:12]
        for n in list(space.NUM_BY_NAME) + [k for s in space.SWITCHES for sc in s.alt.values() for k in sc]:
            if n not in self.decl:
                raise SystemExit(f"setting {n} is not in {self.folder / 'params.hpp'}")

    def values(self, scope: str) -> dict:
        return dict(self.big if scope == "big" else self.small)

    def typ(self, name: str) -> str:
        return self.decl[name][1]

    # -- configurations ------------------------------------------------------------------------
    def coerce(self, name: str, v):
        t = self.typ(name)
        if t == "bool":
            return bool(v)
        if t in ("int", "long"):
            return int(round(float(v)))
        return round(float(v), 6)

    def effective(self, overrides: dict, scope: str) -> dict:
        vals = self.values(scope)
        for k, v in (overrides.get(scope) or {}).items():
            vals[k] = self.coerce(k, v)
        return space.derive(vals) if any(k in (overrides.get(scope) or {}) for k in ("FEED_ROUND", "CONSOLIDATE_ROUND")) else vals

    def canonical(self, overrides: dict) -> dict:
        """Only the settings that differ from the base in each scope, after the derived rules."""
        out = {}
        for scope in space.SCOPES:
            eff = self.effective(overrides, scope)
            base = self.values(scope)
            diff = {k: eff[k] for k in sorted(eff) if eff[k] != base[k]}
            if diff:
                out[scope] = diff
        return out

    def config_id(self, overrides: dict) -> str:
        canon = self.canonical(overrides)
        blob = json.dumps({"base": self.fingerprint, "o": canon}, sort_keys=True)
        return hashlib.sha1(blob.encode()).hexdigest()[:12]

    def render(self, overrides: dict) -> str:
        canon = self.canonical(overrides)
        big = self.effective(overrides, "big")
        small = self.effective(overrides, "small")
        changed = sorted(set(canon.get("big", {})) | set(canon.get("small", {})))
        text = self.text
        small_lines = dict(self.small_listed)
        for name in changed:
            constexpr, typ = self.decl[name]
            needs_small = name in small_lines or small[name] != big[name]
            pat = re.compile(rf"^inline (constexpr )?([a-z: ]+?) {name} = ([^;]+);", re.M)
            repl = f"inline {'' if needs_small else ('constexpr ' if constexpr else '')}{typ} {name} = {literal(big[name], typ)};"
            text, n = pat.subn(lambda m: repl, text, count=1)
            if n != 1:
                raise SystemExit(f"could not set {name}")
            if needs_small:
                small_lines[name] = small[name]
        fn = SMALL_FN.search(text)
        body = "\n".join(f"    {k} = {literal(small_lines[k], self.typ(k))};" for k in sorted(small_lines))
        text = text[:fn.start(2)] + body + text[fn.end(2):]
        if "inline constexpr bool DEBUG = false;" not in text:
            raise SystemExit("DEBUG is on")
        return text

    def make_bot(self, overrides: dict, dest: pathlib.Path) -> None:
        """Write the bot folder (idempotent: same overrides, same files)."""
        text = self.render(overrides)
        if (dest / "params.hpp").is_file() and (dest / "params.hpp").read_text(encoding="utf-8") == text:
            return
        shutil.rmtree(dest, ignore_errors=True)
        dest.mkdir(parents=True)
        for f in self.folder.iterdir():
            if f.is_file() and f.suffix in (".cpp", ".hpp", ".toml"):
                shutil.copy(f, dest / f.name)
        (dest / "params.hpp").write_text(text, encoding="utf-8")
