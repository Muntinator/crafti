#!/usr/bin/env python3
"""Peek at a captured frame from the terminal.

The harness writes ordinary PNGs, so this is only needed when there is no way to
open one: it prints a coarse colour map (one character per block of pixels) and
the RGB values down the middle of the picture.

    python3 tools/pcsim/frame.py /tmp/munt-sim/shots/world.png
"""

import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    i, idat, w, h = 8, b"", 0, 0
    while i < len(data):
        length = struct.unpack(">I", data[i:i + 4])[0]
        kind = data[i + 4:i + 8]
        chunk = data[i + 8:i + 8 + length]
        if kind == b"IHDR":
            w, h = struct.unpack(">II", chunk[:8])
        if kind == b"IDAT":
            idat += chunk
        i += 12 + length
    raw = zlib.decompress(idat)
    stride = w * 3
    rows = []
    for y in range(h):
        start = y * (stride + 1)
        row = bytearray(raw[start + 1:start + 1 + stride])
        filt = raw[start]
        if filt:  # the harness only ever writes filter 0
            prev = rows[-1] if rows else bytearray(stride)
            for x in range(stride):
                a = row[x - 3] if x >= 3 else 0
                b = prev[x]
                c = prev[x - 3] if x >= 3 else 0
                if filt == 1:
                    row[x] = (row[x] + a) & 255
                elif filt == 2:
                    row[x] = (row[x] + b) & 255
                elif filt == 3:
                    row[x] = (row[x] + (a + b) // 2) & 255
                else:
                    p = a + b - c
                    pr = a if abs(p - a) <= abs(p - b) and abs(p - a) <= abs(p - c) else (b if abs(p - b) <= abs(p - c) else c)
                    row[x] = (row[x] + pr) & 255
        rows.append(row)
    return w, h, rows


def classify(r, g, b):
    v = (r * 299 + g * 587 + b * 114) // 1000
    if v < 24:
        return " "
    if v < 60:
        return "."
    if r > g + 32 and r > b + 32:
        return "o" if v > 120 else "r"   # orange / dark red-brown
    if g > r + 16 and g > b + 16:
        return "G" if v > 90 else "g"    # grass
    if b > r + 16 and b > g + 8:
        return "B" if v > 120 else "b"   # sky / water
    return "#" if v > 150 else ("+" if v > 90 else "-")  # greys


def main():
    path = sys.argv[1]
    step = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    w, h, rows = read_png(path)
    print("%s  %dx%d" % (path, w, h))
    for y in range(0, h, step):
        line = "".join(classify(rows[y][3 * x], rows[y][3 * x + 1], rows[y][3 * x + 2])
                       for x in range(0, w, step // 2))
        print("%3d %s" % (y, line))

    print("\nmiddle column, every %d rows:" % (h // 16))
    x = w // 2
    for y in range(0, h, max(1, h // 16)):
        r, g, b = rows[y][3 * x], rows[y][3 * x + 1], rows[y][3 * x + 2]
        print("  y=%3d  (%3d,%3d,%3d)" % (y, r, g, b))


if __name__ == "__main__":
    main()
