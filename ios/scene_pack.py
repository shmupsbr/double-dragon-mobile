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
    JIMMY_IMG,
    ZAKO_C00_ACT0,
    ZAKO_C00_IMG,
    act_span,
    billy_move,
    floor_ground,
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
        baked.append((frame.duration, ox, oy, image))
    return baked


def _layer(chip: Image.Image, tiles: list[int], width: int, height: int) -> Image.Image:
    image = Image.new("RGBA", (width * 16, height * 16), (0, 0, 0, 0))
    for x in range(width):
        for y in range(height):
            tid = tiles[x * height + y]
            if tid < 0:
                continue
            sx, sy = (tid & 31) * 16, (tid >> 5) * 16
            image.alpha_composite(chip.crop((sx, sy, sx + 16, sy + 16)), (x * 16, y * 16))
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
        names = {i: resource_name(blob, i) for i in set(jimmy_img[:8] + zako_img[:4] + [69])}
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
        1,
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
        160,
    )
    payload += struct.pack(f"<{len(ground)}i", *ground)

    def put_image(image: Image.Image) -> None:
        payload.extend(image.tobytes())

    put_image(far)
    put_image(main)
    for group in (idle, walk, bash):
        for duration, ox, oy, image in group:
            payload += struct.pack("<HHHhh", duration, image.size[0], image.size[1], ox, oy)
            put_image(image)
    meta = {
        "spawn": (spawn_x, spawn_z),
        "ground": floor_ground(blob, spawn_x, spawn_z),
        "idle": len(idle),
        "walk": len(walk),
        "bash": len(bash),
        "bash_name": names[zako_img[0]],
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
