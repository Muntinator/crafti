#!/usr/bin/env python3
"""Build the boot screen (textures/loading.png + textures/loading.h).

This one image is the very first thing drawn: main.cpp hands `loading.bitmap`
straight to nGL as the framebuffer, before any task exists, so it is on screen
from the moment the program starts until the title screen draws over it.

Until now it was Muntcraft's own hand-painted splash. Vanilla ships the art for
an equivalent screen, and both halves of it are already in the tree because the
menu port needs them:

  * `gui/options_background.png` -- the 16x16 dirt tile vanilla tiles behind its
    loading and options screens. gen_gui_textures.py already cuts it into
    `menu_background.h`; this script tiles the same file across the frame.
  * `gui/title/minecraft.png` -- the wordmark. gen_gui_textures.py joins its two
    ink runs into `title_logo.h`; this script joins the same two crops and drops
    the result where vanilla puts it, `width / 2 - 137` and `height / 4`.

So the boot screen is assembled from the same two vanilla files the menu
already draws, rather than from a picture of our own.

The frame is 320x240 and fully opaque: it is a framebuffer, and on a 4-bit
greyscale LCD main.cpp runs `greyscaleTexture()` over it in place. That is why
this script emits its own header instead of leaving `loading.h` to the
`%.h: %.png` ConvertImg rule -- the format is spelled out here next to the
composition that produces it.

Regenerate: python3 tools/textures/gen_loading_textures.py
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pngio import read_png, to_rgb565, write_png

WIDTH = 320
HEIGHT = 240

# The dirt tile vanilla tiles behind its loading screen.
BACKGROUND = "gui/options_background"

# The wordmark's two runs of ink, as (x, y, w, h) crops of gui/title/minecraft.
# These are the same crops COMPOSITES uses for title_logo, so the boot screen
# shows the identical wordmark the title screen does.
LOGO_PARTS = [(0, 0, 155, 44), (0, 45, 119, 44)]


def load(rel):
    path = os.path.join("textures", rel + ".png")
    w, h, px = read_png(path)
    return w, h, px


def crop(px, w, h, x, y, cw, ch):
    out = []
    for row in range(y, y + ch):
        out.extend(px[row * w + x: row * w + x + cw])
    return out


def compose_logo():
    """The wordmark as one row of pixels, `LOGO_W` wide."""
    w, h, px = load("gui/title/minecraft")
    pieces = []
    for (x, y, cw, ch) in LOGO_PARTS:
        if x + cw > w or y + ch > h:
            raise SystemExit("minecraft.png is only %dx%d" % (w, h))
        pieces.append(crop(px, w, h, x, y, cw, ch))
    logo_w = sum(p[2] for p in LOGO_PARTS)
    return logo_w, LOGO_PARTS[0][3], [p for piece in pieces for p in piece]


def paste(dst, dw, src, sw, sh, x, y):
    """Source-over composite `src` onto `dst`.

    The wordmark is a sprite with a transparent margin, so a plain copy would
    punch its own holes through the dirt behind it. Alpha 0 leaves the
    destination alone; anything else blends.
    """
    for row in range(sh):
        dy = y + row
        if dy < 0 or dy >= HEIGHT:
            continue
        for col in range(sw):
            dx = x + col
            if dx < 0 or dx >= dw:
                continue
            sr, sg, sb, sa = src[row * sw + col]
            if sa == 0:
                continue
            i = dy * dw + dx
            if sa == 255:
                dst[i] = (sr, sg, sb, 255)
                continue
            dr, dg, db, _ = dst[i]
            f = sa / 255.0
            dst[i] = (int(dr + (sr - dr) * f), int(dg + (sg - dg) * f),
                      int(db + (sb - db) * f), 255)


def build():
    bw, bh, tile = load(BACKGROUND)
    frame = []
    for row in range(HEIGHT):
        sy = row % bh
        for col in range(0, WIDTH, bw):
            frame.extend((r, g, b, 255)
                         for (r, g, b, a) in tile[sy * bw: sy * bw + bw])

    logo_w, logo_h, logo = compose_logo()
    # vanilla's title screen puts the logo at width / 2 - 137; keep the boot
    # screen on the same centre line so the two do not jump when the menu takes
    # over.
    paste(frame, WIDTH, logo, logo_w, logo_h, WIDTH // 2 - logo_w // 2,
          HEIGHT // 4)

    assert len(frame) == WIDTH * HEIGHT, "%d pixels" % len(frame)
    assert all(p[3] == 255 for p in frame), "the framebuffer must be opaque"
    return frame


def write_header(pixels):
    lines = [
        "// Generated from the Minecraft 1.17.1 GUI textures by",
        "// tools/textures/gen_loading_textures.py -- do not edit by hand.",
        "// Regenerate: python3 tools/textures/gen_loading_textures.py",
        "COLOR loading_data[] = {",
    ]
    per_line = 16
    for i in range(0, len(pixels), per_line):
        chunk = pixels[i:i + per_line]
        sep = "," if i + per_line < len(pixels) else ""
        lines.append("    " + ", ".join("0x%04x" % v for v in chunk) + sep)
    lines += [
        "};",
        "",
        "static TEXTURE loading{",
        ".width = %d," % WIDTH,
        ".height = %d," % HEIGHT,
        ".has_transparency = false,",
        ".transparent_color = 0,",
        ".bitmap = loading_data };",
        "",
    ]
    with open(os.path.join("textures", "loading.h"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    os.chdir(root)

    frame = build()
    write_png(os.path.join("textures", "loading.png"), WIDTH, HEIGHT, frame)
    write_header(to_rgb565(frame))
    print("wrote textures/loading.png and textures/loading.h (%dx%d, tiled %s"
          " + the vanilla wordmark)" % (WIDTH, HEIGHT, BACKGROUND))
    return 0


if __name__ == "__main__":
    sys.exit(main())
