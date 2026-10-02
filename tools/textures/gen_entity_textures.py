#!/usr/bin/env python3
"""Emit the mob skin headers in textures/ from the official Minecraft textures.

The skins live under textures/entity/ exactly as they are named in the vanilla
asset tree (`assets/minecraft/textures/entity/...`), so what is checked in is the
authentic PNG, and the .h next to it is a straight conversion of it:

    textures/entity/cow/cow.png        -> textures/cow.h        (cow_tex)
    textures/entity/creeper/creeper.png-> textures/creeper.h    (creeper_tex)
    ...

Deliberately dependency-free: Pillow is not installed everywhere this repo is
built (the CX toolchain box, for one), and the PNGs are plain 8-bit RGBA, so the
codec in pngio.py is enough. The conversion matches the rest of the texture
pipeline (see gen_creeper_h.py): alpha < 128 becomes 0x0000, which nGL treats as
the transparent colour, and opaque black is nudged to 0x0841 so an eye cannot
turn into a hole.

Run from the repository root:

    python3 tools/textures/gen_entity_textures.py
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pngio import read_png, to_rgb565

# (header stem, PNG under textures/entity/, expected width, expected height)
# The expected size is asserted so a wrong or re-scaled file cannot slip in:
# every mob UV in the renderer is written against these exact dimensions.
SKINS = [
    ("creeper", "creeper/creeper.png", 64, 32),
    ("cow", "cow/cow.png", 64, 32),
    ("pig", "pig/pig.png", 64, 32),
    ("sheep", "sheep/sheep.png", 64, 32),
    ("chicken", "chicken.png", 64, 32),
    ("horse", "horse/horse_white.png", 64, 64),
    ("villager", "villager/type/plains.png", 64, 64),
    ("wolf", "wolf/wolf.png", 64, 32),
    ("mooshroom", "cow/red_mooshroom.png", 64, 32),
    ("donkey", "horse/donkey.png", 64, 64),
    # The player's own skin. Vanilla's default "Steve" is the wide-armed model the
    # player inventory screen draws, so this is the one the front-end needs.
    ("steve", "steve.png", 64, 64),
]


def write_header(stem, pixels, width, height, png_rel):
    path = os.path.join("textures", stem + ".h")
    lines = [
        "// Generated from textures/entity/%s (Minecraft 1.17.1) -- do not edit by hand." % png_rel,
        "// Regenerate: python3 tools/textures/gen_entity_textures.py",
        "static uint16_t %s_data[] = {" % stem,
    ]
    per_line = 12
    for i in range(0, len(pixels), per_line):
        chunk = pixels[i:i + per_line]
        sep = "," if i + per_line < len(pixels) else ""
        lines.append("    " + ", ".join("0x%04x" % v for v in chunk) + sep)
    lines += [
        "};",
        "",
        "static TEXTURE %s_tex = {" % stem,
        "    %d, %d," % (width, height),
        "    true, 0,",
        "    %s_data" % stem,
        "};",
        "",
    ]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    return path


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    os.chdir(root)
    for stem, png_rel, want_w, want_h in SKINS:
        png = os.path.join("textures", "entity", png_rel)
        if not os.path.isfile(png):
            sys.stderr.write("missing %s\n" % png)
            return 1
        width, height, pixels = read_png(png)
        if (width, height) != (want_w, want_h):
            sys.stderr.write("%s: expected %dx%d, got %dx%d\n"
                             % (png, want_w, want_h, width, height))
            return 1
        path = write_header(stem, to_rgb565(pixels), width, height, png_rel)
        print("wrote %s (%dx%d)" % (path, width, height))
    return 0


if __name__ == "__main__":
    sys.exit(main())
