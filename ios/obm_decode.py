"""Decode an original OBM sheet. RGBA5551, low nibble first at 4bpp."""

from __future__ import annotations

import struct

from PIL import Image


def _expand(v: int) -> int:
    return (v * 255) // 31


def decode_obm(data: bytes) -> Image.Image:
    if data[:2] != b"OI":
        raise ValueError("not an OBM")
    bpp = data[3]
    width, height = struct.unpack_from("<HH", data, 4)
    colors = 1 << bpp
    palette = data[8 : 8 + colors * 2]
    indices = data[8 + colors * 2 :]
    image = Image.new("RGBA", (width, height))
    pixels = image.load()
    for i in range(width * height):
        if bpp == 8:
            entry = indices[i]
        elif i % 2 == 0:
            entry = indices[i // 2] & 15
        else:
            entry = indices[i // 2] >> 4
        raw = palette[entry * 2] | (palette[entry * 2 + 1] << 8)
        pixels[i % width, i // width] = (
            _expand((raw >> 11) & 31),
            _expand((raw >> 6) & 31),
            _expand((raw >> 1) & 31),
            255 if (raw & 1) else 0,
        )
    return image
