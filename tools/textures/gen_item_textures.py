#!/usr/bin/env python3
"""Build the item atlas (textures/items_texture.png + textures/items_texture.h).

The art is the official Minecraft 1.17.1 texture set, one file per item:

    textures/item/<name>.png   assets/minecraft/textures/item/<name>.png

kept under the vanilla names so what is checked in is the authentic file. This
script only *arranges* them.

The layout is not free: it is the `ItemTexture` enum in textures/items.h. Every
identifier in that enum is a byte value, and itemicons.cpp samples the atlas at

    (x, y) = (index % 16, index / 16)

so index 22 must be at (6, 1) and nowhere else. This script therefore reads the
enum out of items.h and places each tile at the coordinate its value implies; a
name with no source below, or two names landing on the same tile, is an error
rather than a silently wrong icon. The enum is the specification and the atlas
follows it.

Two items need a source other than their own icon:

  * the bed, which vanilla draws as a 3D model and so has no 2D icon -- one is
    composed here from the `red.png` model unwrap the block atlas also uses,
  * compass and clock, which vanilla animates over 32/64 frames and this engine
    draws as a single still, so the first frame is used.

Run from the repository root:

    python3 tools/textures/gen_item_textures.py
"""

from __future__ import annotations

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pngio import read_png, to_rgb565, write_png

GRID = 16               # tiles per row; fixed by itemicons.cpp
TILE = 16               # pixels per tile
COLS = 16
ROWS = 16               # 256px tall, as the old hand-drawn atlas was
ATLAS = COLS * TILE     # 256

BED = "entity/bed/red.png"

