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

/*
 * Maps a location in the texture atlas to a block ID.
 *
 * The atlas itself is the official Minecraft 1.17.1 texture set, arranged into
 * this grid by tools/textures/gen_block_textures.py (which writes both the PNG
 * the desktop build loads and the embedded fallback header). The grid predates
 * those assets and is load-bearing: most of the renderers that do not draw plain
 * cubes -- torch, door, wheat, cake, lamp, switch, wire, bed, the breaking
 * overlay -- read their tile straight out of terrain_atlas[x][y], so a tile must
 * stay where it is. What lives where follows this table for the normal blocks
 * and the comments below for the rest.
 */
static const BLOCK_TEXTURE texture_atlas[][16] =
{
    { TOP(BLOCK_GRASS), ALL(BLOCK_STONE), ALL(BLOCK_DIRT), SID(BLOCK_GRASS), ALL(BLOCK_PLANKS_NORMAL), NON, NON, ALL(BLOCK_WALL), ALL(BLOCK_TNT), TOP(BLOCK_TNT), BOT(BLOCK_TNT), NON, NON, NON, NON, NON },
    { ALL(BLOCK_COBBLESTONE), ALL(BLOCK_BEDROCK), ALL(BLOCK_SAND), NON, SID(BLOCK_WOOD), TAB(BLOCK_WOOD), ALL(BLOCK_IRON), ALL(BLOCK_GOLD), ALL(BLOCK_DIAMOND), NON, NON, NON, NON, NON, NON, NON },
    { ALL(BLOCK_GOLD_ORE), ALL(BLOCK_IRON_ORE), ALL(BLOCK_COAL_ORE), FRO(BLOCK_BOOKSHELF), NON, NON, NON, NON, NON, NON, NON, TAB(BLOCK_CRAFTING_TABLE), FRO(BLOCK_FURNACE), SWF(BLOCK_FURNACE), NON, NON },
    { ALL(BLOCK_SPONGE), ALL(BLOCK_GLASS), ALL(BLOCK_DIAMOND_ORE), ALL(BLOCK_REDSTONE_ORE), NON, ALL(BLOCK_LEAVES), NON, NON, NON, NON, NON, SID(BLOCK_CRAFTING_TABLE), FRO(BLOCK_CRAFTING_TABLE), NON, TOP(BLOCK_FURNACE), NON },
    // Row 4 holds the chest's three faces and then the bed's four tiles.
    // bedrenderer.cpp names the bed's columns, because they are tiles of a
    // *shape* rather than of a block and only two of them are whole faces: the
    // plain blanket top is what the renderer draws for the foot end, so giving
    // it a block side would only make the item icon ambiguous.
    { TAB(BLOCK_CHEST), SID(BLOCK_CHEST), FRO(BLOCK_CHEST), NON, TOP(BLOCK_BED), BOT(BLOCK_BED), SID(BLOCK_BED), NON, NON, NON, NON, NON, NON, NON, NON, NON },
    // Row 5 belongs to the three things that draw themselves out of a single
    // tile: the torch (0,5), the door's two halves (1,5) and (1,6), and the
    // eight wheat stages (8..15,5). None of them is a normal block, so nothing
    // is mapped to a block side here.
    { NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON, NON },
    { NON, NON, NON, NON, NON, NON, TAB(BLOCK_PUMPKIN), ALL(BLOCK_NETHERRACK), NON, ALL(BLOCK_GLOWSTONE), NON, NON, NON, NON, NON, NON},
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

// The single tile each special block shows in an inventory slot (its icon);
// rendering reads the coordinates out of the renderers instead.
static const struct { int x, y; } special_block_texture_idx[BLOCK_SPECIAL_LAST - BLOCK_SPECIAL_START + 1] =
{
    {0, 5}, // Torch
    {12, 0}, // Flower -> the red flower (poppy)
    {11, 0}, // Spiderweb -> Spiderweb
    {9, 7}, // Cake -> The cake's top
    {12, 1}, // Mushroom -> The red mushroom
    {1, 6}, // Door -> Door bottom
    {13, 12}, // Water -> Water
    {13, 14}, // Lava -> Lava
    {15, 5}, // Wheat -> Wheat
    {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, // Gap...
    {3, 13}, // Redstone Lamp -> Lamp (off)
    {0, 6}, // Redstone Switch -> The lever
    {4, 10}, // Redstone Wire -> Redstone Wire
    {3, 7}, // Redstone Torch -> The unlit torch
    {3, 8}, // Pressure Plate -> The unpressed plate
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

    // The grass top and both leaf tiles ship grey: vanilla tints those with the
    // biome's grass and foliage colour (tools/textures/gen_block_textures.py),
    // and so do we, with the colours of the default biome.
    const RGB grass_color = { 145.0f / 255, 189.0f / 255, 89.0f / 255 }; //0x91bd59
    makeColor(grass_color, *terrain_current, 0, 0, field_width, field_height);
    const RGB foliage_color = { 119.0f / 255, 171.0f / 255, 47.0f / 255 }; //0x77ab2f
    makeColor(foliage_color, *terrain_current, 5 * field_width, 3 * field_height, field_width, field_height);
    makeColor(foliage_color, *terrain_current, 4 * field_width, 3 * field_height, field_width, field_height);

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

    // Water and lava are each one still frame of vanilla's animated textures.
    // Both are drawn with their own alpha (see FluidRenderer), so the tiles are
    // left alone: the water is the official texture tinted with the default
    // water colour, which the generator does.

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
