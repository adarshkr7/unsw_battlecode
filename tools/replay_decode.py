"""Decode a .replay file (packed Cap'n Proto, schema taken from the unswbc replay viewer).

    from replay_decode import load
    rep = load("game.replay")   # dict: map, botA, botB, result, events (list of (kind, fields))
"""

from __future__ import annotations

import struct

DIRS = "NESW"
DEATH = {0: "hit wall", 1: "hit self", 2: "hit other body", 3: "head-to-head", 4: "no valid action"}
EVENT_KINDS = ["roundStart", "turnStart", "pearlCountdown", "tileChange", "dragonAction", "engineLog",
               "dragonLog", "dragonIndicator", "debugDraw", "dragonUpdate", "dragonSplit", "dragonDeath",
               "sonarPing"]
END_REASON = {0: "team eliminated", 1: "round limit"}
SONAR_HIT = {0: "unknown", 1: "empty", 2: "kelp", 3: "ally", 4: "ally head", 5: "enemy", 6: "enemy head"}


def unpack(data: bytes) -> bytes:
    """Cap'n Proto packing: a tag byte says which of the next 8 bytes are non-zero;
    0x00 is followed by a count of extra zero words, 0xff by a count of raw words."""
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        tag = data[i]
        i += 1
        word = bytearray(8)
        for b in range(8):
            if tag & (1 << b):
                word[b] = data[i]
                i += 1
        out += word
        if tag == 0x00:
            out += bytes(8 * data[i])
            i += 1
        elif tag == 0xFF:
            cnt = data[i]
            i += 1
            out += data[i:i + 8 * cnt]
            i += 8 * cnt
    return bytes(out)


class Msg:
    def __init__(self, raw: bytes):
        nseg = struct.unpack_from("<I", raw, 0)[0] + 1
        sizes = struct.unpack_from(f"<{nseg}I", raw, 4)
        off = 4 + 4 * nseg
        off += (8 - off % 8) % 8
        self.segs = []
        for s in sizes:
            self.segs.append(raw[off:off + 8 * s])
            off += 8 * s

    def word(self, seg: int, w: int) -> int:
        return struct.unpack_from("<Q", self.segs[seg], 8 * w)[0]

    def resolve(self, seg: int, w: int):
        """Follow the pointer at (seg, w); returns (seg, target word, pointer word)."""
        p = self.word(seg, w)
        if p == 0:
            return None
        kind = p & 3
        if kind == 2:  # far pointer
            double = (p >> 2) & 1
            pad_off = (p >> 3) & 0x1FFFFFFF
            tseg = p >> 32
            if not double:
                return self.resolve(tseg, pad_off)
            land = self.word(tseg, pad_off)
            tag = self.word(tseg, pad_off + 1)
            return (land >> 32, (land >> 3) & 0x1FFFFFFF, tag)
        off = (p >> 2) & 0x3FFFFFFF
        if off & 0x20000000:
            off -= 0x40000000
        return (seg, w + 1 + off, p)


