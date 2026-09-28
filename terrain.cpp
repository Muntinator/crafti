#include "terrain.h"

#include <libndls.h>

#include "textures/terrain.h"
#include "textures/terrain2.h"
#include "textures/terrain3.h"
#include "textures/inv_selection.h"

const char *block_names[BLOCK_NORMAL_LAST + 1] =
{
    "Air",
    "Stone",
    "Dirt",
    "Sand",
    "Wood",
    "Leaves",
    "Normal Planks",
    "Wall",
    "Coal Ore",
    "Gold Ore",
    "Iron Ore",
    "Diamond Ore",
    "Redstone Ore",
    "TNT",
    "Sponge",
    "Dark Planks",
    "Bright Planks",
    "Furnace",
    "Crafting Table",
    "Bookshelf",
    "Grass",
    "Pumpkin",
    "Bedrock",
    "Glass",
    "Cobblestone",
    "Glowstone",
    "Iron block",
    "Gold block",
    "Diamond block",
    "Netherrack",
    "Black Wool",
    "Red Wool",
    "Dark Green Wool",
    "Brown Wool",
    "Dark Blue Wool",
    "Purple Wool",
    "Cyan Wool",
    "White Wool",
    "Gray Wool",
    "Pink Wool",
    "Green Wool",
    "Yellow Wool",
    "Light Blue Wool",
    "Magenta Wool",
    "Orange Wool",
    "Chest",
    "Bed",
    "Snow Layer"
};

struct BLOCK_TEXTURE {
    BLOCK block;
    BLOCK_SIDE_BITFIELD sides;
};

#define NON {BLOCK_AIR, 0}
#define ALL(block) {block, BLOCK_TOP_BIT | BLOCK_BOTTOM_BIT | BLOCK_LEFT_BIT | BLOCK_RIGHT_BIT | BLOCK_FRONT_BIT | BLOCK_BACK_BIT}
#define TOP(block) {block, BLOCK_TOP_BIT}
#define BOT(block) {block, BLOCK_BOTTOM_BIT}
#define LEF(block) {block, BLOCK_LEFT_BIT}
#define RIG(block) {block, BLOCK_RIGHT_BIT}
#define FRO(block) {block, BLOCK_FRONT_BIT}
#define BAC(block) {block, BLOCK_BACK_BIT}
#define TAB(block) {block, BLOCK_TOP_BIT | BLOCK_BOTTOM_BIT}
#define SID(block) {block, BLOCK_FRONT_BIT | BLOCK_BACK_BIT | BLOCK_LEFT_BIT | BLOCK_RIGHT_BIT}
#define SWF(block) {block, BLOCK_BACK_BIT | BLOCK_LEFT_BIT | BLOCK_RIGHT_BIT}
#define AWF(block) {block, BLOCK_TOP_BIT | BLOCK_BOTTOM_BIT | BLOCK_LEFT_BIT | BLOCK_RIGHT_BIT | BLOCK_BACK_BIT}

