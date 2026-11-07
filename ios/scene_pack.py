"""Pack the recovered stage-1 scene for the native viewer.

Reads the clear Mach-O and the original OBM zip. Writes scene.bin.
Does not modify either input.
"""

from __future__ import annotations

import argparse
import struct
import zipfile
from pathlib import Path

from PIL import Image

from ios.macho_anim import (
    JIMMY_ACT0,
    JIMMY_ACT1,
    JIMMY_ACT28,
    JIMMY_ACT29,
    JIMMY_ACT45,
    JIMMY_IMG,
    ZAKO_C00_ACT0,
    ZAKO_C00_ACT1,
    ZAKO_C00_ACT10,
    ZAKO_C00_ACT15,
    ZAKO_C00_ACT45,
    ZAKO_C00_IMG,
    ZAKO_C01_IMG,
    ZAKO_C02_IMG,
    act_span,
    attack_record,
    billy_move,
    char_def,
    encounter_rows,
    first_wave,
    floor_bytes,
    floor_ground,
    hero_hp,
    hurt_record,
    zako_attack_record,
    zako_move,
    jimmy_tables,
    pattern_parts,
    read_i16,
    read_rects,
    resource_name,
    stage1_map,
    stage1_spawn,
)
from ios.obm_decode import decode_obm

ZAKO_RECT = 0x7CD92
ZAKO_PAT = 0x7DF42
ZAKO_POINT = 0x7E48A


def _sheets(zf: zipfile.ZipFile, names: set[str]) -> dict[str, Image.Image]:
    out = {}
    for name in names:
        out[name] = decode_obm(zf.read(name))
    return out


def _pose(sheets, rects, imgno, names, parts) -> tuple[Image.Image, int, int]:
    placed = []
    for rid in parts:
        rect = rects[rid]
        if rect.image & 0x8000:
            continue
        placed.append(rect)
    if not placed:
        raise ValueError("pose has no drawable part")
    min_x = min(-r.px for r in placed)
    min_y = min(-r.py for r in placed)
    max_x = max(-r.px + r.w for r in placed)
    max_y = max(-r.py + r.h for r in placed)
    image = Image.new("RGBA", (max_x - min_x, max_y - min_y), (0, 0, 0, 0))
    for rect in placed:
        sheet_name = names[imgno[rect.image]]
        sheet = sheets[sheet_name]
        part = sheet.crop((rect.x, rect.y, rect.x + rect.w, rect.y + rect.h))
        image.alpha_composite(part, (-rect.px - min_x, -rect.py - min_y))
    return image, -min_x, -min_y


def _frames(blob, sheets, names, rects, pat, points, imgno, act_va, act_end):
    frames, _opcode = act_span(blob, act_va, act_end)
    baked = []
    for frame in frames:
        parts = pattern_parts(pat, points, frame.pattern)
        image, ox, oy = _pose(sheets, rects, imgno, names, parts)
        baked.append((frame.duration, frame.flags, ox, oy, image))
    return baked


def _layer(chip: Image.Image, tiles: list[int], width: int, height: int) -> Image.Image:
    """Row 0 is the bottom of the screen.

    0x1faa2 stores 480 at applet+0x47a8 and 320 at +0x47ac.
    MapChipDraw subtracts +0x47ac from the tile Y. glOrthox has
    top 0 and bottom -height, and StoreSprite negates game Y, so
    game +Y grows down from the top. Low rows are the road and
    land at the bottom: image_y = (height - 1 - row) * 16.
    """
    image = Image.new("RGBA", (width * 16, height * 16), (0, 0, 0, 0))
    for x in range(width):
        for y in range(height):
            tid = tiles[x * height + y]
            if tid < 0:
                continue
            sx, sy = (tid & 31) * 16, (tid >> 5) * 16
            image.alpha_composite(
                chip.crop((sx, sy, sx + 16, sy + 16)),
                (x * 16, (height - 1 - y) * 16),
            )
    return image


def _needed_names(blob, imgno: list[int]) -> set[str]:
    names = {resource_name(blob, imgno[i]) for i in range(8)}
    names.add(resource_name(blob, 69))
    return names