# name -> file under textures/, without the extension. Every `ItemTexture` in
# items.h must appear here exactly once; the enum order decides the layout, so
# nothing in this table is positional.
SOURCES = {
    # Row 0
    "LEATHER_HELMET": "item/leather_helmet",
    "CHAINMAIL_HELMET": "item/chainmail_helmet",
    "IRON_HELMET": "item/iron_helmet",
    "DIAMOND_HELMET": "item/diamond_helmet",
    "GOLDEN_HELMET": "item/golden_helmet",
    "FLINT_AND_STEEL": "item/flint_and_steel",
    "COAL": "item/coal",
    "CHARCOAL": "item/charcoal",
    "STRING": "item/string",
    "WHEAT_SEEDS": "item/wheat_seeds",
    "APPLE": "item/apple",
    "GOLDEN_APPLE": "item/golden_apple",
    "EGG": "item/egg",
    "SUGAR": "item/sugar",
    "SNOWBALL": "item/snowball",
    "ELYTRA": "item/elytra",

    # Row 1
    "LEATHER_CHESTPLATE": "item/leather_chestplate",
    "CHAINMAIL_CHESTPLATE": "item/chainmail_chestplate",
    "IRON_CHESTPLATE": "item/iron_chestplate",
    "DIAMOND_CHESTPLATE": "item/diamond_chestplate",
    "GOLDEN_CHESTPLATE": "item/golden_chestplate",
    "COD": "item/cod",
    "SALMON": "item/salmon",
    "TROPICAL_FISH": "item/tropical_fish",
    "PUFFERFISH": "item/pufferfish",
    "SEAGRASS": "item/seagrass",
    "PRISMARINE_CRYSTALS": "item/prismarine_crystals",
    "AMETHYST_SHARD": "item/amethyst_shard",
    "BAMBOO": "item/bamboo",
    "KELP": "item/kelp",
    "DRIED_KELP": "item/dried_kelp",
    "ACACIA_SIGN": "item/acacia_sign",

    # Row 2
    "LEATHER_LEGGINGS": "item/leather_leggings",
    "CHAINMAIL_LEGGINGS": "item/chainmail_leggings",
    "IRON_LEGGINGS": "item/iron_leggings",
    "DIAMOND_LEGGINGS": "item/diamond_leggings",
    "GOLDEN_LEGGINGS": "item/golden_leggings",
    "ARROW": "item/arrow",
    "END_CRYSTAL": "item/end_crystal",
    "GOLD_INGOT": "item/gold_ingot",
    "GUNPOWDER": "item/gunpowder",
    "BREAD": "item/bread",
    "OAK_SIGN": "item/oak_sign",
    "OAK_DOOR": "item/oak_door",
    "IRON_DOOR": "item/iron_door",
    "BED": None,                 # composed below
    "ITEM_FRAME": "item/item_frame",
    "CHORUS_FRUIT": "item/chorus_fruit",

    # Row 3
    "LEATHER_BOOTS": "item/leather_boots",
    "CHAINMAIL_BOOTS": "item/chainmail_boots",
    "IRON_BOOTS": "item/iron_boots",
    "DIAMOND_BOOTS": "item/diamond_boots",
    "GOLDEN_BOOTS": "item/golden_boots",
    "STICK": "item/stick",
    "COMPASS": "item/compass_00",       # frame 0 of vanilla's 32-frame spin
    "DIAMOND": "item/diamond",
    "REDSTONE_DUST": "item/redstone",
    "CLAY_BALL": "item/clay_ball",
    "PAPER": "item/paper",
    "BOOK": "item/book",
    "MAP": "item/map",
    "MELON_SEEDS": "item/melon_seeds",
    "PUMPKIN_SEEDS": "item/pumpkin_seeds",
    "POPPED_CHORUS_FRUIT": "item/popped_chorus_fruit",

    # Row 4
    "WOODEN_SWORD": "item/wooden_sword",
    "STONE_SWORD": "item/stone_sword",
    "IRON_SWORD": "item/iron_sword",
    "DIAMOND_SWORD": "item/diamond_sword",
    "GOLDEN_SWORD": "item/golden_sword",
    "FISHING_ROD_CAST": "item/fishing_rod_cast",
    "CLOCK": "item/clock_00",           # frame 0 of vanilla's 64-frame dial
    "BOWL": "item/bowl",
    "MUSHROOM_STEW": "item/mushroom_stew",
    "GLOWSTONE_DUST": "item/glowstone_dust",
    "BUCKET": "item/bucket",
    "WATER_BUCKET": "item/water_bucket",
    "LAVA_BUCKET": "item/lava_bucket",
    "MILK_BUCKET": "item/milk_bucket",
    "INK_SAC": "item/ink_sac",
    "GRAY_DYE": "item/gray_dye",

    # Row 5
    "WOODEN_SHOVEL": "item/wooden_shovel",
    "STONE_SHOVEL": "item/stone_shovel",
    "IRON_SHOVEL": "item/iron_shovel",
    "DIAMOND_SHOVEL": "item/diamond_shovel",
    "GOLDEN_SHOVEL": "item/golden_shovel",
    "FISHING_ROD": "item/fishing_rod",
    "REDSTONE_REPEATER": "item/repeater",
    "RAW_PORKCHOP": "item/porkchop",
    "COOKED_PORKCHOP": "item/cooked_porkchop",
    "RAW_COD": "item/cod",
    "COOKED_COD": "item/cooked_cod",
    "RAW_SALMON": "item/salmon",
    "COOKIE": "item/cookie",
    "SHEARS": "item/shears",
    "ROSE_RED": "item/red_dye",         # 1.14 renamed rose red to red dye
    "PINK_DYE": "item/pink_dye",

    # Row 6
    "WOODEN_PICKAXE": "item/wooden_pickaxe",
    "STONE_PICKAXE": "item/stone_pickaxe",
    "IRON_PICKAXE": "item/iron_pickaxe",
    "DIAMOND_PICKAXE": "item/diamond_pickaxe",
    "GOLDEN_PICKAXE": "item/golden_pickaxe",
    "BOW_PULLING_1": "item/bow_pulling_0",
    "CARROT_ON_A_STICK": "item/carrot_on_a_stick",
    "LEATHER": "item/leather",
    "SADDLE": "item/saddle",
    "RAW_BEEF": "item/beef",
    "COOKED_BEEF": "item/cooked_beef",
    "ENDER_PEARL": "item/ender_pearl",
    "BLAZE_ROD": "item/blaze_rod",
    "GLISTERING_MELON_SLICE": "item/glistering_melon_slice",
    "CACTUS_GREEN": "item/green_dye",   # 1.14 renamed cactus green to green dye
    "LIME_DYE": "item/lime_dye",

    # Row 7
    "WOODEN_AXE": "item/wooden_axe",
    "STONE_AXE": "item/stone_axe",
    "IRON_AXE": "item/iron_axe",
    "DIAMOND_AXE": "item/diamond_axe",
    "GOLDEN_AXE": "item/golden_axe",
    "BOW_PULLING_2": "item/bow_pulling_1",
    "BAKED_POTATO": "item/baked_potato",
    "POTATO": "item/potato",
    "CARROT": "item/carrot",
    "RAW_CHICKEN": "item/chicken",
    "COOKED_CHICKEN": "item/cooked_chicken",
    "GHAST_TEAR": "item/ghast_tear",
    "GOLD_NUGGET": "item/gold_nugget",
    "NETHER_WART": "item/nether_wart",
    "COCOA_BEANS": "item/cocoa_beans",
    "DANDELION_YELLOW": "item/yellow_dye",  # 1.14 renamed dandelion yellow

    # Row 8
    "WOODEN_HOE": "item/wooden_hoe",
    "STONE_HOE": "item/stone_hoe",
    "IRON_HOE": "item/iron_hoe",
    "DIAMOND_HOE": "item/diamond_hoe",
    "GOLDEN_HOE": "item/golden_hoe",
    "BOW_PULLING_3": "item/bow_pulling_2",
    "POISONOUS_POTATO": "item/poisonous_potato",
    "MINECART": "item/minecart",
    "OAK_BOAT": "item/oak_boat",
    "MELON_SLICE": "item/melon_slice",
    "FERMENTED_SPIDER_EYE": "item/fermented_spider_eye",
    "SPIDER_EYE": "item/spider_eye",
    "GLASS_BOTTLE": "item/glass_bottle",
    "ROTTEN_FLESH": "item/rotten_flesh",
    "LAPIS_LAZULI": "item/lapis_lazuli",
    "LIGHT_BLUE_DYE": "item/light_blue_dye",

    # Row 9
    "ARMOR_STAND": "item/armor_stand",
    "IRON_INGOT": "item/iron_ingot",
    "IRON_HORSE_ARMOR": "item/iron_horse_armor",
    "DIAMOND_HORSE_ARMOR": "item/diamond_horse_armor",
    "GOLDEN_HORSE_ARMOR": "item/golden_horse_armor",
    "REDSTONE_COMPARATOR": "item/comparator",
    "GOLDEN_CARROT": "item/golden_carrot",
    "CHEST_MINECART": "item/chest_minecart",
    "PUMPKIN_PIE": "item/pumpkin_pie",
    "SPAWN_EGG": "item/spawn_egg",
    "RABBIT_FOOT": "item/rabbit_foot",
    "ENDER_EYE": "item/ender_eye",
    "CAULDRON": "item/cauldron",
    "BLAZE_POWDER": "item/blaze_powder",
    "PURPLE_DYE": "item/purple_dye",
    "MAGENTA_DYE": "item/magenta_dye",

    # Row 10
    "CYAN_DYE": "item/cyan_dye",
    "BLUE_DYE": "item/blue_dye",
    "WHITE_DYE": "item/white_dye",
    "BLACK_DYE": "item/black_dye",
    "BROWN_DYE": "item/brown_dye",
    "GREEN_DYE": "item/green_dye",
    "RED_DYE": "item/red_dye",
    "ORANGE_DYE": "item/orange_dye",
    "YELLOW_DYE": "item/yellow_dye",
    # The *_ALT entries are the same dyes under a second id; 1.14 merged the old
    # per-name colours into a single dye each, so both ids draw the same file.
    "LIME_DYE_ALT": "item/lime_dye",
    "LIGHT_GRAY_DYE": "item/light_gray_dye",
    "CYAN_DYE_ALT": "item/cyan_dye",
    "PURPLE_DYE_ALT": "item/purple_dye",
    "BLUE_DYE_ALT": "item/blue_dye",
    "BROWN_DYE_ALT": "item/brown_dye",
    "BONE_MEAL": "item/bone_meal",

    # Row 11
    "COOKED_SALMON": "item/cooked_salmon",
}

