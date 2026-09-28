#ifndef BIOMEGEN_H
#define BIOMEGEN_H

#include <stdint.h>

/**
 * Deterministic, dependency-free biomes, mountains, rivers, caves and ravines.
 *
 * This is the Stage 2 counterpart to villagegen.h and it follows the same rules:
 *
 *  - everything here is a pure function of (world seed, world block
 *    coordinates), so every chunk can be generated in any order, alone, without
 *    cross-chunk bookkeeping and without a pending-change queue,
 *  - no engine header is included and no memory outside the arguments is
 *    touched, so the whole module is unit-tested on the host
 *    (tests/biomegen_test.cc),
 *  - the block ids are passed in as the numeric codes documented in terrain.h,
 *    which keeps this header free of nGL and of the texture tables.
 *
 * The caller owns the terrain itself: it computes the Perlin base height and
 * hands it to columnAt(), which returns the final surface height plus the two
 * surface blocks for that column. Chunk::generate() calls the same function for
 * terrain, for the village site search and for the mob spawn rules, so all of
 * them always agree on where the ground is.
 *
 * Everything is integer math on a lattice whose spacing is a power of two, so
 * the CX pays no integer division in the generation hot path: the only shifts
 * are the ones the lattice size implies.
 *
 * On the block palette: this game has no snow, ice, sandstone, gravel, clay or
 * cactus block id, so the biomes are limited to what the existing ids can
 * express honestly (water/beach/grass/dirt/sand/stone/leaves/wood). There is
 * deliberately no "snowy" biome that would have to fake snow with white wool.
 */
namespace BiomeGen
{
    enum Biome
    {
        BiomeOcean = 0,
        BiomeBeach,
        BiomeDesert,
        BiomePlains,
        BiomeForest,
        BiomeSavanna,
        BiomeMountains,
        BiomeCount
    };

    const char *biomeName(int biome);

    // Block ids, kept numeric so this header stays free of the engine.
    constexpr uint8_t BlockAir = 0;
    constexpr uint8_t BlockStone = 1;
    constexpr uint8_t BlockDirt = 2;
    constexpr uint8_t BlockSand = 3;
    constexpr uint8_t BlockGrass = 20;
    constexpr uint8_t BlockWater = 133;

    /** Sea level, in world blocks. The ocean/beach/land split hangs off this. */
    constexpr int SeaLevel = 12;
    /** Lowest surface height terrain generation may produce. */
    constexpr int MinHeight = 4;
    /**
     * Highest surface height terrain generation may produce. chunk.cpp
     * static_asserts that this matches World::HEIGHT * Chunk::SIZE - 3, so the
     * terrain keeps its old ceiling.
     */
    constexpr int MaxHeight = 37;

    /**
     * The surface of one world column. `height` is the world Y of the top
     * terrain block, so the column spans y = 0 .. height - 1 and `surface` is
     * the block at y = height - 1. Water then fills up to SeaLevel on top.
     */
    struct Column
    {
        int biome = BiomePlains;
        int height = SeaLevel + 2;
        uint8_t surface = BlockGrass;
        uint8_t subsurface = BlockDirt;
    };

    /**
     * Resolves a column from the caller's Perlin base height. `base_height` is
     * the height the classic 4-octave terrain noise produced, before biomes,
     * mountains and rivers.
     */
    void columnAt(uint32_t world_seed, int world_x, int world_z, int base_height, Column &out);

    /** Convenience wrapper around columnAt() for callers that only want the biome. */
    int biomeAt(uint32_t world_seed, int world_x, int world_z, int base_height);

    /**
     * How much a river has carved this column down, in blocks (0 outside a
     * river). Exposed because it is the one height term that is meaningful on
     * its own: gameplay code can ask "is this column in a river bed?" without
     * recomputing the Perlin base.
     */
    int riverDepth(uint32_t world_seed, int world_x, int world_z);

    /**
     * Tree density for a biome, as a percent chance per column. Terrain
     * generation rolls it for every grass column; chunk.cpp keeps a hard
     * per-chunk cap on top so the cost on the CX stays bounded.
     */
    int treeDensityPercent(int biome);

    /**
     * True when the block at this position is carved out as a cave. Two
     * independent 3D fields have to be near their centre at the same time, which
     * is what turns the intersection into a winding tunnel instead of a blob.
     * Only meaningful underground: the caller keeps the surface block intact.
     */
    bool caveAt(uint32_t world_seed, int world_x, int world_y, int world_z);

    /**
     * A ravine crossing one column. Ravines are carved from a deterministic
     * segment per cell of a coarse grid, so they are long, deep and narrow
     * instead of round.
     */
    struct Ravine
    {
        bool present = false;
        int lowest = 0;  ///< lowest world Y carved in this column
        int highest = -1; ///< highest world Y carved in this column (inclusive)
        int strength = 0; ///< 0..1024, 1024 directly on the ravine's centre line
    };

    /**
     * Resolves the ravine at one column. `surface_height` is the column's final
     * surface height; a ravine never reaches it, so the surface stays sealed and
     * no cave or ravine can drain an ocean through the floor.
     */
    void ravineAt(uint32_t world_seed, int world_x, int world_z, int surface_height, Ravine &out);

    /** Grid spacing of the ravine cells, in blocks. A power of two. */
    constexpr int RavineCellBlocks = 256;
    /** Longest reach of a ravine outside its own cell, in blocks (for callers). */
    constexpr int RavineReach = 96;
}

#endif // BIOMEGEN_H
