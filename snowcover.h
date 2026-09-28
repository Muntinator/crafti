#ifndef SNOWCOVER_H
#define SNOWCOVER_H

#include <stdint.h>

/**
 * Snow cover: how deep a snow layer grows, what it can rest on, and which columns
 * a step of snow touches.
 *
 * This is the snow counterpart of weather.h and it follows the same rules: pure
 * functions of (world seed, world clock ticks, the block ids of the column), no
 * engine header, no state of its own, and therefore unit-tested whole on the host
 * (tests/snowcover_test.cc).
 *
 * A snow layer is an ordinary block whose *data byte* is its depth, 1..MaxLayers.
 * Nothing about it is saved: its depth is in the world like any other block's
 * data, and the clock decides when it grows and when it melts.
 *
 * The depth tops out at half a block on purpose. Vanilla grows snow to eight
 * steps, where the last step is a full cube the player stands on -- but this
 * engine has no auto step-up, so a full-cube snow block that quietly appeared
 * under a player standing still would be a trap rather than a surface. Every
 * depth here is a slab the player walks straight through (the AABB is real, so it
 * can still be aimed at and broken), which is why MaxLayers is half of vanilla's.
 *
 * Block ids are passed in as the numeric codes documented in terrain.h, exactly
 * as biomegen.h does it, so this header stays free of nGL and of the texture
 * tables.
 */
namespace SnowCover
{
    /** Deepest snow a column accumulates, in steps. Each step is 2/16 of a block. */
    constexpr int MaxLayers = 4;
    /** Height one step adds, in sixteenths of a block. */
    constexpr int LayerSixteenths = 2;
    /** Ticks between two accumulation or melt steps. */
    constexpr int LayerTicks = 20;
    /** Columns a single step looks at. */
    constexpr int ColumnsPerStep = 3;
    /** How far from the player a step may reach, in blocks. */
    constexpr int Radius = 12;
    /**
     * How far up a candidate column is searched for a roof. Snow does not form
     * indoors or in caves, and the search stops here so a column deep underground
     * costs a bounded number of block reads rather than a walk to the sky.
     */
    constexpr int MaxSkyScan = 48;
    /**
     * How far below the player's feet the ground is looked for. The player is
     * usually standing on it; this is the allowance for standing in a dip, on a
     * slope or on the block next to the one that got picked.
     */
    constexpr int MaxGroundDrop = 6;

    /**
     * A block as this module reads it: the low byte is the block id and bits 8-14
     * are its data byte. That is exactly terrain.h's BLOCK_WDATA packing, spelled
     * with a plain uint16_t here so this header (and its host test) stays free of
     * nGL and can be handed the world's blocks with no conversion at all.
     */
    typedef uint16_t Block;
    constexpr uint8_t blockId(const Block block) { return block & 0xFF; }
    constexpr uint8_t blockData(const Block block) { return (block >> 8) & 0x7F; }
    constexpr Block makeBlock(const uint8_t id, const uint8_t data) { return static_cast<Block>((data << 8) | id); }

    // Block ids, numeric so this header stays free of the engine.
    constexpr uint8_t BlockAir = 0;
    constexpr uint8_t BlockLava = 134;
    constexpr uint8_t BlockWater = 133;
    constexpr uint8_t BlockWaterFast = 151;
    constexpr uint8_t BlockTorch = 127;
    constexpr uint8_t BlockRedstoneTorch = 149;
    constexpr uint8_t BlockRedstoneWire = 148;
    constexpr uint8_t BlockPressurePlate = 150;
    constexpr uint8_t BlockFlower = 128;
    constexpr uint8_t BlockMushroom = 131;
    constexpr uint8_t BlockWheat = 135;
    constexpr uint8_t BlockSpiderweb = 129;
    constexpr uint8_t BlockItem = 254;
    /** The snow layer itself (matches terrain.h's BLOCK_SNOW). */
    constexpr uint8_t BlockSnow = 47;

    /**
     * Blocks precipitation falls through: air, the fluids, and the flat or
     * billboard blocks that do not cover a column. Everything else -- stone, dirt,
     * grass, sand, wood, leaves, wool, glass, the ores -- stops a column from
     * being "under the sky", whether or not it is a full cube.
     *
     * This is a list of what lets water through rather than a list of what blocks
     * it, so a block this game grows later is treated as solid cover until someone
     * says otherwise: the safe direction to be wrong in.
     */
    bool isPermeable(uint8_t block);

