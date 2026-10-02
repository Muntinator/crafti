#!/usr/bin/env python3
"""A tiny, dependency-free PNG codec for the texture generators.

Pillow is not installed everywhere this repo is built (the CX toolchain box, for
one), and every asset the generators touch is a plain 8-bit PNG, so a fifty-line
decoder and a thirty-line writer are enough. Both are shared by
gen_entity_textures.py and gen_block_textures.py -- the entity one only reads,
the block one also writes the atlas back out as a PNG.

Pixels are always `(r, g, b, a)` tuples of ints, in row-major order starting at
the *top* row, which is the orientation nGL's atlas code expects.
"""

from __future__ import annotations

import struct
import zlib


def read_png(path):
    """Decode an 8-bit, non-interlaced PNG into (width, height, [(r,g,b,a), ...])."""
    with open(path, "rb") as f:
        data = f.read()

    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)

    pos = 8
    width = height = colortype = None
    idat = bytearray()
    palette = None
    trns = None
    while pos + 8 <= len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, bitdepth, colortype, _comp, _filt, interlace = struct.unpack(
                ">IIBBBBB", chunk
            )
            if bitdepth != 8 or interlace != 0:
                raise ValueError("%s: only 8-bit, non-interlaced PNGs" % path)
        elif ctype == b"PLTE":
            palette = chunk
        elif ctype == b"tRNS":
            trns = chunk
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break

    if width is None:
        raise ValueError("%s has no IHDR" % path)

    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colortype]
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    rows = []
    prev = bytearray(stride)
    i = 0
    for _y in range(height):
        ftype = raw[i]
        i += 1
        line = bytearray(raw[i:i + stride])
        i += stride
        if ftype == 1:  # Sub
            for x in range(channels, stride):
                line[x] = (line[x] + line[x - channels]) & 0xFF
        elif ftype == 2:  # Up
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xFF
        elif ftype == 3:  # Average
            for x in range(stride):
                a = line[x - channels] if x >= channels else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif ftype == 4:  # Paeth
            for x in range(stride):
                a = line[x - channels] if x >= channels else 0
                b = prev[x]
                c = prev[x - channels] if x >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pred) & 0xFF
        elif ftype != 0:
            raise ValueError("%s: unknown filter %d" % (path, ftype))
        rows.append(line)
        prev = line

    pixels = []
    for row in rows:
        for idx in range(0, stride, channels):
            if colortype == 6:
                r, g, b, a = row[idx], row[idx + 1], row[idx + 2], row[idx + 3]
            elif colortype == 2:
                r, g, b, a = row[idx], row[idx + 1], row[idx + 2], 255
            elif colortype == 0:
                r = g = b = row[idx]
                a = 255
            elif colortype == 4:
                r = g = b = row[idx]
                a = row[idx + 1]
            else:  # palette
                pi = row[idx]
                r, g, b = palette[pi * 3:pi * 3 + 3]
                a = trns[pi] if trns is not None and pi < len(trns) else 255
            pixels.append((r, g, b, a))
    return width, height, pixels


def write_png(path, width, height, pixels):
    """Write 8-bit RGBA pixels as a PNG (filter 0 on every scanline)."""
    if len(pixels) != width * height:
        raise ValueError("expected %d pixels, got %d" % (width * height, len(pixels)))

    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type: none
        for r, g, b, a in pixels[y * width:(y + 1) * width]:
            raw += bytes((r, g, b, a))

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")

    with open(path, "wb") as f:
        f.write(png)


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def to_rgb565(pixels):
    """Convert RGBA pixels to the engine's format.

    nGL treats 0x0000 as the transparent colour, so alpha below 128 becomes
    exactly that, and the few opaque pixels that would round to 0x0000 anyway
    (near-black eyes, outlines) are nudged to the darkest grey instead of
    turning into holes.
    """
    out = []
    for r, g, b, a in pixels:
        if a < 128:
            out.append(0x0000)
        else:
            value = rgb565(r, g, b)
            out.append(value if value != 0 else 0x0841)
    return out