class Struct:
    __slots__ = ("m", "seg", "data", "dwords", "ptrs")

    def __init__(self, m: Msg, seg: int, start: int, dwords: int, ptrs: int):
        self.m, self.seg, self.data, self.dwords, self.ptrs = m, seg, start, dwords, ptrs

    def _bytes(self, off: int, size: int) -> bytes | None:
        if off + size > 8 * self.dwords:
            return None
        base = 8 * self.data + off
        return self.m.segs[self.seg][base:base + size]

    def u16(self, off):
        b = self._bytes(off, 2)
        return struct.unpack("<H", b)[0] if b else 0

    def i32(self, off):
        b = self._bytes(off, 4)
        return struct.unpack("<i", b)[0] if b else 0

    def u32(self, off):
        b = self._bytes(off, 4)
        return struct.unpack("<I", b)[0] if b else 0

    def u64(self, off):
        b = self._bytes(off, 8)
        return struct.unpack("<Q", b)[0] if b else 0

    def bit(self, n):
        b = self._bytes(n // 8, 1)
        return bool(b and b[0] >> (n % 8) & 1)

    def ptr(self, i):
        if i >= self.ptrs:
            return None
        return self.m.resolve(self.seg, self.data + self.dwords + i)

    def struct(self, i) -> "Struct | None":
        r = self.ptr(i)
        if not r:
            return None
        seg, start, p = r
        return Struct(self.m, seg, start, (p >> 32) & 0xFFFF, p >> 48)

    def text(self, i) -> str:
        r = self.ptr(i)
        if not r:
            return ""
        seg, start, p = r
        n = p >> 35
        return self.m.segs[seg][8 * start:8 * start + n - 1].decode(errors="replace")

    def list_structs(self, i) -> list["Struct"]:
        r = self.ptr(i)
        if not r:
            return []
        seg, start, p = r
        tag = self.m.word(seg, start)
        count = (tag >> 2) & 0x3FFFFFFF
        dw, pw = (tag >> 32) & 0xFFFF, tag >> 48
        step = dw + pw
        return [Struct(self.m, seg, start + 1 + k * step, dw, pw) for k in range(count)]

    def list_u16(self, i) -> list[int]:
        r = self.ptr(i)
        if not r:
            return []
        seg, start, p = r
        count = p >> 35
        return list(struct.unpack_from(f"<{count}H", self.m.segs[seg], 8 * start))


def point(s: Struct | None):
    return (s.i32(0), s.i32(4)) if s else None


def decode_event(e: Struct):
    which = e.u16(0)
    kind = EVENT_KINDS[which] if which < len(EVENT_KINDS) else f"?{which}"
    s = e.struct(0)
    if s is None:
        return kind, {}
    if kind == "roundStart":
        return kind, {"round": s.i32(0)}
    if kind == "turnStart":
        return kind, {"id": s.i32(0)}
    if kind == "pearlCountdown":
        return kind, {"tile": point(s.struct(0)), "countdown": s.i32(0)}
    if kind == "tileChange":
        return kind, {"tile": point(s.struct(0)), "hasPearl": s.bit(0)}
    if kind == "dragonAction":
        a = s.struct(0)
        act = {}
        if a:
            w = a.u16(0)
            if w == 0:
                act = {"move": "".join(DIRS[d] for d in a.list_u16(0))}
            elif w == 1:
                act = {"split": a.i32(4)}
            else:
                act = {"suicide": True}
        ins = s.struct(1)
        return kind, {"id": s.i32(0), **act, "points": ins.u64(0) if ins else None, "tle": s.bit(32)}
    if kind in ("engineLog", "dragonLog", "dragonIndicator"):
        return kind, {"id": s.i32(0), "text": s.text(0)}
    if kind == "dragonUpdate":
        return kind, {"id": s.i32(0), "facing": DIRS[s.u16(4) & 3], "head": point(s.struct(0)),
                      "tail": point(s.struct(1))}
    if kind == "dragonSplit":
        return kind, {"parent": s.i32(0), "child": s.i32(4), "team": "AB"[s.u16(8) & 1],
                      "parentBody": [point(p) for p in s.list_structs(0)],
                      "childBody": [point(p) for p in s.list_structs(1)]}
    if kind == "dragonDeath":
        return kind, {"id": s.i32(0), "reason": DEATH.get(s.u16(4), s.u16(4))}
    if kind == "sonarPing":
        return kind, {"sender": s.i32(0), "dir": DIRS[s.u16(4) & 3], "value": s.u32(8),
                      "origin": point(s.struct(0)), "end": point(s.struct(1)),
                      "hitId": s.i32(12) if s.u16(6) == 1 else None,
                      "hitKind": SONAR_HIT.get(s.u16(24), s.u16(24))}
    return kind, {}


def load(path: str) -> dict:
    m = Msg(unpack(open(path, "rb").read()))
    root = m.resolve(0, 0)
    seg, start, p = root
    r = Struct(m, seg, start, (p >> 32) & 0xFFFF, p >> 48)
    res = r.struct(4)
    result = None
    if res:
        teams = []
        for i in (0, 1):
            t = res.struct(i)
            teams.append({"dragons": t.i32(0), "longest": t.i32(4), "total": t.i32(8)} if t else None)
        result = {"terminated": res.bit(0), "endReason": END_REASON.get(res.u16(2), res.u16(2)),
                  "winner": "AB"[res.u16(6)] if res.u16(4) == 1 else None, "A": teams[0], "B": teams[1]}
    return {"map": r.text(0), "botA": r.text(1), "botB": r.text(2), "result": result,
            "events": [decode_event(e) for e in r.list_structs(3)], "formatVersion": r.u32(0)}


def stats(path: str) -> dict:
    """Fast summary for the eval harness: the final standings and pearls eaten per team."""
    m = Msg(unpack(open(path, "rb").read()))
    seg, start, p = m.resolve(0, 0)
    r = Struct(m, seg, start, (p >> 32) & 0xFFFF, p >> 48)
    team, eaten, cur = {}, {"A": 0, "B": 0}, None
    for line in r.text(0).splitlines():
        if line.startswith("DRAGON "):
            team[len(team)] = "AB"[int(line.split()[1])]
    for e in r.list_structs(3):
        which = e.u16(0)
        if which == 1:  # turnStart
            cur = e.struct(0).i32(0)
        elif which == 3:  # tileChange: a pearl gone during a dragon's turn was eaten by it
            s = e.struct(0)
            if not s.bit(0) and cur in team:
                eaten[team[cur]] += 1
        elif which == 10:  # dragonSplit
            s = e.struct(0)
            team[s.i32(4)] = "AB"[s.u16(8) & 1]
    res = load_result(r)
    return {"result": res, "eaten": eaten}


def load_result(r: Struct):
    res = r.struct(4)
    if not res:
        return None
    teams = []
    for i in (0, 1):
        t = res.struct(i)
        teams.append({"dragons": t.i32(0), "longest": t.i32(4), "total": t.i32(8)} if t else None)
    return {"endReason": END_REASON.get(res.u16(2), res.u16(2)),
            "winner": "AB"[res.u16(6)] if res.u16(4) == 1 else None, "A": teams[0], "B": teams[1]}


if __name__ == "__main__":
    import sys
    if sys.argv[1] == "--stats":
        import json
        print(json.dumps(stats(sys.argv[2])))
        sys.exit(0)
    rep = load(sys.argv[1])
    print("A:", rep["botA"], " B:", rep["botB"], " map:", rep["map"].split("\n")[:3])
    print("result:", rep["result"])
    print(len(rep["events"]), "events")
    for ev in rep["events"][: int(sys.argv[2]) if len(sys.argv) > 2 else 60]:
        print(ev)