    /** True where a snow layer can come to rest: on anything that is not permeable. */
    inline bool supportsSnow(uint8_t block) { return !isPermeable(block) && block != BlockLava; }

    /** The depth stored in a block's data byte, clamped into 1..MaxLayers. */
    int layersOf(uint8_t data);

    /**
     * One accumulation step on a column.
     *
     * Cold and snowing grows the layer by one step, cold and dry leaves it alone,
     * and anything warmer melts one step -- including snow that was placed by hand
     * or carried in from a cold biome. Melting is deliberately unconditional on
     * the weather: in a warm place the sun does that, and it is what stops a
     * desert from keeping a snowball. Returns the new depth, 0..MaxLayers, where 0
     * means the column has no snow left and the block should be removed.
     */
    int step(int layers, bool snowing, bool freezing);

    /**
     * The column a step touches, as an offset in blocks from the player.
     * `index` selects which of the step's candidates is wanted. Pure, so the same
     * tick, seed and index always name the same column: a reload cannot put the
     * snow somewhere else.
     */
    void columnOffset(uint32_t world_seed, unsigned long long step_index, int index, int &dx, int &dz);

    /**
     * One accumulation step on one column, with the column's blocks supplied by the
     * caller through `read`: `read(world_y)` returns the block at that height.
     *
     * The template is on purpose. Reading a column is the only thing this rule
     * needs from the world, so taking it as a callable means every rule about
     * *where* snow may sit is in this header and therefore in the host test: the
     * depth it grows, that a cell holding anything but air or snow is left alone,
     * that a covered column gets nothing, that an existing drift grows in place
     * instead of stacking a new block on top of itself, and that a drift in a warm
     * place is reached and melts even though it is in its own way. The calculator
     * passes a lambda over World::getBlock; the test passes a lambda over a table.
     *
     * `feet_y` is the block the player's feet are in and `world_top` the first
     * height outside the world.
     *
     * Returns -1 when the column is left exactly as it is, 0 when the layer there
     * has melted away and the block should be removed, or the new depth
     * 1..MaxLayers. `cover_y` is set to the height the write belongs at whenever
     * the result is not -1.
     */
    template <typename ReadBlock>
    int columnStep(const int feet_y, const int world_top, ReadBlock read,
                   const bool snowing, const bool freezing, int &cover_y)
    {
        cover_y = -1;

        // The first block below the feet that can carry a layer. A snow layer is
        // skipped rather than counted as ground, so a drift grows in place --
        // counting it would stack one layer on top of the last, one block per step,
        // and the world would grow a white column wherever the weather liked.
        int ground_y = -1;
        for(int wy = feet_y - 1; wy >= feet_y - 1 - MaxGroundDrop && wy >= 0; --wy)
        {
            const uint8_t block = blockId(read(wy));
            if(block == BlockSnow)
                continue;
            if(!supportsSnow(block))
                continue;
            ground_y = wy;
            break;
        }

        if(ground_y < 0)
            return -1;

        const int cell_y = ground_y + 1;
        if(cell_y >= world_top)
            return -1;

        const Block existing = read(cell_y);
        const uint8_t existing_block = blockId(existing);

        // Only a layer this system placed is ever touched: a torch, a flower or a
        // block the player put down in that cell is not snow's business.
        if(existing_block != BlockAir && existing_block != BlockSnow)
            return -1;

        // Snow comes out of the sky, so a covered column gets none (caves and the
        // insides of houses stay bare). Two details keep the rule honest:
        //
        //  - the scan starts *above* the cell the layer occupies, because starting
        //    at it would count an existing drift as its own roof and snow that had
        //    already settled could never grow another step or melt away again,
        //  - snow higher up the column is not a roof either, for the same reason
        //    one step deeper: a drift that was stacked by hand deepens and melts
        //    like any other.
        for(int wy = cell_y + 1; wy < world_top && wy < cell_y + MaxSkyScan; ++wy)
        {
            const uint8_t block = blockId(read(wy));
            if(block == BlockSnow || isPermeable(block))
                continue;
            return -1;
        }

        const int layers = existing_block == BlockSnow ? layersOf(blockData(existing)) : 0;
        const int next = step(layers, snowing, freezing);
        if(next == layers)
            return -1;

        cover_y = cell_y;
        return next;
    }
}

#endif // SNOWCOVER_H
