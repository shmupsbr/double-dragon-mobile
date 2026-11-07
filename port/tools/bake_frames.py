#!/usr/bin/env python3
"""Crop named frames out of the original res.pak.

The sheets are free-packed. Rectangles below were chosen by looking at
connected sprites on JIMMY_1.obm, WILLIAMS.obm and AREA1_CHIP.obm.
They are not an animation table from the encrypted module.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PAK = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / ".." / "investigation" / "cache1327" / "res.pak")
OUT = Path(sys.argv[2] if len(sys.argv) > 2 else ROOT / "assets" / "frames.bin")


def ru32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def ru64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


def load_pak(blob: bytes) -> dict[str, bytes]:
    index = ru32(blob, len(blob) - 8)
    files: dict[str, bytes] = {}

    def parse(off: int, prefix: str) -> int:
        rec = ru32(blob, off + 16)
        typ = ru32(blob, off + 20)
        if typ == 1:
            size = ru64(blob, off + 24)
            data = ru64(blob, off + 8)
            name = blob[off + 32 : blob.index(b"\x00", off + 32)].decode()
            files[prefix + name] = blob[data : data + size]
            return off + rec
        children = ru32(blob, off + 28)
        name = blob[off + 32 : blob.index(b"\x00", off + 32)].decode()
        path = prefix + name + ("/" if name and not name.endswith("/") else "")
        cursor = off + rec
        for _ in range(children):
            cursor = parse(cursor, path)
        return cursor

    parse(index, "")
    return files


def expand5(v: int) -> int:
    return (v * 255) // 31


def decode_obm(data: bytes) -> Image.Image:
    if data[:2] != b"OI":
        raise SystemExit("not an OBM")
    bpp = data[3]
    w, h = struct.unpack_from("<HH", data, 4)
    colors = 1 << bpp
    pal = data[8 : 8 + colors * 2]
    idx = data[8 + colors * 2 :]
    im = Image.new("RGBA", (w, h))
    px = im.load()
    for i in range(w * h):
        if bpp == 8:
            entry = idx[i]
        elif i % 2 == 0:
            entry = idx[i // 2] & 15
        else:
            entry = idx[i // 2] >> 4
        raw = pal[entry * 2] | (pal[entry * 2 + 1] << 8)
        px[i % w, i // w] = (
            expand5((raw >> 11) & 31),
            expand5((raw >> 6) & 31),
            expand5((raw >> 1) & 31),
            255 if (raw & 1) else 0,
        )
    return im


def key_magenta(im: Image.Image) -> Image.Image:
    """Tiles use a magenta placeholder. Character sheets already have alpha."""
    out = im.copy()
    px = out.load()
    w, h = out.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if r > 180 and b > 180 and g < 90:
                px[x, y] = (0, 0, 0, 0)
    return out


# id, sheet, x, y, w, h, magenta_key
# Sheet art faces screen-left. The game mirrors it when facing right.
FRAMES = [
    (1, "res/obm2/JIMMY_1.obm", 0, 50, 38, 43, False),  # idle guard
    (2, "res/obm2/JIMMY_1.obm", 0, 0, 37, 49, False),  # walk
    (3, "res/obm2/JIMMY_1.obm", 126, 0, 32, 50, False),  # stride
    (4, "res/obm2/JIMMY_1.obm", 0, 164, 29, 46, False),
    (5, "res/obm2/JIMMY_1.obm", 86, 0, 38, 43, False),  # punch
    (6, "res/obm2/JIMMY_1.obm", 160, 0, 30, 50, False),  # leg forward, used as kick
    (7, "res/obm2/JIMMY_1.obm", 192, 0, 38, 37, False),  # hurt
    (8, "res/obm2/JIMMY_1.obm", 80, 44, 44, 36, False),  # down
    (9, "res/obm2/JIMMY_1.obm", 40, 84, 38, 42, False),  # special pose
    (11, "res/obm2/WILLIAMS.obm", 0, 41, 42, 38, False),
    (12, "res/obm2/WILLIAMS.obm", 0, 1, 44, 38, False),
    (13, "res/obm2/WILLIAMS.obm", 92, 1, 30, 50, False),
    (14, "res/obm2/WILLIAMS.obm", 168, 2, 29, 41, False),  # attack
    (15, "res/obm2/WILLIAMS.obm", 49, 78, 32, 43, False),  # hurt
    (16, "res/obm2/WILLIAMS.obm", 46, 0, 44, 36, False),  # down-ish
    (20, "res/obm2/face_Jimmy.obm", 0, 0, 32, 32, False),
    (21, "res/obm2/AREA1_CHIP.obm", 0, 456, 192, 14, True),  # cobble row
    (22, "res/obm2/AREA1_CHIP.obm", 0, 0, 160, 96, True),  # brick and fence
]


def main() -> None:
    files = load_pak(PAK.read_bytes())
    cache: dict[str, Image.Image] = {}
    blob = bytearray(b"DDF1")
    blob += struct.pack("<HH", len(FRAMES), 0)
    for fid, path, x, y, w, h, mag in FRAMES:
        if path not in cache:
            raw = files[path]
            cache[path] = decode_obm(raw) if path.endswith(".obm") else Image.open(
                __import__("io").BytesIO(raw)
            ).convert("RGBA")
        crop = cache[path].crop((x, y, x + w, y + h))
        if mag:
            crop = key_magenta(crop)
        ax, ay = w // 2, h - 1
        blob += struct.pack("<HHHHhh", fid, w, h, 0, ax, ay)
        blob += crop.tobytes()
        print(f"id {fid:2} {w:3}x{h:<3} {path} @ {x},{y}")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(blob)
    print("wrote", OUT, len(blob))


if __name__ == "__main__":
    main()