ENUM_RE = re.compile(r"^\s*([A-Z][A-Z0-9_]*)\s*=\s*(\d+)\s*,", re.MULTILINE)


# --- small pixel helpers -----------------------------------------------------

def load(rel):
    return read_png(os.path.join("textures", rel))


def crop(px, w, h, x, y, cw, ch):
    out = []
    for yy in range(y, y + ch):
        out.extend(px[yy * w + x:yy * w + x + cw])
    return out, cw, ch


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


def tile(rel, size=(TILE, TILE)):
    """One atlas tile: the whole texture, rescaled to 16x16 if it is not already."""
    w, h, px = load(rel + ".png")
    if (w, h) != size:
        px = resize(px, w, h, size[0], size[1])
    return px


def bed_icon():
    """The bed's item icon, composed from the red bed model's own unwrap.

    Vanilla's bed item is a 3D model and has no 2D icon, so there is nothing to
    copy. The unwrap holds both faces the icon needs -- the blanket (with its
    pillow) at (0,0) 28x22 and the plank frame at (28,6) 16x16 -- and stacking a
    strip of the frame under a strip of the blanket gives the same
    blanket-on-a-wooden-base read the model has.
    """
    _w, _h, px = load(BED)
    blanket = resize(*crop(px, 64, 64, 0, 0, 28, 22), 12, 7)
    frame = resize(*crop(px, 64, 64, 28, 6, TILE, TILE), 12, 4)

    icon = [(0, 0, 0, 0)] * (TILE * TILE)
    paste(icon, TILE, TILE, blanket, 12, 7, 2, 3)
    paste(icon, TILE, TILE, frame, 12, 4, 2, 10)
    return icon


