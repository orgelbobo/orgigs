#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""PPM (P6, from tests/host.c) -> PNG at 2x, with nothing but the standard library. Deletes the PPM."""
import struct
import sys
import zlib
from pathlib import Path


def png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


for p in map(Path, sys.argv[1:]):
    if not p.exists():
        continue
    data = p.read_bytes()
    head = data.index(b"255\n") + 4               # "P6 W H 255\n", then the pixels
    _, w, h, _ = data[:head].split()
    w, h = int(w), int(h)
    px = data[head:head + w * h * 3]
    big = bytearray()
    for y in range(h):
        row = bytearray()
        for x in range(w):
            row += px[(y * w + x) * 3:(y * w + x) * 3 + 3] * 2
        big += row + row
    png(p.with_suffix(".png"), w * 2, h * 2, bytes(big))
    p.unlink()
    print("screen ->", p.with_suffix(".png"))
