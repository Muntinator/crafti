#!/usr/bin/env python3
"""Build the GUI headers (textures/*.h) from the official Minecraft 1.17.1 GUI art.

The sources are the official GUI textures, checked in under textures/gui/ in the
vanilla asset tree's own layout:

    textures/gui/<name>.png              assets/minecraft/textures/gui/<name>.png
    textures/gui/container/<name>.png    .../gui/container/<name>.png
    textures/particle/flame.png          .../particle/flame.png

and each header below is one crop of one of those files, embedded as RGB565 for
nGL. Nothing is drawn here: the game's windows, hotbar, buttons and HUD are the
authentic sheets, cut to the piece the engine blits.

The crops are the vanilla widgets as vanilla itself samples them:

  * widgets.png holds the hotbar (0,0) 182x22 -- nine 16-pixel slots on a 20-pixel
    pitch starting at (3,3) -- the selected-slot frame (0,22) 24x24, the button in
    its three states (0,46) 200x60 (disabled, plain, highlighted, one 20-pixel row
    each), the slider handle (the 8-pixel knob at the left edge of the plain and
    highlighted button rows), and the title screen's icon buttons: the language
    button at (0,106) and the world-lock buttons at (0,146) and (20,146), each in
    plain and highlighted states (and greyed, for the locks) below that,
  * checkbox.png holds the options screen's checkbox in its four states, as a
    2x2 grid of 20x20 cells: unchecked/checked across, plain/highlighted down,
  * container/inventory.png holds the whole player window (0,0) 176x166: the four
    armour slots down the left at (8,8) on an 18-pixel pitch, the 2x2 crafting grid
    at (98,18), the result slot at (154,28), the 27 storage slots from (8,84) and
    the hotbar at (8,142),
  * container/crafting_table.png is the same window with a 3x3 grid at (30,17) and
    the result slot at (124,35),
  * container/furnace.png is the furnace window (0,0) 176x166 with the flame at
    (176,0) 14x13 and the smelting arrow at (176,14) 25x16 to its right, which is
    why the whole file is kept: the two sprites are read at the same coordinates
    vanilla reads them at,
  * container/generic_54.png holds a chest window: the six chest slot rows from
    (0,0), and the player's own inventory part at (0,126) 176x96, which is what a
    chest window blits below its own rows,
  * icons.png is the HUD sheet: hearts, armour, breath, hunger, the crosshair and
    the experience bar, all sampled at the vanilla 1.17 coordinates (hearts at y=0,
    armour y=9, breath y=18, hunger y=27, xp bar y=64),
  * options_background.png is the dark dirt tile every menu screen is tiled with,
  * particle/flame.png is the torch flame particle,
  * gui/title/minecraft.png holds the vanilla wordmark; the title screen's copy is
    stored as two halves stacked in the sheet -- vanilla blits (0,0,155,44) at the
    logo's left edge and (0,45,155,44) 155 pixels to its right -- which are joined
    here into the single 274x44 image the title screen draws (see COMPOSITES).
    gui/title/edition.png is the "Java Edition" strip vanilla draws under it.

The title screen's wordmark is the one piece of the front-end that is an image
rather than text, so the crop above is the vanilla logo itself, assembled from the
same two source rectangles vanilla blits side by side.

One more header is stitched from several files: armor_slots.h is the five
`item/empty_armor_slot_*.png` placeholders -- helmet, chestplate, leggings, boots
and the offhand shield -- laid side by side, which is the strip the player
inventory draws into its empty armour (and offhand) slots.

Two more headers do not come from a single file:

  * font_bmp.h / font_dat.h are the vanilla 8-pixel font: gui's `font/ascii.png`
    (a 16x16 grid of 8x8 cells, one per character of 0x00..0xFF), plus a table of
    per-glyph advances derived by scanning each cell for its rightmost opaque
    column -- the same rule vanilla uses, and the reason no hand-written width
    table can drift away from the atlas,
  * title_backdrop.h is one pre-rendered frame of the title screen's panorama.
    Vanilla renders six cubemap faces every frame; a calculator cannot hold six
    1024x1024 faces, let alone project them, so one face is cropped to the 4:3
    window the title screen has and scaled down to the screen size instead. The
    face is chosen automatically, by looking for the one whose horizon sits
    closest to the middle of the frame.

Run from the repository root:

    python3 tools/textures/gen_gui_textures.py
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pngio import read_png, to_rgb565

# (header, symbol, source, crop or None for the whole file)
PANELS = [
    # --- the HUD and its widgets -------------------------------------------
    ("icons",           "icons",           "gui/icons",                     None),
    ("inventory",       "inventory",       "gui/widgets",                   (0, 0, 182, 22)),
    ("inv_selection",   "inv_selection",   "gui/widgets",                   (0, 22, 24, 24)),
    ("menu_button",     "menu_button",     "gui/widgets",                   (0, 46, 200, 60)),
    # The options screen's two extra widgets: the slider's handle and the
    # checkbox. The handle is the 8-pixel knob vanilla cuts from the left edge of
    # the button rows (the plain and highlighted rows stacked into one 8x40
    # block), and the checkbox is its own 40x40 sheet holding the four states
    # vanilla can show -- unchecked/checked across, plain/highlighted down.
    ("slider_handle",   "slider_handle",   "gui/widgets",                   (0, 66, 8, 40)),
    ("checkbox",        "checkbox",        "gui/checkbox",                  (0, 0, 40, 40)),
    # The title screen's two icon buttons draw these icons over ordinary button
    # art. The language globe is the same glyph vanilla bakes into its language
    # button at widgets.png (0,106); the accessibility figure is vanilla's
    # gui/options/accessibility.png, the one icon widgets.png does not carry.
    ("language_icon",   "language_icon",   "gui/options/language",          (0, 0, 15, 15)),
    ("accessibility_icon", "accessibility_icon", "gui/options/accessibility", (0, 0, 15, 15)),
    ("menu_background", "menu_background", "gui/options_background",        None),
    # --- the title screen --------------------------------------------------
    ("edition",         "edition",         "gui/title/edition",             (0, 0, 98, 14)),
    # --- the windows -------------------------------------------------------
    ("inventory2",      "inventory2",      "gui/container/inventory",       (0, 0, 176, 166)),
    ("crafting_table",  "crafting_table",  "gui/container/crafting_table",  (0, 0, 176, 166)),
    ("furnace",         "furnace_gui",     "gui/container/furnace",         None),
    ("chest_top",       "chest_top",       "gui/container/generic_54",      (0, 0, 176, 125)),
    ("chest_player",    "chest_player",    "gui/container/generic_54",      (0, 126, 176, 96)),
    # The creative inventory: the 195x136 item-tab window (the slot grid, the
    # hotbar and the scrollbar track all baked in), the tab strip (seven 28x32
    # cells -- the selected row at y 32, the unselected at y 0 -- placed 29
    # pixels apart on screen), and the 12x15 scrollbar handle at the sheet's
    # far right.
    ("creative_window", "creative_window", "gui/container/creative_inventory/tab_items", (0, 0, 195, 136)),
    ("creative_tabs",   "creative_tabs",   "gui/container/creative_inventory/tabs",      (0, 0, 196, 64)),
    ("creative_scroll", "creative_scroll", "gui/container/creative_inventory/tabs",      (232, 0, 12, 15)),
    # --- world sprites that used to come from the old combined sheets -------
    ("part_fire",       "part_fire",       "particle/flame",                None),
]

# Panels assembled from more than one rectangle of one sheet, laid side by side.
# The title wordmark is stored as two halves stacked in minecraft.png: vanilla
# blits (0,0,155,44) at the logo's left edge and (0,45,155,44) 155 pixels to its
# right (its ink stops at x 118, so the right half contributes 119 columns). The
# two are joined here into the single 274x44 image the title screen draws at
# `width / 2 - 137`, which is exactly where vanilla's two blits land.
COMPOSITES = [
    ("title_logo",      "title_logo",      "gui/title/minecraft",
     [(0, 0, 155, 44), (0, 45, 119, 44)]),
]

# Panels stitched from several whole files, laid side by side in the order given.
# The armour placeholders are one 16x16 sprite each under `item/`; vanilla draws
# whichever one the empty slot wants into the player window (and the shield into
# the offhand slot), so they are gathered into the one strip the engine indexes by
# equipment slot: helmet, chestplate, leggings, boots, then the offhand shield.
STITCHES = [
    ("armor_slots",     "armor_slots",
     ["item/empty_armor_slot_helmet", "item/empty_armor_slot_chestplate",
      "item/empty_armor_slot_leggings", "item/empty_armor_slot_boots",
      "item/empty_armor_slot_shield"]),
]


def load(rel):
    return read_png(os.path.join("textures", rel + ".png"))


def crop(px, w, h, x, y, cw, ch):
    out = []
    for yy in range(y, y + ch):
        out.extend(px[yy * w + x:yy * w + x + cw])
    return out, cw, ch


def write_header(header, symbol, width, height, pixels):
    """`pixels` are RGBA; the header holds the RGB565 the engine samples."""
    words = to_rgb565(pixels)
    transparent = any(a < 128 for _r, _g, _b, a in pixels)

    lines = [
        "// Generated from the Minecraft 1.17.1 GUI textures -- do not edit by hand.",
        "// Source: textures/gui/. Regenerate: python3 tools/textures/gen_gui_textures.py",
        "static uint16_t %s_data[] = {" % symbol,
    ]
    per_line = 12
    for i in range(0, len(words), per_line):
        chunk = words[i:i + per_line]
        sep = "," if i + per_line < len(words) else ""
        lines.append("    " + ", ".join("0x%04x" % v for v in chunk) + sep)
    lines += [
        "};",
        "",
        "static TEXTURE %s{" % symbol,
        "    %d, %d," % (width, height),
        "    %s, 0," % ("true" if transparent else "false"),
        "    %s_data" % symbol,
        "};",
        "",
    ]
    with open(os.path.join("textures", header + ".h"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    return width, height, transparent


# The vanilla font's cell: ascii.png is a grid of square 8x8 glyphs, one per
# character, laid out row by row from the character at FONT_FIRST_CHAR. The
# engine samples it with the same division (see font.cpp), so the grid has to be
# square for the lookup to hit the right cell.
FONT_CELL = 8
FONT_FIRST_CHAR = 0
# A glyph that is blank (the space, the control codes) still advances the pen: 4
# pixels is what vanilla gives them.
FONT_BLANK_ADVANCE = 4


def glyph_advance(px, w, cx, cy):
    """How far the pen moves after the glyph at (cx, cy) -- vanilla's rule.

    The rightmost opaque column of the cell is found and two pixels are added: one
    to the ink's own width and one for the gap before the next glyph. A cell with
    no ink at all is a blank, which has a fixed width.
    """
    right = -1
    for y in range(FONT_CELL):
        for x in range(FONT_CELL):
            if px[(cy + y) * w + cx + x][3] >= 128:
                right = x
    return right + 2 if right >= 0 else FONT_BLANK_ADVANCE


def _metrics(cell, advances):
    """The bytes font.cpp reads: [8] cell width, [12] cell height, [16] first
    character and [17 + c] the advance of character c. The leading 16 bytes it does
    not read are kept so that the layout stays the one it was written for."""
    return [0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
            cell, 0x00, 0x00, 0x00,
            cell, 0x00, 0x00, 0x00,
            FONT_FIRST_CHAR] + advances


def double_pixels(px, w, h, factor):
    """The nearest-neighbour upscale the desktop half of the font uses."""
    out = []
    for y in range(h):
        row = []
        for x in range(w):
            row.extend([px[y * w + x]] * factor)
        for _ in range(factor):
            out.extend(row)
    return out


def write_font():
    """The glyph atlas and the metrics table, at both GUI scales.

    The calculator draws the vanilla font as it comes: an 8x8 cell. The desktop
    runs the same front-end at twice the scale in both directions, so its font is
    the doubled atlas and the doubled advances -- which is what vanilla does too,
    where the font is always 8 pixels and the GUI scale multiplies it.

    Both pairs define `font_bmp` and `font_dat`, so font.cpp includes exactly one
    of them, chosen by the build.
    """
    w, h, px = load("font/ascii")
    cols = w // FONT_CELL
    rows = h // FONT_CELL
    count = cols * rows

    advances = []
    for index in range(count):
        cx = (index % cols) * FONT_CELL
        cy = (index // cols) * FONT_CELL
        advances.append(glyph_advance(px, w, cx, cy))

    _write_u8_header("font_dat", "font_dat", _metrics(FONT_CELL, advances))
    write_header("font_bmp", "font_bmp", w, h, px)

    wide = double_pixels(px, w, h, 2)
    _write_u8_header("font_dat_wide", "font_dat",
                     _metrics(FONT_CELL * 2, [a * 2 for a in advances]))
    write_header("font_bmp_wide", "font_bmp", w * 2, h * 2, wide)

    print("wrote textures/font_bmp.h, font_dat.h (%dx%d cell) and font_bmp_wide.h, "
          "font_dat_wide.h (%dx%d cell), %d glyphs %d..%d"
          % (FONT_CELL, FONT_CELL, FONT_CELL * 2, FONT_CELL * 2,
             count, FONT_FIRST_CHAR, FONT_FIRST_CHAR + count - 1))


def _write_u8_header(header, symbol, values):
    """A byte array that is not a texture -- today only the font's metrics."""
    lines = [
        "// Generated from the Minecraft 1.17.1 font -- do not edit by hand.",
        "// Source: textures/font/. Regenerate: python3 tools/textures/gen_gui_textures.py",
        "unsigned char %s[] = {" % symbol,
    ]
    per_line = 12
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        lines.append("    " + ", ".join("0x%02x" % (v & 0xFF) for v in chunk)
                     + ("," if i + per_line < len(values) else ""))
    lines += ["};", ""]
    with open(os.path.join("textures", header + ".h"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


# One panorama face is a 90-degree view; the title screen wants 4:3, so three
# quarters of the face's height is kept and the rest cropped away. The face and
# the crop are chosen so that the horizon lands near the middle.
BACKDROP_W = 320
BACKDROP_H = 240
BACKDROP_GAIN = 2


def row_luminance(px, w, y, x0, x1):
    total = 0
    for x in range(x0, x1):
        r, g, b, _a = px[y * w + x]
        total += (r * 3 + g * 6 + b) // 10
    return total / (x1 - x0)


def best_face_crop():
    """Pick the panorama face and the 4:3 window through it that reads best.

    The score is how close the sky-to-ground transition is to the middle of the
    window, weighted by how strong that transition is: a face that is all sky, or
    all ground, has no horizon and loses to one that has a landscape in it.
    """
    crop_h = BACKDROP_W * 3 // 4
    best = None
    for index in range(6):
        w, h, px = load("gui/title/background/panorama_%d" % index)
        profile = [row_luminance(px, w, y, w // 4, w * 3 // 4) for y in range(h)]
        # The horizon is the row the brightness falls off fastest across.
        horizon = max(range(1, h - 1), key=lambda y: profile[y - 1] - profile[y + 1])
        # 1.17's panorama is a dark scene, so the brightest tenth of it is a better
        # measure of how much is actually visible than the single brightest row.
        brightest = sorted(profile, reverse=True)[:max(1, h // 10)]
        contrast = sum(brightest) / len(brightest) - min(profile)
        top = min(max(horizon - crop_h // 2, 0), h - crop_h)
        # Distance from the middle of the window, in percent of the window height.
        off = abs((horizon - top) - crop_h // 2) * 100 // crop_h
        score = contrast - off * 4
        if best is None or score > best[0]:
            best = (score, index, px, w, h, top)
    return best[1:]


def write_backdrop():
    index, px, w, h, top = best_face_crop()
    crop_h = BACKDROP_W * 3 // 4

    # Box-average the window down to the screen: the panorama is a rendered image,
    # not pixel art, so a nearest-neighbour shrink would just alias it.
    out = []
    for dy in range(BACKDROP_H):
        y0 = top + dy * crop_h // BACKDROP_H
        y1 = max(y0 + 1, top + (dy + 1) * crop_h // BACKDROP_H)
        for dx in range(BACKDROP_W):
            x0 = dx * w // BACKDROP_W
            x1 = max(x0 + 1, (dx + 1) * w // BACKDROP_W)
            r = g = b = n = 0
            for y in range(y0, y1):
                for x in range(x0, x1):
                    pr, pg, pb, pa = px[y * w + x]
                    if pa < 128:
                        continue
                    r += pr
                    g += pg
                    b += pb
                    n += 1
            if n == 0:
                out.append((0, 0, 0, 0))
            else:
                # The panorama is a dim scene (its median pixel is around 40/255)
                # and the calculator's screen is small and low-contrast, so the
                # window is brightened into a readable range before it is saved.
                # Overlay() is what vanilla uses to tint this backdrop; nGL has no
                # blending, so the adjustment has to be baked in here instead.
                out.append((min(255, r * BACKDROP_GAIN // n),
                            min(255, g * BACKDROP_GAIN // n),
                            min(255, b * BACKDROP_GAIN // n), 255))

    write_header("title_backdrop", "title_backdrop", BACKDROP_W, BACKDROP_H, out)
    print("wrote textures/title_backdrop.h  (panorama_%d, top %d, %dx%d)"
          % (index, top, BACKDROP_W, BACKDROP_H))


# The world list's icon and its hover glyph. Vanilla draws every world's own
# `icon.png` at 32x32 and falls back to `misc/unknown_server.png` when a world
# has none (WorldSelectionList.ICON_MISSING); this engine saves no world icons,
# so the fallback is the icon every entry shows. The 128x128 art is box-averaged
# down to its 32x32 here rather than squeezed on every frame. Beside it,
# `gui/world_selection.png` holds the overlay vanilla lays over a hovered row's
# icon -- the "join" tile in its two states (row 0 while the row is merely
# hovered, row 32 while the pointer is on the icon itself).
def write_world_assets():
    w, h, px = load("misc/unknown_server")
    out = []
    for dy in range(32):
        for dx in range(32):
            y0, y1 = dy * h // 32, (dy + 1) * h // 32
            x0, x1 = dx * w // 32, (dx + 1) * w // 32
            r = g = b = 0
            for y in range(y0, y1):
                for x in range(x0, x1):
                    pr, pg, pb, _pa = px[y * w + x]
                    r += pr
                    g += pg
                    b += pb
            n = (y1 - y0) * (x1 - x0)
            out.append((r // n, g // n, b // n, 255))
    write_header("world_icon", "world_icon", 32, 32, out)
    print("wrote textures/world_icon.h  (world_icon 32x32, from misc/unknown_server)")

    w, h, px = load("gui/world_selection")
    px, w, h = crop(px, w, h, 0, 0, 32, 64)
    out_w, out_h, transparent = write_header("world_icon_overlay", "world_icon_overlay", w, h, px)
    print("wrote textures/world_icon_overlay.h  (world_icon_overlay %dx%d%s)"
          % (out_w, out_h, ", transparent" if transparent else ""))


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    os.chdir(root)

    for header, symbol, source, box in PANELS:
        w, h, px = load(source)
        if box is not None:
            if box[0] + box[2] > w or box[1] + box[3] > h:
                print("crop %s out of %s bounds (%dx%d)" % (str(box), source, w, h))
                return 1
            px, w, h = crop(px, w, h, *box)
        out_w, out_h, transparent = write_header(header, symbol, w, h, px)
        print("wrote textures/%s.h  (%s %dx%d%s)"
              % (header, symbol, out_w, out_h, ", transparent" if transparent else ""))

    for header, symbol, source, parts in COMPOSITES:
        w, h, px = load(source)
        out_w = sum(part[2] for part in parts)
        out_h = max(part[3] for part in parts)
        out = [(0, 0, 0, 0)] * (out_w * out_h)
        x = 0
        for (px0, py0, pw, ph) in parts:
            if px0 + pw > w or py0 + ph > h:
                print("crop %s out of %s bounds (%dx%d)" % (str((px0, py0, pw, ph)), source, w, h))
                return 1
            for yy in range(ph):
                for xx in range(pw):
                    out[yy * out_w + x + xx] = px[(py0 + yy) * w + px0 + xx]
            x += pw
        out_w, out_h, transparent = write_header(header, symbol, out_w, out_h, out)
        print("wrote textures/%s.h  (%s %dx%d%s, composed)"
              % (header, symbol, out_w, out_h, ", transparent" if transparent else ""))

    for header, symbol, sources in STITCHES:
        tiles = [load(rel) for rel in sources]
        out_w = sum(tile[0] for tile in tiles)
        out_h = max(tile[1] for tile in tiles)
        out = [(0, 0, 0, 0)] * (out_w * out_h)
        x = 0
        for (tw, th, tpx) in tiles:
            for yy in range(th):
                for xx in range(tw):
                    out[yy * out_w + x + xx] = tpx[yy * tw + xx]
            x += tw
        out_w, out_h, transparent = write_header(header, symbol, out_w, out_h, out)
        print("wrote textures/%s.h  (%s %dx%d%s, stitched)"
              % (header, symbol, out_w, out_h, ", transparent" if transparent else ""))

    write_world_assets()
    write_font()
    write_backdrop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