def build(blob: bytes, obm_zip: Path) -> tuple[bytes, dict]:
    info = stage1_map(blob)
    spawn_x, spawn_z = stage1_spawn(blob)
    vel_x, vel_z = billy_move(blob)
    with zipfile.ZipFile(obm_zip) as zf:
        jimmy_img = read_i16(blob, JIMMY_IMG, 16)
        zako_img = read_i16(blob, ZAKO_C00_IMG, 8)
        will_img = read_i16(blob, ZAKO_C01_IMG, 8)
        bolo_img = read_i16(blob, ZAKO_C02_IMG, 8)
        linda_img = read_i16(blob, 0x7CD5E, 8)
        names = {i: resource_name(blob, i) for i in set(jimmy_img[:8] + zako_img[:4] + will_img[:4] + bolo_img[:4] + linda_img[:4] + [69])}
        sheets = _sheets(zf, set(names.values()))
    jimmy_rects, jimmy_pat, jimmy_points = jimmy_tables(blob)
    zako_rects = read_rects(blob, ZAKO_RECT, ZAKO_PAT - ZAKO_RECT)
    zako_pat = read_i16(blob, ZAKO_PAT, ZAKO_POINT - ZAKO_PAT)
    zako_points = read_i16(blob, ZAKO_POINT, ZAKO_C00_IMG - ZAKO_POINT)
    idle = _frames(blob, sheets, names, jimmy_rects, jimmy_pat, jimmy_points, jimmy_img, JIMMY_ACT0, JIMMY_ACT1)
    walk = _frames(
        blob, sheets, names, jimmy_rects, jimmy_pat, jimmy_points, jimmy_img, JIMMY_ACT1, JIMMY_ACT1 + 0x50
    )
    bash = _frames(
        blob, sheets, names, zako_rects, zako_pat, zako_points, zako_img, ZAKO_C00_ACT0, ZAKO_C00_ACT0 + 0x5C
    )
    will = _frames(
        blob, sheets, names, zako_rects, zako_pat, zako_points, will_img, ZAKO_C00_ACT0, ZAKO_C00_ACT0 + 0x5C
    )
    punch28 = _frames(blob, sheets, names, jimmy_rects, jimmy_pat, jimmy_points, jimmy_img, JIMMY_ACT28, JIMMY_ACT29)
    punch29 = _frames(blob, sheets, names, jimmy_rects, jimmy_pat, jimmy_points, jimmy_img, JIMMY_ACT29, 0xAA708)
    punch45 = _frames(blob, sheets, names, jimmy_rects, jimmy_pat, jimmy_points, jimmy_img, JIMMY_ACT45, 0xAAB38)
    jdown = _frames(blob, sheets, names, jimmy_rects, jimmy_pat, jimmy_points, jimmy_img, 0xAA27C, 0xAA2FC)
    jland = _frames(blob, sheets, names, jimmy_rects, jimmy_pat, jimmy_points, jimmy_img, 0xAA31C, 0xAA368)
    zwalk_b = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, zako_img, ZAKO_C00_ACT1, 0xB222C)
    zwalk_w = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, will_img, ZAKO_C00_ACT1, 0xB222C)
    zpunch_b = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, zako_img, ZAKO_C00_ACT45, 0xB2B38)
    zpunch_w = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, will_img, ZAKO_C00_ACT45, 0xB2B38)
    zdown_b = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, zako_img, ZAKO_C00_ACT10, 0xB2410)
    zdown_w = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, will_img, ZAKO_C00_ACT10, 0xB2410)
    zland_b = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, zako_img, ZAKO_C00_ACT15, 0xB24DC)
    zland_w = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, will_img, ZAKO_C00_ACT15, 0xB24BC)
    zfall_b = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, zako_img, 0xB2430, 0xB2450)
    zfall_w = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, will_img, 0xB2430, 0xB2450)
    # Act 17 is the alive get-up GT_Down installs. Act 23 is the zero-HP act it hands to GT_KO.
    zget_b = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, zako_img, 0xB24DC, 0xB2514)
    zget_w = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, will_img, 0xB24DC, 0xB2514)
    zlie_b = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, zako_img, 0xB24BC, 0xB24DC)
    zlie_w = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, will_img, 0xB24BC, 0xB24DC)
    zdie_b = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, zako_img, 0xB2618, 0xB2638)
    zdie_w = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, will_img, 0xB2618, 0xB2638)
    jfall = _frames(blob, sheets, names, jimmy_rects, jimmy_pat, jimmy_points, jimmy_img, 0xAA2BC, 0xAA2FC)
    jget = _frames(blob, sheets, names, jimmy_rects, jimmy_pat, jimmy_points, jimmy_img, 0xAA368, 0xAA3AC)
    jlie = _frames(blob, sheets, names, jimmy_rects, jimmy_pat, jimmy_points, jimmy_img, 0xAA348, 0xAA368)
    jdie = _frames(blob, sheets, names, jimmy_rects, jimmy_pat, jimmy_points, jimmy_img, 0xAA4C8, 0xAA528)
    bidle = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, bolo_img, ZAKO_C00_ACT0, ZAKO_C00_ACT0 + 0x5C)
    bwalk = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, bolo_img, ZAKO_C00_ACT1, 0xB222C)
    bpunch = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, bolo_img, ZAKO_C00_ACT45, 0xB2B38)
    bdown = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, bolo_img, ZAKO_C00_ACT10, 0xB2410)
    bland = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, bolo_img, ZAKO_C00_ACT15, 0xB24BC)
    bfall = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, bolo_img, 0xB2430, 0xB2450)
    bget = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, bolo_img, 0xB24DC, 0xB2514)
    blie = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, bolo_img, 0xB24BC, 0xB24DC)
    bdie = _frames(blob, sheets, names, zako_rects, zako_pat, zako_points, bolo_img, 0xB2618, 0xB2638)
    linda_rects = read_rects(blob, 0x7B97C, 0x7C88C - 0x7B97C)
    linda_pat = read_i16(blob, 0x7C88C, 0x7CCA8 - 0x7C88C)
    linda_points = read_i16(blob, 0x7CCA8, 0x7CD5E - 0x7CCA8)
    lidle = _frames(blob, sheets, names, linda_rects, linda_pat, linda_points, linda_img, 0xB3158, 0xB319C)
    lwalk = _frames(blob, sheets, names, linda_rects, linda_pat, linda_points, linda_img, 0xB319C, 0xB31EC)
    lpunch = _frames(blob, sheets, names, linda_rects, linda_pat, linda_points, linda_img, 0xB3A74, 0xB3BA4)
    ldown = _frames(blob, sheets, names, linda_rects, linda_pat, linda_points, linda_img, 0xB33A4, 0xB33C4)
    lland = _frames(blob, sheets, names, linda_rects, linda_pat, linda_points, linda_img, 0xB3444, 0xB3470)
    lfall = _frames(blob, sheets, names, linda_rects, linda_pat, linda_points, linda_img, 0xB33E4, 0xB3404)
    lget = _frames(blob, sheets, names, linda_rects, linda_pat, linda_points, linda_img, 0xB3490, 0xB34C8)
    llie = _frames(blob, sheets, names, linda_rects, linda_pat, linda_points, linda_img, 0xB3470, 0xB3490)
    ldie = _frames(blob, sheets, names, linda_rects, linda_pat, linda_points, linda_img, 0xB35CC, 0xB35EC)
    wave = first_wave(blob)
    zatk = zako_attack_record(blob, 6)
    zvx, zvz = zako_move(blob)
    atk = attack_record(blob, 2)
    close = attack_record(blob, 5)
    hurt = hurt_record(blob, 1)
    chip = sheets[names[69]]
    far = _layer(chip, info["near"], info["width"], info["height"])
    main = _layer(chip, info["main"], info["width"], info["height"])
    ground = []
    for ty in range(info["floor_h"]):
        for tx in range(info["floor_w"]):
            x = (tx - info["origin_x"]) << 4
            z = (ty - 1) << 4
            value = floor_ground(blob, x, z)
            ground.append(-1000000 if value is None else value)
    payload = bytearray()
    payload += struct.pack(
        "<4sHHHiiiiiiiiiiHHHH",
        b"DD1S",
        7,
        480,
        320,
        info["pixel_w"],
        info["pixel_h"],
        spawn_x,
        spawn_z,
        vel_x,
        vel_z,
        95,
        info["floor_w"],
        info["floor_h"],
        info["origin_x"],
        len(idle),
        len(walk),
        len(bash),
        0,
    )
    payload += struct.pack("<4H", len(will), len(punch28), len(punch29), len(punch45))
    payload += struct.pack("<7h", atk[0], atk[1], atk[2], atk[3], atk[4], atk[5], atk[6])
    payload += struct.pack("<7h", close[0], close[1], close[2], close[3], close[4], close[5], close[6])
    payload += struct.pack("<6b", *hurt)
    payload += struct.pack("<2h", 90, 10)
    # Foes are spawned by the row walker, not listed up front.
    payload += struct.pack("<i", 0)
    # Installer image slots: 4 LINDA, 6 BASH, 7 WILLIAMS, 8 BOLO.
    draw_sheet = {4: 3, 6: 0, 7: 1, 8: 2}
    payload += struct.pack(
        "<6H4i7h",
        len(zwalk_b), len(zwalk_w), len(zpunch_b), len(zpunch_w), len(zdown_b), len(jdown),
        zvx, zvz, hero_hp(blob, 1), zatk[6],
        zatk[0], zatk[1], zatk[2], zatk[3], zatk[4], zatk[5], zatk[6],
    )
    payload += struct.pack("<4H", len(zdown_w), len(zland_b), len(zland_w), len(jland))
    payload += struct.pack(
        "<6H", len(zfall_b), len(zfall_w), len(zget_b), len(zget_w), len(jfall), len(jget)
    )
    payload += struct.pack(
        "<6H", len(zlie_b), len(zlie_w), len(jlie), len(zdie_b), len(zdie_w), len(jdie)
    )
    payload += struct.pack(f"<{len(ground)}i", *ground)

    def put_image(image: Image.Image) -> None:
        payload.extend(image.tobytes())

    def put_group(group) -> None:
        for duration, flags, ox, oy, image in group:
            payload.extend(struct.pack("<HIHHhh", duration, flags & 0xFFFFFFFF, image.size[0], image.size[1], ox, oy))
            put_image(image)

    put_image(far)
    put_image(main)
    for group in (
        idle, walk, bash, will, punch28, punch29, punch45,
        zwalk_b, zwalk_w, zpunch_b, zpunch_w, zdown_b, zdown_w, jdown, zland_b, zland_w, jland,
        zfall_b, zfall_w, zget_b, zget_w, jfall, jget,
        zlie_b, zlie_w, jlie, zdie_b, zdie_w, jdie,
        bidle, bwalk, bpunch, bdown, bland, bfall, bget, blie, bdie,
    ):
        put_group(group)
    payload += struct.pack(
        "<9H", len(lidle), len(lwalk), len(lpunch), len(ldown), len(lland),
        len(lfall), len(lget), len(llie), len(ldie),
    )
    for group in (lidle, lwalk, lpunch, ldown, lland, lfall, lget, llie, ldie):
        put_group(group)
    tiles, fmt5 = floor_bytes(blob)
    payload += tiles
    payload += fmt5
    rows, _deferred = encounter_rows(blob)
    payload += struct.pack("<i", len(rows))
    for row in rows:
        kind = row[1]
        if kind == 3:
            _scroll, _k, x, z, hp, draw, flags, y_word, entry, _char = row
            payload += struct.pack("<8i", row[0], 3, x, z, hp, draw_sheet[draw], flags, y_word)
        elif kind == 0:
            payload += struct.pack("<8i", row[0], 0, row[2], row[3], 0, 0, 0, 0)
        else:
            payload += struct.pack("<8i", row[0], 5, 0, 0, 0, 0, 0, 0)
    meta = {
        "spawn": (spawn_x, spawn_z),
        "ground": floor_ground(blob, spawn_x, spawn_z),
        "wave": wave,
        "punch28": len(punch28),
        "punch45": len(punch45),
    }
    return bytes(payload), meta


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--macho", type=Path, required=True)
    parser.add_argument("--obm", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    payload, meta = build(args.macho.read_bytes(), args.obm)
    args.out.write_bytes(payload)
    print(f"wrote {args.out} ({len(payload)} bytes) {meta}")


if __name__ == "__main__":
    main()