//Maps location in texture atlas to block ID
static const BLOCK_TEXTURE texture_atlas[][16] =
{
    { TOP(BLOCK_GRASS), ALL(BLOCK_STONE), ALL(BLOCK_DIRT), SID(BLOCK_GRASS), ALL(BLOCK_PLANKS_NORMAL), NON, NON, ALL(BLOCK_WALL), ALL(BLOCK_TNT), TOP(BLOCK_TNT), BOT(BLOCK_TNT), NON, NON, NON, NON, NON },
    { ALL(BLOCK_COBBLESTONE), ALL(BLOCK_BEDROCK), ALL(BLOCK_SAND), NON, SID(BLOCK_WOOD), TAB(BLOCK_WOOD), ALL(BLOCK_IRON), ALL(BLOCK_GOLD), ALL(BLOCK_DIAMOND), NON, NON, NON, NON, NON, NON, NON },
    { ALL(BLOCK_GOLD_ORE), ALL(BLOCK_IRON_ORE), ALL(BLOCK_COAL_ORE), FRO(BLOCK_BOOKSHELF), NON, NON, NON, NON, NON, NON, NON, TAB(BLOCK_CRAFTING_TABLE), FRO(BLOCK_FURNACE), SWF(BLOCK_FURNACE), NON, NON },
    { ALL(BLOCK_SPONGE), ALL(BLOCK_GLASS), ALL(BLOCK_DIAMOND_ORE), ALL(BLOCK_REDSTONE_ORE), NON, ALL(BLOCK_LEAVES), NON, NON, NON, NON, NON, SID(BLOCK_CRAFTING_TABLE), FRO(BLOCK_CRAFTING_TABLE), NON, TOP(BLOCK_FURNACE), NON },
    // Row 4 was unused; the chest is painted there by paintChestTexture().
    { TAB(BLOCK_CHEST), SID(BLOCK_CHEST), FRO(BLOCK_CHEST), NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON },
    // Row 5 is the bed, painted by paintBedTexture(). Only three of its four
    // tiles are mapped here: the plain blanket top (5,3) is used by the renderer
    // for the foot end of the bed and is never the whole block's face, so giving
    // it a block side would only make the item icon ambiguous.
    { TOP(BLOCK_BED), BOT(BLOCK_BED), SID(BLOCK_BED), NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON },
    { NON, NON, NON, NON, NON, NON, TAB(BLOCK_PUMPKIN), ALL(BLOCK_NETHERRACK), NON, ALL(BLOCK_GLOWSTONE), NON, NON, NON, NON, NON, NON},
    // (5,7) is the packs' own light grey gravel-like tile, unused until now: the
    // snow layer is drawn with it rather than with a new texture, so all three
    // texture packs get snow without a byte of art being authored.
    { NON, ALL(BLOCK_WOOL_BLACK), ALL(BLOCK_WOOL_GRAY), NON, NON, ALL(BLOCK_SNOW), SWF(BLOCK_PUMPKIN), FRO(BLOCK_PUMPKIN), NON, NON, NON, NON, NON, NON, NON, NON },
    { NON, ALL(BLOCK_WOOL_RED), ALL(BLOCK_WOOL_PINK), NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON },
    { NON, ALL(BLOCK_WOOL_DARK_GREEN), ALL(BLOCK_WOOL_GREEN), NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON },
    { NON, ALL(BLOCK_WOOL_BROWN), ALL(BLOCK_WOOL_YELLOW), NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON },
    { NON, ALL(BLOCK_WOOL_DARK_BLUE), ALL(BLOCK_WOOL_LIGHT_BLUE), NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON },
    { NON, ALL(BLOCK_WOOL_PURPLE), ALL(BLOCK_WOOL_MAGENTA), NON, NON, NON, ALL(BLOCK_PLANKS_DARK), AWF(BLOCK_BOOKSHELF), NON, NON, NON, NON, NON, NON, NON, NON },
    { NON, ALL(BLOCK_WOOL_CYAN), ALL(BLOCK_WOOL_ORANGE), NON, NON, NON, ALL(BLOCK_PLANKS_BRIGHT), NON, NON, NON, NON, NON, NON, NON, NON, NON },
    { NON, ALL(BLOCK_WOOL_WHITE), NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON },
    { NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON }
};