# --- the atlas ---------------------------------------------------------------

def enum_index():
    """The `ItemTexture` enum of items.h as {value: name}, in header order."""
    with open(os.path.join("textures", "items.h"), encoding="utf-8") as f:
        text = f.read()

    header = text[text.index("enum class ItemTexture"):text.index("};")]
    values = {}
    for name, value in ENUM_RE.findall(header):
        value = int(value)
        if value in values:
            raise SystemExit("items.h: id %d is used by both %s and %s"
                             % (value, values[value], name))
        values[value] = name
    return values


def build():
    """Return the whole atlas as one 256x256 RGBA image."""
    values = enum_index()
    missing = sorted(set(values.values()) - set(SOURCES))
    extra = sorted(set(SOURCES) - set(values.values()))
    if missing:
        raise SystemExit("no official texture mapped for: %s" % ", ".join(missing))
    if extra:
        raise SystemExit("items.h no longer has: %s" % ", ".join(extra))

    atlas = [(0, 0, 0, 0)] * (ATLAS * ATLAS)
    for index in sorted(values):
        name = values[index]
        source = SOURCES[name]
        px = bed_icon() if source is None else tile(source)
        assert len(px) == TILE * TILE, "%s is not a 16x16 tile" % name
        x = (index % GRID) * TILE
        y = (index // GRID) * TILE
        paste(atlas, ATLAS, ATLAS, px, TILE, TILE, x, y)
    return atlas


def write_header(pixels):
    lines = [
        "// Generated from the Minecraft 1.17.1 item textures -- do not edit by hand.",
        "// Regenerate: python3 tools/textures/gen_item_textures.py",
        "COLOR texdata_items[] = {",
    ]
    per_line = 16
    for i in range(0, len(pixels), per_line):
        chunk = pixels[i:i + per_line]
        sep = "," if i + per_line < len(pixels) else ""
        lines.append("    " + ", ".join("0x%04x" % v for v in chunk) + sep)
    lines += [
        "};",
        "",
        "static const TEXTURE tex_items = {",
        "    .width = %d," % ATLAS,
        "    .height = %d," % ATLAS,
        "    .has_transparency = true,",
        "    .transparent_color = 0x0000,",
        "    .bitmap = texdata_items",
        "};",
        "",
    ]
    with open(os.path.join("textures", "items_texture.h"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    os.chdir(root)

    atlas = build()
    write_png(os.path.join("textures", "items_texture.png"), ATLAS, ATLAS, atlas)
    write_header(to_rgb565(atlas))
    print("wrote textures/items_texture.png and textures/items_texture.h (%dx%d)"
          % (ATLAS, ATLAS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
