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
    13: 4,  # Act_AI_SET_MOVE reads four words, then one more pc step
    14: 1,  # Act_AI_SET_MOVE2
    15: 0,  # Act_AI_RESET_MOVE
    16: 1,  # Act_AI_ADD_MOVE
    17: 1,  # Act_AI_MUL_MOVE
    18: 1,  # Act_AI_DIV_MOVE
    19: 0,  # Act_AI_SET_GROUND
    20: 0,  # Act_AI_TURN
    21: 1,  # Act_AI_NAGE
    22: 1,  # Act_AI_DAMAGE
    23: 3,  # Act_AI_SOUND leaves pc four words after the opcode
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
JIMMY_ACT1 = 0xAA01C
BILLY_RECT = 0x9692A
BILLY_MOVE = 0xC6398
ZAKO_C00_RECT = 0x7CD92
ZAKO_C00_PAT = 0x7DF42
ZAKO_C00_PAT_POINT = 0x7E48A
ZAKO_C00_IMG = 0x7E56A
ZAKO_C01_IMG = 0x7E58C
ZAKO_C02_IMG = 0x7E5AE
ZAKO_C00_ACT0 = 0xB2174
ZAKO_C00_ACT1 = 0xB21D0
ZAKO_C00_ACT10 = 0xB23F0
ZAKO_C00_ACT15 = 0xB2490
ZAKO_C00_ACT45 = 0xB2AB4
ZAKO_MOVE = 0xC6C50
ZAKO_ATK = 0xC6DD6
HERO_HP_TBL = 0xC54AC
ZAKO_BODY = 0xC6EA6
JIMMY_ACT28 = 0xAA608
JIMMY_ACT29 = 0xAA688
JIMMY_ACT45 = 0xAAA6C
JIMMY_ATK = 0xC67C4
ENC_LIST = 0x64A60
ENC_DATA2 = 0x64C30
ENC_ENEMY = 0x64DE0
ENEMY_TBL = 0xC55D0
# Written at 0x1faa2 and 0x27666: +0x47a8 = 0x1e0, +0x47ac = 0x1e0-0xa0.
SCREEN_W = 0x1E0
SCREEN_H = 0x140
NAME_TABLE = 0x5EAB0
NAME_SLOT = 0x20
STAGE1_RANGE = 0x650B0
STAGE1_SCROLL = 0x650DC
STAGE1_FLOOR_LIST = 0x6517C
STAGE1_FLOOR = 0x651B8
STAGE1_CHIP = 0x65498
STAGE1_CHIP_NEAR = 0x663C0
FLOOR_FORMAT = 0xC2A9C
DRAW_FLAG = 0x604D0
KEY_CONFIG = 0xC2A04
PLAYER_CHAR = 0xA84EC


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
    words = read_i32(blob, JIMMY_ACT0, (JIMMY_ACT1 - JIMMY_ACT0) // 4)
    return parse_act(words)


def act_span(blob: bytes, va: int, end: int) -> tuple[list[Frame], int]:
    words = read_i32(blob, va, (end - va) // 4)
    return parse_act(words)


def resource_name(blob: bytes, index: int) -> str:
    raw = _load(blob, NAME_TABLE + index * NAME_SLOT, NAME_SLOT)
    return raw.split(b"\x00", 1)[0].decode("ascii")


def stage1_map(blob: bytes) -> dict:
    """ciStage01MapChip is column-major. MapChipDraw indexes height * x + y.

    A negative id is skipped. Otherwise tx = id & 31 and ty = id >> 5,
    and the chip is 16 by 16. The header is csStage01MapRange.
    """
    header = read_i16(blob, STAGE1_RANGE, 0x12)
    width = header[4]
    height = header[5]
    count = width * height
    main = read_i16(blob, STAGE1_CHIP, count * 2)
    near = read_i16(blob, STAGE1_CHIP_NEAR, count * 2)
    return {
        "pixel_w": header[0],
        "pixel_h": header[2],
        "width": width,
        "height": height,
        "floor_w": header[6],
        "floor_h": header[7],
        "origin_x": header[8],
        "main": main,
        "near": near,
    }


def stage1_spawn(blob: bytes) -> tuple[int, int]:
    """Player 0 position from csStage01ScrollSet.

    GT_CreatePlayerTask reads +0xc into task+0x5c (X) and +0x10 into
    task+0x7c (Z). Both are pixel values, then shifted into 16.16.
    """
    words = read_i32(blob, STAGE1_SCROLL, 8)
    return words[3], words[4]


def billy_move(blob: bytes) -> tuple[int, int]:
    """First two int32 of ciBillyMoveData. 16.16 fixed point.

    The installer stores this pointer at applet+0x4fa8 (type 0, Billy)
    and at applet+0x4fac (type 1, Jimmy). GT_MoveCheck uses +0 for X
    and +4 for Z.
    """
    words = read_i32(blob, BILLY_MOVE, 2)
    return words[0], words[1]


def player_char(blob: bytes, index: int) -> tuple[int, int, int, int]:
    """csPlayerCharLoadData records are 12 bytes.

    Field 0 is the task type at +0x28. Field 1 is the draw set.
    Fields 2 and 3 are resource-name indexes.
    """
    raw = _load(blob, PLAYER_CHAR + index * 12, 12)
    return struct.unpack("<6h", raw)[:4]


def draw_flags(blob: bytes) -> list[int]:
    """ciDrawFlag. DirectImageDraw indexes extra + (facing_combo << 2)."""
    return read_i32(blob, DRAW_FLAG, 16)


def key_config(blob: bytes) -> list[int]:
    """cuiDefaultKeyConfig. Button index ip matches raw key bit mask[ip]."""
    return read_i32(blob, KEY_CONFIG, 16)


def floor_bytes(blob: bytes) -> tuple[bytes, bytes]:
    """Stage-1 tile ids and cuiFloorFormat index 5.

    Record 0 is height 99999 on every subcell (format 0 is all zeros).
    Record 1 is height 0. Record 2 uses format 5: a set bit selects
    the 99999 height, a clear bit selects 0.
    """
    info = stage1_map(blob)
    tiles = _load(blob, STAGE1_FLOOR, info["floor_w"] * info["floor_h"])
    fmt5 = _load(blob, FLOOR_FORMAT + 5 * 256, 256)
    return tiles, fmt5


def floor_ground(blob: bytes, x: int, z: int) -> int | None:
    """Pixel ground Y from FloorGroundSet, or None when the tile is outside.

    Screen Y of the character origin is this value minus Z. The height
    word 99999 is the void record and is returned as 0xEF - 99999.
    """
    info = stage1_map(blob)
    tile_x = info["origin_x"] + (x >> 4)
    tile_y = (z >> 4) + 1
    if tile_x < 0 or tile_y < 0 or tile_x >= info["floor_w"] or tile_y >= info["floor_h"]:
        return None
    floors = _load(blob, STAGE1_FLOOR, info["floor_w"] * info["floor_h"])
    which = floors[info["floor_w"] * tile_y + tile_x]
    rec = read_i32(blob, STAGE1_FLOOR_LIST + which * 20, 5)
    fmt = struct.unpack_from("<h", struct.pack("<i", rec[0]), 2)[0]
    masks = _load(blob, FLOOR_FORMAT + (fmt << 8), 256)
    bit = masks[((z & 15) << 4) + (x & 15)]
    height = rec[1] if bit == 0 else rec[2]
    return 0xEF - height


def attack_record(blob: bytes, index: int) -> tuple[int, ...]:
    """One _ATK_DATA. HitRangeCheck reads x,y,w,h,z,zs. DamageCalc reads +0xc."""
    raw = _load(blob, JIMMY_ATK + index * 26, 26)
    return struct.unpack("<13h", raw)


def zako_attack_record(blob: bytes, index: int) -> tuple[int, ...]:
    """ciZakoC00AtkData. Act 45's HITA selects index 6. Byte +0x11 is the reaction."""
    raw = _load(blob, ZAKO_ATK + index * 26, 26)
    geom = struct.unpack("<13h", raw)
    return geom + (raw[0x11],)


def hero_hp(blob: bytes, index: int) -> int:
    """GT_CreatePlayerTask loads this int16 at stride 4. Entries 0 and 1 are both 100."""
    raw = _load(blob, HERO_HP_TBL + index * 4, 2)
    return struct.unpack("<h", raw)[0]


def push_x(self_x: int, self_face: int, other_x: int, other_face: int, delta: int) -> int:
    """PushHitCheckX separation. Index-1 box xoff=-17, width=29.

    Both facing 0 and moving right lands on other_x - 30.
    """
    xoff, width = -17, 29
    if delta > 0:
        edge = other_x - xoff - width if other_face else other_x + xoff
        neu = xoff + edge if self_face else (edge - width) - xoff
        return neu - 1
    if delta < 0:
        edge = other_x - xoff if other_face else other_x + width + xoff
        neu = xoff + width + edge if self_face else edge - xoff
        return neu + 1
    return self_x


def zako_move(blob: bytes) -> tuple[int, int]:
    words = read_i32(blob, ZAKO_MOVE, 2)
    return words[0], words[1]


def hurt_record(blob: bytes, index: int) -> tuple[int, ...]:
    """ciZakoC00BodyData. Act 0's HITP selects index 1 for these zako."""
    raw = _load(blob, ZAKO_BODY + index * 6, 6)
    return struct.unpack("<6b", raw)


def enemy_def(blob: bytes, index: int) -> tuple[int, int, int, int]:
    """csStage01EncEnemyData, 24 bytes. Returns char id, x, z, flags."""
    words = read_i32(blob, ENC_ENEMY + index * 24, 6)
    return words[0], words[1], words[3], words[5]


def char_def(blob: bytes, index: int) -> tuple[int, int, int]:
    """csEnemyDataTbl, 24 bytes. Returns type, draw set, hp at +0xc."""
    words = read_i32(blob, ENEMY_TBL + index * 24, 6)
    return words[0], words[1], words[3]


# csStage01EncObjData placements the stage-1 kind-9 rows name.
# phase is the applet+0x47f4 value GT_Stage01DoorReady / WallReady compare.
# place_y is the placement Y. The sprite origin is (0xEF - place_y) - Z,
# because +0x44 bit 1 is clear, so the drawer uses Y - Z, then the camera.
# fire_mode 1: DoorOpen fires when the act frame carries flag 0x10000000.
# fire_mode 2: WallBreak fires when the act reaches STOP.
# The fence's extra 30 has no EncData2 key, and stage 1 never stores phase 3 or 4.
OBJ_PHASE = {
    0: (767, 64, 1, 10, 0, 1),
    1: (1000, 80, 2, 20, -16, 2),
    2: (1320, 80, 0, 30, 0, 0),
}


def enc_data2(blob: bytes) -> list[tuple[int, int, int, int]]:
    """csStage01EncData2 rows (key, kind, param, extra) through the -1 key."""
    out = []
    raw = _load(blob, ENC_DATA2, 16 * 48)
    for i in range(48):
        key, kind, param, extra = struct.unpack_from("<4i", raw, i * 16)
        if key < 0:
            break
        out.append((key, kind, param, extra))
    return out


def encounter_rows(blob: bytes) -> tuple[list[tuple], list[tuple]]:
    """Split csStage01EncData into rows this scene can dispatch and rows it cannot.

    Implemented kinds are 0 (quota), 3 (enemy), 5 (scroll-lock increment) and
    the three kind-9 objects whose ready procs watch applet+0x47f4.
    Kind 4 items, the scroll-0 scenery objects, and kind 10 at scroll 1055
    stay deferred. Kind 10 tasks that EncData2 itself starts are not rows.
    """
    done = []
    deferred = []
    raw = _load(blob, ENC_LIST, 16 * 40)
    for i in range(40):
        scroll, kind, param, extra = struct.unpack_from("<4i", raw, i * 16)
        if scroll < 0:
            break
        if kind == 3:
            char_id, abs_x, y_word, depth, entry, flags = struct.unpack_from(
                "<6i", _load(blob, ENC_ENEMY + param * 24, 24)
            )
            _typ, draw, hp = char_def(blob, char_id)
            done.append((scroll, kind, abs_x, depth, hp, draw, flags, y_word, entry, char_id))
        elif kind == 0:
            trigger = extra if extra < 65535 else -1
            done.append((scroll, kind, param, trigger))
        elif kind == 5:
            done.append((scroll, kind))
        elif kind == 9 and param in OBJ_PHASE:
            x, z, phase, obj_extra, place_y, fire_mode = OBJ_PHASE[param]
            done.append((scroll, 9, x, z, phase, obj_extra, place_y, fire_mode))
        else:
            deferred.append((scroll, kind, param, extra))
    return done, deferred


def scroll_enemies(blob: bytes, scroll_x: int) -> list[tuple[int, int, int, int, int, int]]:
    """Kind-3 rows of csStage01EncData whose scroll word equals scroll_x.

    Returns (enemy index, char id, x, z, hp, draw set). Flag bit 1 places
    the body at camera + 480 + 20. Other rows keep the absolute X.
    """
    out = []
    raw = _load(blob, ENC_LIST, 16 * 40)
    for i in range(40):
        scroll, kind, param, _extra = struct.unpack_from("<4i", raw, i * 16)
        if scroll < 0:
            break
        if kind != 3 or scroll != scroll_x:
            continue
        char_id, abs_x, depth, flags = enemy_def(blob, param)
        typ, draw, hp = char_def(blob, char_id)
        x = SCREEN_W + 20 if flags & 2 else abs_x
        out.append((param, char_id, x, depth, hp, draw))
    return out


def first_wave(blob: bytes) -> list[tuple[int, int, int, int, int]]:
    """Kind-3 rows of csStage01EncData with scroll X 0.

    The scroll byte of csStage01ScrollSet is 0, so ScrollEncountCheck
    uses the path that spawns while record X <= camera and stops after.
    Returns (enemy index, char id, x, z, hp) using screen width 480.
    """
    out = []
    raw = _load(blob, ENC_LIST, 16 * 40)
    for i in range(40):
        scroll, kind, param, _extra = struct.unpack_from("<4i", raw, i * 16)
        if scroll < 0:
            break
        if scroll > 0:
            break
        if kind != 3:
            continue
        char_id, _abs_x, depth, flags = enemy_def(blob, param)
        _typ, _draw, hp = char_def(blob, char_id)
        x = SCREEN_W + 20 if flags & 2 else _abs_x
        out.append((param, char_id, x, depth, hp))
    return out


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