static const struct { int x, y; } special_block_texture_idx[BLOCK_SPECIAL_LAST - BLOCK_SPECIAL_START + 1] =
{
    {4, 0}, // Torch -> Planks
    {4, 3}, // Flower -> Leaves
    {11, 0}, // Spiderweb -> Spiderweb
    {9, 7}, // Cake -> Cake
    {14, 8}, // Mushroom -> Mushroom block
    {1, 6}, // Door -> Door bottom
    {13, 12}, // Water -> Water
    {13, 14}, // Lava -> Lava
    {15, 5}, // Wheat -> Wheat
    {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, // Gap...
    {3, 13}, // Redstone Lamp -> Lamp (off)
    {1, 0}, // Redstone Switch -> Stone
    {4, 10}, // Redstone Wire -> Redstone Wire
    {4, 0}, // Redstone Torch -> Planks
    {1, 0}, // Pressure Plate -> Stone
};

TerrainAtlasEntry block_textures[BLOCK_NORMAL_LAST + 1][BLOCK_SIDE_LAST + 1];
TerrainQuadEntry quad_block_textures[BLOCK_NORMAL_LAST + 1][BLOCK_SIDE_LAST + 1], dual_block_textures[2][BLOCK_NORMAL_LAST + 1][BLOCK_SIDE_LAST + 1];
TerrainAtlasEntry terrain_atlas[16][16];
TerrainAtlasEntry special_block_textures[BLOCK_SPECIAL_LAST - BLOCK_SPECIAL_START + 1];

TEXTURE *terrain_current, *terrain_resized, *terrain_quad, *inv_selection_p, *door_preview;

//Some textures have a different color in different biomes. We have to make them green. Grey grass just looks so unhealty
static void makeColor(const RGB &color, TEXTURE &texture, const int x, const int y, const int w, const int h)
{
    for(int x1 = x; x1 < w + x; x1++)
        for(int y1 = y; y1 < h + y; y1++)
        {
            RGB grey = rgbColor(texture.bitmap[x1 + y1*texture.width]);
            grey.r *= color.r;
            grey.g *= color.g;
            grey.b *= color.b;
            texture.bitmap[x1 + y1*texture.width] = colorRGB(grey);
        }
}

static void makeWaterOpaque(TEXTURE &texture, const int x, const int y, const int w, const int h)
{
    for(int x1 = x; x1 < w + x; x1++)
        for(int y1 = y; y1 < h + y; y1++)
        {
            RGB color = rgbColor(texture.bitmap[x1 + y1*texture.width]);
            // Taking the (non-remultiplied) color directly looks too bright
            color.r = (color.r * 3) / 4;
            color.g = (color.g * 3) / 4;
            color.b = (color.b * 3) / 4;
            texture.bitmap[x1 + y1*texture.width] = colorRGB(color);
        }
}

/**
 * Draws the chest into one 16x16 tile of the atlas.
 *
 * The chest is the only block with no art of its own: the atlas ships in three
 * variants (an external 512x512 file and two embedded 256x256 ones), so instead
 * of adding a texture to each the tile is painted here, in the atlas the runtime
 * actually loaded. Row 4 of the atlas was unused, which is where these go.
 *
 * `variant` is 0 for the top, 1 for a plain side and 2 for the front, which
 * carries the latch.
 */
static void paintChestTile(TEXTURE &texture, const int x, const int y, const int w, const int h, const int variant)
{
    static const RGB wood      = { 0.62f, 0.40f, 0.20f };
    static const RGB wood_dark = { 0.36f, 0.22f, 0.10f };
    static const RGB wood_light= { 0.76f, 0.55f, 0.30f };
    static const RGB latch     = { 0.60f, 0.60f, 0.48f };
    static const RGB latch_dark= { 0.24f, 0.24f, 0.17f };

    for(int px = 0; px < w; ++px)
        for(int py = 0; py < h; ++py)
        {
            // 0..15 inside the tile, whatever the atlas resolution is.
            const int u = (px * 16) / w;
            const int v = (py * 16) / h;

            const bool frame = (u == 0 || u == 15 || v == 0 || v == 15);
            const bool inner_edge = (u == 1 || u == 14 || v == 1 || v == 14);
            const bool lid_seam = (variant != 0) && (v == 5 || v == 6);

            RGB color = wood;
            if(frame)
                color = wood_dark;
            else if(inner_edge)
                color = wood_light;
            else if(lid_seam)
                color = (v == 5) ? wood_dark : wood;

            // A darker band across the middle of the side gives the lid a body.
            if(variant == 1 && v == 11 && u > 1 && u < 14)
                color = wood_dark;

            // The top is a lid: an inset panel with a plank line down the middle.
            if(variant == 0)
            {
                if((u == 2 || u == 13 || v == 2 || v == 13))
                    color = wood_light;
                if(u == 7 || u == 8)
                    color = wood_dark;
            }

            if(variant == 2)
            {
                // Hinges on the lid...
                if((u == 3 || u == 12) && v == 4)
                    color = latch_dark;
                // ...and the latch, with a keyhole, over the seam.
                if(u >= 6 && u <= 9 && v >= 4 && v <= 9)
                    color = latch;
                if(u >= 6 && u <= 9 && (v == 4 || v == 9))
                    color = latch_dark;
                if((u == 6 || u == 9) && v >= 4 && v <= 9)
                    color = latch_dark;
                if((u == 7 || u == 8) && v == 7)
                    color = latch_dark;
            }

            texture.bitmap[(x + px) + (y + py) * texture.width] = colorRGB(color);
        }
}

/** The three chest tiles in the atlas: top at (0,4), sides at (1,4), front at (2,4). */
static void paintChestTexture(TEXTURE &texture, const int field_width, const int field_height)
{
    paintChestTile(texture, 0 * field_width, 4 * field_height, field_width, field_height, 0);
    paintChestTile(texture, 1 * field_width, 4 * field_height, field_width, field_height, 1);
    paintChestTile(texture, 2 * field_width, 4 * field_height, field_width, field_height, 2);
}

/**
 * Draws one bed tile into a 16x16 field of the atlas.
 *
 * Like the chest, the bed has no art in the atlas files, so its four tiles are
 * painted here into row 5 (rows 4 and 5 were the unused ones; the chest took 4).
 * They are the only tiles in the game that belong to a *shape* rather than to a
 * block: the bed is a two-block slab, and its ends are told apart by these.
 *
 *   variant 0  the head end's top: the blanket, with the pillow pad inset
 *   variant 1  the underside: plain planks, which is what a bed is built on
 *   variant 2  a side: the blanket turned over the wooden frame
 *   variant 3  the foot end's top: the blanket on its own
 *
 * Every variant is left-right symmetric, and only the side has a meaningful top
 * and bottom, so no tile has to be turned to follow which way the bed faces.
 * That is deliberate: the top faces of the two halves meet along their short
 * edge, and a tile that had to be rotated per facing would need four variants of
 * each of the two top tiles to avoid a seam.
 */
static void paintBedTile(TEXTURE &texture, const int x, const int y, const int w, const int h, const int variant)
{
    static const RGB blanket      = { 0.66f, 0.17f, 0.14f };
    static const RGB blanket_dim  = { 0.48f, 0.11f, 0.09f };
    static const RGB blanket_lit  = { 0.80f, 0.28f, 0.22f };
    static const RGB pillow       = { 0.90f, 0.89f, 0.84f };
    static const RGB pillow_dim   = { 0.68f, 0.67f, 0.62f };
    static const RGB wood         = { 0.62f, 0.40f, 0.20f };
    static const RGB wood_dark    = { 0.36f, 0.22f, 0.10f };

    for(int px = 0; px < w; ++px)
        for(int py = 0; py < h; ++py)
        {
            // 0..15 inside the tile, whatever the atlas resolution is.
            const int u = (px * 16) / w;
            const int v = (py * 16) / h;

            RGB color = blanket;
            const bool edge = (u == 0 || u == 15);

            switch(variant)
            {
            case 0: // The pillow end.
            {
                // The blanket first, with a quiet quilt: every fourth row and
                // column of thread is a shade off.
                if(edge || v == 15)
                    color = blanket_dim;
                else if(u % 4 == 0 || v % 4 == 0)
                    color = blanket_lit;

                // The pillow pad, inset from every side so the blanket shows as a
                // border around it.
                if(u >= 2 && u <= 13 && v >= 3 && v <= 11)
                {
                    color = pillow;
                    if(v == 3 || v == 11)
                        color = pillow_dim;
                }
                break;
            }
            case 1: // The underside.
            {
                color = wood;
                if(edge || v == 0 || v == 15)
                    color = wood_dark;
                // Plank seams, spaced so they line up between the two halves.
                else if(u == 5 || u == 10)
                    color = wood_dark;
                break;
            }
            case 2: // A side: blanket over the frame.
            {
                if(v <= 5)
                {
                    color = blanket;
                    if(v == 5)
                        color = blanket_dim;      // the seam where the blanket tucks in
                    else if(edge)
                        color = blanket_dim;
                    else if(v == 0)
                        color = blanket_lit;
                }
                else
                {
                    color = wood;
                    if(v == 6)
                        color = wood_dark;
                    else if(v == 15)
                        color = wood_dark;
                    else if(edge)
                        color = wood_dark;
                }
                break;
            }
            default: // 3: the foot end's top, blanket only.
            {
                if(edge || v == 15)
                    color = blanket_dim;
                else if(u % 4 == 0 || v % 4 == 0)
                    color = blanket_lit;
                // A fold across the middle, so the foot end is not a flat print.
                else if(v >= 6 && v <= 7)
                    color = blanket_lit;
                break;
            }
            }

            texture.bitmap[(x + px) + (y + py) * texture.width] = colorRGB(color);
        }
}

/** The four bed tiles in the atlas, in row 5: pillow, underside, side, blanket. */
static void paintBedTexture(TEXTURE &texture, const int field_width, const int field_height)
{
    paintBedTile(texture, 0 * field_width, 5 * field_height, field_width, field_height, 0);
    paintBedTile(texture, 1 * field_width, 5 * field_height, field_width, field_height, 1);
    paintBedTile(texture, 2 * field_width, 5 * field_height, field_width, field_height, 2);
    paintBedTile(texture, 3 * field_width, 5 * field_height, field_width, field_height, 3);
}

// Some embedded texture variants store block-breaking frames with white background
// instead of transparency. Convert pure white to transparent in that strip.
static void fixBreakingOverlayTransparency(TEXTURE &texture, const int x, const int y, const int w, const int h)
{
    const int x_end = x + w;
    const int y_end = y + h;

    for(int x1 = x; x1 < x_end; ++x1)
        for(int y1 = y; y1 < y_end; ++y1)
        {
            COLOR &pixel = texture.bitmap[x1 + y1 * texture.width];
            if(pixel == 0xFFFF)
                pixel = 0;
        }
}

void terrainInit(const char *texture_path)
{
    terrain_current = loadTextureFromFile(texture_path);
    if(!terrain_current)
    {
        terrain_current = &tex_terrain3; // Use embedded terrain3 texture when file unavailable
        if(!terrain_current)
            terrain_current = &terrain2; //Fallback to built-in terrain2
    }
    else
        puts("External texture loaded!");

    // Enable transparency for all terrain textures (black=0x0000 is the transparent color)
    terrain_current->has_transparency = true;
    terrain_current->transparent_color = 0x0000;

    int fields_x = 16;
    int fields_y = 16;
    int field_width = terrain_current->width / fields_x;
    int field_height = terrain_current->height / fields_y;

    // Crack textures are at (0..9, 15) in the terrain atlas.
    fixBreakingOverlayTransparency(*terrain_current, 0, 15 * field_height, 10 * field_width, field_height);

    //Give grass and leaves color
    const RGB green = { 0.5f, 0.8f, 0.3f };
    makeColor(green, *terrain_current, 0, 0, field_width, field_height);
    makeColor(green, *terrain_current, 5 * field_width, 3 * field_height, field_width, field_height);
    makeColor(green, *terrain_current, 4 * field_width, 3 * field_height, field_width, field_height);

    //Also redstone
    drawTexture(*terrain_current, *terrain_current, 4 * field_width, 10 * field_height, field_width, field_height, 4 * field_width, 11 * field_height, field_width, field_height);
    drawTexture(*terrain_current, *terrain_current, 5 * field_width, 10 * field_height, field_width, field_height, 5 * field_width, 11 * field_height, field_width, field_height);
    const RGB black = { 0.3f, 0.2f, 0.2f };
    makeColor(black, *terrain_current, 4 * field_width, 10 * field_height, field_width, field_height);
    makeColor(black, *terrain_current, 5 * field_width, 10 * field_height, field_width, field_height);
    const RGB red = { 1.0f, 0.2f, 0.2f };
    makeColor(red, *terrain_current, 4 * field_width, 11 * field_height, field_width, field_height);
    makeColor(red, *terrain_current, 5 * field_width, 11 * field_height, field_width, field_height);

    //And redstone switches
    drawTexture(*terrain_current, *terrain_current, 0 * field_width, 6 * field_height, field_width, field_height, 10 * field_width, 15 * field_height, field_width, field_height);
    const RGB red_tint = { 1.0f, 0.8f, 0.8f };
    makeColor(red_tint, *terrain_current, 10 * field_width, 15 * field_height, field_width, field_height);

    // Water has opacity of 0.5 but it's not rendered with alpha here.
    // makeWaterOpaque(*terrain_current, 13 * field_width, 12 * field_height, field_width, field_height);

    // The chest has no texture in the atlas files, so it is painted here. This
    // has to happen before terrain_resized is made, so the inventory and the
    // block list get the icon as well.
    paintChestTexture(*terrain_current, field_width, field_height);
    // ...and the bed, whose four tiles are painted for the same reason.
    paintBedTexture(*terrain_current, field_width, field_height);

    if(terrain_current->width == 384 && terrain_current->height == 384)
        terrain_resized = terrain_current;
    else
        terrain_resized = resizeTexture(*terrain_current, 384, 384);

    for(int y = 0; y < fields_y; y++)
        for(int x = 0; x < fields_x; x++)
        {
            //+1 and -2 to work around GLFix inaccuracies resulting in rounding errors
            TerrainAtlasEntry tea = terrain_atlas[x][y] = {textureArea(x * field_width + 1, y * field_height + 1, field_width - 2, field_height - 2),
                                                            textureArea(x * 24, y * 24, 24, 24) };

            BLOCK_TEXTURE bt = texture_atlas[y][x];
            if(bt.sides == 0)
                continue;

            if(bt.sides & BLOCK_BOTTOM_BIT)
                block_textures[bt.block][BLOCK_BOTTOM] = tea;
            if(bt.sides & BLOCK_TOP_BIT)
                block_textures[bt.block][BLOCK_TOP] = tea;
            if(bt.sides & BLOCK_LEFT_BIT)
                block_textures[bt.block][BLOCK_LEFT] = tea;
            if(bt.sides & BLOCK_RIGHT_BIT)
                block_textures[bt.block][BLOCK_RIGHT] = tea;
            if(bt.sides & BLOCK_FRONT_BIT)
                block_textures[bt.block][BLOCK_FRONT] = tea;
            if(bt.sides & BLOCK_BACK_BIT)
                block_textures[bt.block][BLOCK_BACK] = tea;
        }

    //Slight hack, you can't assign a texture to multiple blocks
    block_textures[BLOCK_GRASS][BLOCK_BOTTOM] = block_textures[BLOCK_DIRT][BLOCK_BOTTOM];

    // Assign special_block_textures based on special_block_texture_idx
    for(unsigned int i = 0; i < sizeof(special_block_texture_idx)/sizeof(*special_block_texture_idx); i++)
    {
        auto &idx = special_block_texture_idx[i];
        special_block_textures[i] = terrain_atlas[idx.x][idx.y];
    }

    //Prerender four times the same texture to speed up drawing, see terrain.h
    const BLOCK_TEXTURE quad_textures[] = { ALL(BLOCK_DIRT), SID(BLOCK_GRASS), TOP(BLOCK_GRASS), ALL(BLOCK_STONE), ALL(BLOCK_SAND), SID(BLOCK_WOOD), ALL(BLOCK_PLANKS_NORMAL), ALL(BLOCK_LEAVES) };
    terrain_quad = newTexture(field_width * 2 * (sizeof(quad_textures)/sizeof(*quad_textures)), field_height * 2);

    for(BLOCK b = 0; b <= BLOCK_NORMAL_LAST; b++)
        for(uint8_t s = 0; s <= BLOCK_SIDE_LAST; s++)
            quad_block_textures[b][s].has_quad = false;

    unsigned int x = 0;
    for(BLOCK_TEXTURE bt : quad_textures)
    {
        TextureAtlasEntry *tae = nullptr;

        if(bt.sides & BLOCK_BOTTOM_BIT)
            tae = &block_textures[bt.block][BLOCK_BOTTOM].current;
        if(bt.sides & BLOCK_TOP_BIT)
            tae = &block_textures[bt.block][BLOCK_TOP].current;
        if(bt.sides & BLOCK_LEFT_BIT)
            tae = &block_textures[bt.block][BLOCK_LEFT].current;
        if(bt.sides & BLOCK_RIGHT_BIT)
            tae = &block_textures[bt.block][BLOCK_RIGHT].current;
        if(bt.sides & BLOCK_FRONT_BIT)
            tae = &block_textures[bt.block][BLOCK_FRONT].current;
        if(bt.sides & BLOCK_BACK_BIT)
            tae = &block_textures[bt.block][BLOCK_BACK].current;

        if(!tae)
        {
            printf("Block %d has no texture!\n", bt.block);
            continue;
        }

        //- 1 to reverse the workaround above. Yes, I hate myself for this.
        drawTexture(*terrain_current, *terrain_quad, tae->left - 1, tae->top - 1, field_width, field_height, x, 0, field_width, field_height);
        drawTexture(*terrain_current, *terrain_quad, tae->left - 1, tae->top - 1, field_width, field_height, x + field_width, 0, field_width, field_height);
        drawTexture(*terrain_current, *terrain_quad, tae->left - 1, tae->top - 1, field_width, field_height, x+ field_width, field_height, field_width, field_height);
        drawTexture(*terrain_current, *terrain_quad, tae->left - 1, tae->top - 1, field_width, field_height, x, field_height, field_width, field_height);

        //Get an average color of the block
        RGB sum;
        for(unsigned int tex_x = tae->left - 1; tex_x <= tae->right; ++tex_x)
            for(unsigned int tex_y = tae->top - 1; tex_y <= tae->bottom; ++tex_y)
            {
                RGB rgb = rgbColor(terrain_current->bitmap[tex_x + tex_y*terrain_current->width]);
                sum.r += rgb.r;
                sum.g += rgb.g;
                sum.b += rgb.b;
            }

        int pixels = field_width * field_height;
        sum.r /= pixels;
        sum.g /= pixels;
        sum.b /= pixels;

        auto colorWithBrightness = [](const RGB &c, const GLFix brightness) {
            return colorRGB(c.r * brightness, c.g * brightness, c.b * brightness);
        };

        //And add the workaround here again..
        TerrainQuadEntry tqe = {
            true,
            textureArea(x + 1, 1, field_width * 2 - 2, field_height * 2 - 2),
            { colorRGB(sum),
              colorWithBrightness(sum, 0.90f),
              colorWithBrightness(sum, 0.80f),
              colorWithBrightness(sum, 0.85f),
              colorWithBrightness(sum, 0.95f), }
        };

        if(bt.sides & BLOCK_BOTTOM_BIT)
            quad_block_textures[bt.block][BLOCK_BOTTOM] = tqe;
        if(bt.sides & BLOCK_TOP_BIT)
            quad_block_textures[bt.block][BLOCK_TOP] = tqe;
        if(bt.sides & BLOCK_LEFT_BIT)
            quad_block_textures[bt.block][BLOCK_LEFT] = tqe;
        if(bt.sides & BLOCK_RIGHT_BIT)
            quad_block_textures[bt.block][BLOCK_RIGHT] = tqe;
        if(bt.sides & BLOCK_FRONT_BIT)
            quad_block_textures[bt.block][BLOCK_FRONT] = tqe;
        if(bt.sides & BLOCK_BACK_BIT)
            quad_block_textures[bt.block][BLOCK_BACK] = tqe;

        x += field_width * 2;
    }

    //Part 2 of the hack above
    quad_block_textures[BLOCK_GRASS][BLOCK_BOTTOM] = quad_block_textures[BLOCK_DIRT][BLOCK_BOTTOM];

    if(lcd_type() == SCR_320x240_4)
    {
        greyscaleTexture(*terrain_current);
        if(terrain_resized != terrain_current)
            greyscaleTexture(*terrain_resized);
        greyscaleTexture(*terrain_quad);
    }

    //Make the texture available to others for sharing
    inv_selection_p = &inv_selection;

    door_preview = newTexture(16, 32);
    TextureAtlasEntry door = terrain_atlas[1][5].current;
    door.bottom += door.bottom - door.top; //Double height
    drawTexture(*terrain_current, *door_preview, door.left, door.top, door.right - door.left, door.bottom - door.top, 0, 0, 16, 32);
}

void terrainUninit()
{
    if(terrain_resized != terrain_current)
        deleteTexture(terrain_resized);

    // terrain_current may point to a static texture (terrain or terrain2); only delete when it was dynamically loaded
    if(terrain_current != &terrain && terrain_current != &terrain2)
        deleteTexture(terrain_current);

    deleteTexture(terrain_quad);

    deleteTexture(door_preview);
}
