#!/usr/bin/env python3
"""Read a calculator screen that is showing a list, row by row.

Driving the OS file browser from a script means knowing two things at once: what
each row says, and which row the cursor is on. The rows are found by looking for
the lines of text (the fixed pitch guesses wrong as soon as the list scrolls),
and the selected one is the row drawn as a filled bar.

    python3 tools/emu/screen-list.py shot.png

Rows are listed top to bottom with `*` marking the selected one. Needs ffmpeg and
tesseract.
"""

import os
import struct
import subprocess
import sys
import tempfile
import zlib

TEXT_LEFT = 16
TEXT_RIGHT = 214
TOP = 22


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
        filt = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        if filt:
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
                    pr = (a if abs(p - a) <= abs(p - b) and abs(p - a) <= abs(p - c)
                          else (b if abs(p - b) <= abs(p - c) else c))
                    row[x] = (row[x] + pr) & 255
        rows.append(row)
    return w, h, rows


def ink_profile(rows, width, height):
    """Dark pixels per scanline, over the name column only."""
    profile = []
    for y in range(height):
        row = rows[y]
        dark = 0
        for x in range(TEXT_LEFT, min(TEXT_RIGHT, width)):
            r, g, b = row[3 * x], row[3 * x + 1], row[3 * x + 2]
            if (r * 299 + g * 587 + b * 114) // 1000 < 140:
                dark += 1
        profile.append(dark)
    return profile


def selection_rows(rows, width, height):
    """Scanlines that are mostly the browser's selection colour."""
    marked = []
    for y in range(height):
        row = rows[y]
        blue = 0
        for x in range(0, width):
            r, g, b = row[3 * x], row[3 * x + 1], row[3 * x + 2]
            if b > 90 and b > r + 30 and b > g + 20:
                blue += 1
        marked.append(blue / float(width) > 0.6)
    return marked


def ocr(path, x, y, w, h):
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "crop.png")
        subprocess.run(
            ["ffmpeg", "-loglevel", "error", "-y", "-i", path,
             "-vf", "crop=%d:%d:%d:%d,scale=iw*8:ih*8:flags=neighbor,format=gray,negate" % (w, h, x, y),
             out], check=True)
        result = subprocess.run(["tesseract", out, "-", "--psm", "7"],
                                capture_output=True, text=True)
        return " ".join(result.stdout.split())


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    path = sys.argv[1]
    width, height, rows = read_png(path)
    profile = ink_profile(rows, width, height)
    marked = selection_rows(rows, width, height)

    print("%s  %dx%d" % (path, width, height))

    # A row of text is a run of scanlines with ink in it; the gaps between rows
    # have none. Runs shorter than three scanlines are noise (a rule, an icon).
    y = TOP
    index = 0
    while y < height - 4:
        if profile[y] > 1:
            start = y
            while y < height - 4 and (profile[y] > 1 or profile[y + 1] > 1):
                y += 1
            end = y
            if end - start >= 4:
                pad = 2
                band = slice(max(0, start - pad), min(height, end + pad))
                selected = any(marked[band])
                text = ocr(path, TEXT_LEFT, max(0, start - pad), TEXT_RIGHT - TEXT_LEFT,
                           min(height, end + pad) - max(0, start - pad))
                index += 1
                print("  %s %2d  y=%3d-%3d  %s" % ("*" if selected else " ", index, start, end, text))
        y += 1

    print("  (rows marked * are the current selection)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
