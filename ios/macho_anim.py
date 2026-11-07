"""Read animation tables from the clear iOS ddiPhone Mach-O.

The armv7 slice is the one whose symbol values match the const data.
FairPlay is present as LC_ENCRYPTION_INFO and inactive: cryptid is 0.

A pose is not one rectangle. ci*Rect records are _IMG_RECT values
(eight signed 16-bit fields). ci*Pat lists those record indexes and ends
each pose with -1. The pivot in each record is the point that lands on
the character origin: the part is drawn at (-px, -py). ci*Act_* is an
int32 bytecode stream. A non-negative word starts a triple
(duration, flags, pattern). A negative word is an opcode.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

# Opcode -> extra int32 words after the opcode word.
# Counts come from the pc increments in each Act_AI_* handler.
OPCODE_EXTRA = {
    1: 0,  # Act_AI_STOP
    2: 0,  # Act_AI_STOP2
    3: 0,  # Act_AI_LOOP
    4: 1,  # Act_AI_LOOP2
    5: 1,  # Act_AI_NEXT
    6: 0,  # Act_AI_SET_FLAG
    7: 0,  # Act_AI_RESET_FLAG
    8: 0,  # Act_AI_IF
    9: 3,  # Act_AI_HITP
    10: 1,  # Act_AI_HITA
    11: 1,  # Act_AI_MOVE
    12: 1,  # Act_AI_MOVE_Y
    13: 1,  # Act_AI_SET_MOVE
    14: 1,  # Act_AI_SET_MOVE2
    15: 0,  # Act_AI_RESET_MOVE
    16: 1,  # Act_AI_ADD_MOVE
    17: 1,  # Act_AI_MUL_MOVE
    18: 1,  # Act_AI_DIV_MOVE
    19: 0,  # Act_AI_SET_GROUND
    20: 0,  # Act_AI_TURN
    21: 1,  # Act_AI_NAGE
    22: 1,  # Act_AI_DAMAGE
    23: 1,  # Act_AI_SOUND
    24: 2,  # Act_AI_OFFSET
}

ARMV7_SLICE = 0x155000
TEXT_VM = 0x1000

# armv7 symbol values, section __TEXT.__const unless noted.
JIMMY_RECT = 0x9B064
JIMMY_PAT = 0x9E664
JIMMY_PAT_POINT = 0x9F438
JIMMY_IMG = 0x9F690
JIMMY_ACT0 = 0xA9FCC
BILLY_RECT = 0x9692A
ZAKO_C00_RECT = 0x7CD92
ZAKO_C00_PAT = 0x7DF42
ZAKO_C00_PAT_POINT = 0x7E48A


@dataclass
class Rect:
    x: int
    y: int
    w: int
    h: int
    px: int
    py: int
    image: int
    extra: int


@dataclass
class Frame:
    duration: int
    flags: int
    pattern: int


def va_to_offset(va: int, slice_off: int = ARMV7_SLICE) -> int:
    return slice_off + (va - TEXT_VM)


def encryption(blob: bytes) -> list[dict]:
    """LC_ENCRYPTION_INFO for every fat slice. cryptid 0 means not encrypted."""
    magic = struct.unpack_from(">I", blob, 0)[0]
    if magic != 0xCAFEBABE:
        raise ValueError("not a fat Mach-O")
    nfat = struct.unpack_from(">I", blob, 4)[0]
    out = []
    pos = 8
    for _ in range(nfat):
        cputype, cpusub, offset, size, _align = struct.unpack_from(">IIIII", blob, pos)
        pos += 20
        ncmds = struct.unpack_from("<I", blob, offset + 16)[0]
        cmd = offset + 28
        info = None
        for _c in range(ncmds):
            kind, cmdsize = struct.unpack_from("<II", blob, cmd)
            if kind == 0x21:
                cryptoff, cryptsize, cryptid = struct.unpack_from("<III", blob, cmd + 8)
                info = {
                    "cputype": cputype,
                    "cpusub": cpusub,
                    "cryptoff": cryptoff,
                    "cryptsize": cryptsize,
                    "cryptid": cryptid,
                }
            cmd += cmdsize
        if info is None:
            raise ValueError("slice has no LC_ENCRYPTION_INFO")
        out.append(info)
    return out


def _load(blob: bytes, va: int, n: int) -> bytes:
    off = va_to_offset(va)
    return blob[off : off + n]


def read_rects(blob: bytes, va: int, nbytes: int) -> list[Rect]:
    raw = _load(blob, va, nbytes)
    if len(raw) % 16:
        raise ValueError("rect table is not a multiple of 16")
    out = []
    for i in range(0, len(raw), 16):
        x, y, w, h, px, py, image, extra = struct.unpack_from("<8h", raw, i)
        out.append(Rect(x, y, w, h, px, py, image, extra))
    return out


def read_i16(blob: bytes, va: int, nbytes: int) -> list[int]:
    raw = _load(blob, va, nbytes)
    return list(struct.unpack(f"<{len(raw) // 2}h", raw))


def read_i32(blob: bytes, va: int, nwords: int) -> list[int]:
    raw = _load(blob, va, nwords * 4)
    return list(struct.unpack(f"<{nwords}i", raw))


def pattern_parts(pat: list[int], points: list[int], index: int) -> list[int]:
    start = points[index]
    out = []
    k = start
    while k < len(pat) and pat[k] >= 0:
        out.append(pat[k])
        k += 1
    return out


def parse_act(words: list[int]) -> tuple[list[Frame], int]:
    """Return the display triples and the ending opcode (positive magnitude).

    Unknown opcodes stop the parse and the opcode is returned as a negative
    number so the caller can see the limit.
    """
    i = 0
    frames: list[Frame] = []
    while i < len(words):
        word = words[i]
        if word >= 0:
            if i + 2 >= len(words):
                break
            frames.append(Frame(word, words[i + 1], words[i + 2]))
            i += 3
            continue
        op = -word
        if op in (1, 3):
            return frames, op
        extra = OPCODE_EXTRA.get(op)
        if extra is None:
            return frames, -op
        i += 1 + extra
    return frames, 0


def jimmy_tables(blob: bytes) -> tuple[list[Rect], list[int], list[int]]:
    rects = read_rects(blob, JIMMY_RECT, JIMMY_PAT - JIMMY_RECT)
    pat = read_i16(blob, JIMMY_PAT, JIMMY_PAT_POINT - JIMMY_PAT)
    points = read_i16(blob, JIMMY_PAT_POINT, JIMMY_IMG - JIMMY_PAT_POINT)
    return rects, pat, points


def jimmy_act0(blob: bytes) -> tuple[list[Frame], int]:
    # Act 1 starts 0x50 bytes later; that gap is the symbol span of act 0.
    words = read_i32(blob, JIMMY_ACT0, 0x50 // 4)
    return parse_act(words)


def compose_box(rects: list[Rect], part_ids: list[int]) -> tuple[int, int, int, int]:
    """Bounding box of a pose in origin space. Does not need the sheet."""
    xs = []
    ys = []
    xe = []
    ye = []
    for rid in part_ids:
        r = rects[rid]
        x0, y0 = -r.px, -r.py
        xs.append(x0)
        ys.append(y0)
        xe.append(x0 + r.w)
        ye.append(y0 + r.h)
    return min(xs), min(ys), max(xe), max(ye)
