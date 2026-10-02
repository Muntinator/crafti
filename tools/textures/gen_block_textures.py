#!/usr/bin/env python3
"""Build the block atlas (textures/terrain3.png + textures/terrain3.h).

The art is the official Minecraft 1.17.1 texture set:

    textures/block/<name>.png        assets/minecraft/textures/block/<name>.png
    textures/entity/chest/normal.png the chest's model unwrap
    textures/entity/bed/red.png      the red bed's model unwrap
    textures/item/cake.png           the cake item icon

Kept under those vanilla names so what is checked in is the authentic file, and
this script only *arranges* them: the engine samples one 16x16 tile per block
face out of a 16x16 grid, which is a layout older than these assets, and lots of
renderers hardcode their tile by position (`terrain_atlas[x][y]`), so the
coordinates have to stay exactly as they are.

Which official file lands on which coordinate follows the block table in
terrain.cpp; where a block is drawn from a texture that is not a block texture
in vanilla (chest, bed, cake, water, lava, redstone dust, the breaking overlay,
wheat stages) the vanilla source is named explicitly below.

Colours that vanilla *tints* at render time -- grass, leaves, water -- are left
grey here and tinted by terrainInit() (and by this script, for water, which the
engine draws without a colour). The two exceptions are composed here rather
than by hand: the chest and the bed are block *entities* in vanilla, so their
texture is a model unwrap, and the pieces this engine needs are cut out of it.

Run from the repository root:

    python3 tools/textures/gen_block_textures.py
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pngio import read_png, to_rgb565, write_png

GRID = 16               # tiles per axis in the atlas
TILE = 16               # pixels per tile
ATLAS = GRID * TILE     # 256

# Vanilla 1.17.1 render-time tints, scaled to 0..255, for the default (plains)
# biome. terrainInit() multiplies the grey texture by these, exactly like the
# vanilla render types do.
GRASS = (145, 189, 89)    # 0x91BD59, the grass colour of grass.png's default
FOLIAGE = (119, 171, 47)  # 0x77AB2F, the default foliage colour
WATER = (63, 118, 228)    # 0x3F76E4, the default water colour

CHEST = "entity/chest/normal.png"
BED = "entity/bed/red.png"
CAKE_ITEM = "item/cake.png"


# --- small pixel helpers -----------------------------------------------------

def load(rel):
    return read_png(os.path.join("textures", rel))


def crop(px, w, h, x, y, cw, ch):
    """Cut a rectangle out of an image, top-left origin."""
    out = []
    for yy in range(y, y + ch):
        out.extend(px[yy * w + x:yy * w + x + cw])
    return out, cw, ch


def vstack(parts):
    """Stack (pixels, w, h) parts vertically. All parts must be equally wide."""
    width = parts[0][1]
    px = []
    for part, pw, _ph in parts:
        assert pw == width
        px.extend(part)
    return px, width, sum(ph for _p, _w, ph in parts)


def resize(px, w, h, nw, nh):
    """Nearest-neighbour rescale, so no colour is ever invented."""
    out = []
    for y in range(nh):
        sy = y * h // nh
        for x in range(nw):
            out.append(px[sy * w + x * w // nw])
    return out


def paste(dst, dw, dh, src, sw, sh, x, y):
    for j in range(sh):
        for i in range(sw):
            dst[(y + j) * dw + (x + i)] = src[j * sw + i]


def flatten(px, tint_color=None, solid=False):
    """Cut alpha to a hard edge (nGL has no alpha blending) and tint if asked."""
    out = []
    for r, g, b, a in px:
        if a < 128 and not solid:
            out.append((0, 0, 0, 0))
            continue
        if tint_color is not None:
            r = r * tint_color[0] // 255
            g = g * tint_color[1] // 255
            b = b * tint_color[2] // 255
        out.append((r, g, b, 255))
    return out


def fill_holes(px, w, h):
    """Close the alpha holes of a leaf-style texture with their neighbours.

    The engine's "fast" leaves are drawn as an opaque cube, so the texture's
    cut-out pixels would show up as black speckles. Growing the surrounding
    colour inwards is what a mipmapped cut-out looks like, and it invents no
    colour of its own.
    """
    px = list(px)
    known = [a >= 128 for _r, _g, _b, a in px]
    while not all(known):
        grown = list(px)
        newly = list(known)
        progress = False
        for y in range(h):
            for x in range(w):
                i = y * w + x
                if known[i]:
                    continue
                total = [0, 0, 0]
                n = 0
                for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h and known[ny * w + nx]:
                        r, g, b, _a = px[ny * w + nx]
                        total[0] += r
                        total[1] += g
                        total[2] += b
                        n += 1
                if n:
                    grown[i] = (total[0] // n, total[1] // n, total[2] // n, 255)
                    newly[i] = True
                    progress = True
        px, known = grown, newly
        if not progress:
            break
    return [(r, g, b, 255) for r, g, b, _a in px]


def tile(rel, crop_box=None, size=(TILE, TILE), tint_color=None, solid=False):
    """One atlas tile: a whole texture, or a crop of one, rescaled to 16x16."""
    w, h, px = load(rel)
    if crop_box is not None:
        px, w, h = crop(px, w, h, *crop_box)
    px = flatten(px, tint_color, solid)
    if (w, h) != size:
        px = resize(px, w, h, size[0], size[1])
    return px


def block(name, **kw):
    return tile("block/%s.png" % name, **kw)


# --- the block entities ------------------------------------------------------

def chest_tiles():
    """(top, side, front) cut out of the chest model's unwrap.

    `normal.png` is 64x64 and holds two 14x{5,10}x14 boxes: the lid at texture
    offset (0,0) and the body at (0,19). A box of that size unwraps to
      -x at (0,14) and (0,33)   +x at (28,14) and (28,33)
      +y at (14,0)  and (14,19) -y at (28,0)  and (28,19)
      +z at (14,14) and (14,33) -z at (42,14) and (42,33)
    so the lid's top face is (14,0)-(28,14), a side is the lid's face above the
    body's (5 + 10 rows), and the front additionally carries the latch from the
    2x4 knob at (1,1).
    """
    _w, _h, px = load(CHEST)
    src = (px, 64, 64)

    top = tile(CHEST, crop_box=(14, 0, 14, 14))

    lid_face = crop(*src, 0, 14, 14, 5)
    body_face = crop(*src, 0, 33, 14, 10)
    side = resize(*vstack([lid_face, body_face]), TILE, TILE)

    front = list(side)
    latch = resize(*crop(*src, 1, 1, 2, 4), 4, 8)
    paste(front, TILE, TILE, latch, 4, 8, 6, 4)

    return top, side, front


def bed_tiles():
    """(head top, underside, side, foot top) cut out of the red bed model's unwrap.

    `red.png` is 64x64. Its parts are 16-pixel-ish panels: the head's top with
    the pillow at (0,0) 28x22, the foot's blanket at (0,28) 28x16, and the
    wooden frame at (28,6) 16x16. The underside is the frame again -- a bed is
    built on planks -- and a side is the frame with the blanket's edge turned
    over its top, which is how the two meet on the real model.
    """
    _w, _h, px = load(BED)
    src = (px, 64, 64)

    head = tile(BED, crop_box=(0, 0, 28, 22))
    foot = tile(BED, crop_box=(0, 28, 28, 16))
    frame = tile(BED, crop_box=(28, 6, TILE, TILE))

    edge = (resize(*crop(*src, 0, 28, 28, 4), TILE, 4), TILE, 4)
    board = crop(*src, 28, 6, TILE, TILE - 4)
    side = resize(*vstack([edge, board]), TILE, TILE)

    return head, frame, side, foot


# --- the atlas ---------------------------------------------------------------

def build():
    """Return the whole atlas as one 256x256 RGBA image."""
    tiles = []

    def at(x, y, px):
        assert len(px) == TILE * TILE, "(%d,%d) is not a 16x16 tile" % (x, y)
        tiles.append((x, y, px))

    # Row 0 -- the top of the pack, where the classic terrain atlas starts.
    at(0, 0, block("grass_block_top"))              # grey; tinted green below
    at(1, 0, block("stone"))
    at(2, 0, block("dirt"))
    at(3, 0, block("grass_block_side"))             # already carries its green
    at(4, 0, block("oak_planks"))
    at(7, 0, block("bricks"))                       # BLOCK_WALL, i.e. "bricks"
    at(8, 0, block("tnt_side"))
    at(9, 0, block("tnt_top"))
    at(10, 0, block("tnt_bottom"))
    at(11, 0, block("cobweb"))                      # spiderweb billboard
    at(12, 0, block("poppy"))                       # flower data 0
    at(13, 0, block("dandelion"))                   # flower data 1

    # Row 1
    at(0, 1, block("cobblestone"))
    at(1, 1, block("bedrock"))
    at(2, 1, block("sand"))
    at(4, 1, block("oak_log"))
    at(5, 1, block("oak_log_top"))
    at(6, 1, block("iron_block"))
    at(7, 1, block("gold_block"))
    at(8, 1, block("diamond_block"))
    at(12, 1, block("red_mushroom"))                # mushroom data 0
    at(13, 1, block("brown_mushroom"))              # mushroom data 1

    # Row 2
    at(0, 2, block("gold_ore"))
    at(1, 2, block("iron_ore"))
    at(2, 2, block("coal_ore"))
    at(3, 2, block("bookshelf"))
    at(11, 2, block("crafting_table_top"))
    at(12, 2, block("furnace_front"))
    at(13, 2, block("furnace_side"))

    # Row 3
    at(0, 3, block("sponge"))
    at(1, 3, block("glass"))
    at(2, 3, block("diamond_ore"))
    at(3, 3, block("redstone_ore"))
    at(4, 3, block("oak_leaves"))                   # fancy ("transparent") leaves
    at(5, 3, fill_holes(block("oak_leaves"), TILE, TILE))  # fast leaves
    at(11, 3, block("crafting_table_side"))
    at(12, 3, block("crafting_table_front"))
    at(14, 3, block("furnace_top"))

    # Row 4 -- the chest, then the bed (see bedrenderer.cpp for the columns).
    chest_top, chest_side, chest_front = chest_tiles()
    at(0, 4, chest_top)
    at(1, 4, chest_side)
    at(2, 4, chest_front)

    bed_head, bed_frame, bed_side, bed_foot = bed_tiles()
    at(4, 4, bed_head)      # the blanket with the pillow pad
    at(5, 4, bed_frame)     # the underside
    at(6, 4, bed_side)      # the blanket turned over the frame
    at(7, 4, bed_foot)      # the blanket on its own

    # Row 5 -- the torch, the door's two halves and the wheat stages.
    at(0, 5, block("torch"))                        # torchrenderer.cpp
    at(1, 5, block("oak_door_top"))                 # doorrenderer.cpp
    for stage in range(8):
        at(8 + stage, 5, block("wheat_stage%d" % stage))

    # Row 6
    at(0, 6, block("lever"))                        # unpowered switch
    at(1, 6, block("oak_door_bottom"))
    at(3, 6, block("redstone_torch"))               # redtorchrenderer.cpp
    at(6, 6, block("pumpkin_top"))
    at(7, 6, block("netherrack"))
    at(9, 6, block("glowstone"))

    # Row 7
    at(1, 7, block("black_wool"))
    at(2, 7, block("gray_wool"))
    at(3, 7, block("redstone_torch_off"))
    at(5, 7, block("snow"))
    at(6, 7, block("pumpkin_side"))
    at(7, 7, block("carved_pumpkin"))               # vanilla's pumpkin face
    at(9, 7, block("cake_top"))
    at(10, 7, block("cake_side"))
    at(12, 7, block("cake_bottom"))

    # Rows 8-14 -- the pressure plates and the wools, then the lamp and lava.
    at(3, 8, block("stone"))                        # pressure plate, up
    at(3, 9, block("stone"))                        # pressure plate, pressed
    at(12, 8, tile(CAKE_ITEM))                      # the cake's inventory icon

    at(1, 8, block("red_wool"))
    at(2, 8, block("pink_wool"))
    at(1, 9, block("green_wool"))                   # BLOCK_WOOL_DARK_GREEN
    at(2, 9, block("lime_wool"))                    # BLOCK_WOOL_GREEN
    at(1, 10, block("brown_wool"))
    at(2, 10, block("yellow_wool"))
    at(4, 10, block("redstone_dust_dot"))           # wire; terrainInit tints it
    at(5, 10, block("redstone_dust_line0"))
    at(1, 11, block("blue_wool"))                   # BLOCK_WOOL_DARK_BLUE
    at(2, 11, block("light_blue_wool"))
    at(1, 12, block("purple_wool"))
    at(2, 12, block("magenta_wool"))
    at(6, 12, block("dark_oak_planks"))             # BLOCK_PLANKS_DARK
    at(7, 12, block("bookshelf"))                   # the bookshelf's sides
    at(13, 12, block("water_still", crop_box=(0, 0, 16, 16), tint_color=WATER))
    at(1, 13, block("cyan_wool"))
    at(2, 13, block("orange_wool"))
    at(3, 13, block("redstone_lamp"))
    at(4, 13, block("redstone_lamp_on"))
    at(6, 13, block("birch_planks"))                # BLOCK_PLANKS_BRIGHT
    at(1, 14, block("white_wool"))
    at(13, 14, block("lava_still", crop_box=(0, 0, 16, 16)))  # first animation frame

    # Row 15 -- the ten breaking-overlay frames.
    for frame in range(10):
        at(frame, 15, block("destroy_stage_%d" % frame))

    # Grey tints: grass top and both leaf tiles are tinted at load time, so the
    # tiles themselves stay as vanilla ships them. Nothing to do here.

    atlas = [(0, 0, 0, 0)] * (ATLAS * ATLAS)
    for x, y, px in tiles:
        paste(atlas, ATLAS, ATLAS, px, TILE, TILE, x * TILE, y * TILE)
    return atlas


def write_header(pixels):
    lines = [
        "// Generated from the Minecraft 1.17.1 block textures -- do not edit by hand.",
        "// Regenerate: python3 tools/textures/gen_block_textures.py",
        "COLOR texdata_terrain3[] = {",
    ]
    per_line = 16
    for i in range(0, len(pixels), per_line):
        chunk = pixels[i:i + per_line]
        sep = "," if i + per_line < len(pixels) else ""
        lines.append("    " + ", ".join("0x%04x" % v for v in chunk) + sep)
    lines += [
        "};",
        "",
        "static TEXTURE tex_terrain3 = {",
        "    .width = %d," % ATLAS,
        "    .height = %d," % ATLAS,
        "    .has_transparency = false,",
        "    .transparent_color = 0x0000,",
        "    .bitmap = texdata_terrain3",
        "};",
        "",
    ]
    with open(os.path.join("textures", "terrain3.h"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    os.chdir(root)

    atlas = build()
    write_png(os.path.join("textures", "terrain3.png"), ATLAS, ATLAS, atlas)
    write_header(to_rgb565(atlas))
    print("wrote textures/terrain3.png and textures/terrain3.h (%dx%d)" % (ATLAS, ATLAS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
