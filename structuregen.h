#ifndef STRUCTUREGEN_H
#define STRUCTUREGEN_H

#include <stdint.h>

#include "biomegen.h"

/**
 * Deterministic, dependency-free procedural structures: dungeons, ruins and
 * desert temples, with the chests and the loot inside them.
 *
 * Same contract as villagegen.h and biomegen.h, and for the same reasons:
 *
 *  - a structure is a pure function of (world seed, cell coordinates), so chunks
 *    can stream in any order, alone, with no cross-chunk bookkeeping,
 *  - every chunk emits only the blocks of a structure that fall inside itself,
 *  - no engine header is included, so the whole layout is unit-tested on the
 *    host (tests/structuregen_test.cc) and the block ids are passed in as the
 *    numeric codes documented in structuregen.cpp,
 *  - nothing here allocates or keeps state, so the generator costs a couple of
 *    hashes per chunk on the CX.
 *
 * A structure cell is 128 blocks across (a power of two, so the hot path has no
 * integer division) and most cells hold nothing. The chests are filled by the
 * caller through ChestStore, which is why the loot is exposed here as plain
 * (stack, count) pairs: the module itself must not know about the world.
 *
 * The palette is limited by the block ids this game actually has — there is no
 * mossy cobblestone, no stone bricks and no sandstone, so a dungeon is
 * cobblestone with spiderwebs, a ruin is a broken cobblestone and plank hut, and
 * a temple is a sand and cobblestone step pyramid.
 */
namespace Structures
{
    enum Kind
    {
        KindNone = 0,
        KindDungeon,   ///< a buried cobblestone room with a chest
        KindRuin,      ///< a broken hut on the surface with a chest
        KindTemple,    ///< a step pyramid in a desert, chest under the floor
        KindCount
    };

    /** Grid spacing of structure cells, in blocks. A power of two. */
    constexpr int CellBlocks = 128;
    /** Blocks per chunk edge; must match Chunk::SIZE (chunk.cpp static_asserts). */
    constexpr int ChunkBlocks = 8;
    /** Maximum offset of a candidate from the cell's centre, in blocks. */
    constexpr int CellJitter = 24;
    /** Candidate origins tried inside a cell before giving up. */
    constexpr int CandidateCount = 2;
    /** Largest half-extent of any structure, so callers know how far one reaches. */
    constexpr int MaxReach = 6;

    /**
     * A dungeon needs this much ground above it to stay buried: it sits eleven
     * blocks below the surface, and eight blocks of rock have to stay above its
     * ceiling. The terrain spends most of its time just above sea level, so this
     * is deliberately low enough that a dungeon is not only in the highest hills.
     */
    constexpr int DungeonMinGroundY = 18;
    /** Surface structures are kept off beaches, rivers and the world ceiling. */
    constexpr int SurfaceMinGroundY = 15;
    constexpr int SurfaceMaxGroundY = 30;

    /** Structures that can hold a chest, in one plan. */
    constexpr int MaxChests = 2;
    /** Stacks one chest may hold. */
    constexpr int MaxLootStacks = 5;

    /** One stack of loot. `stack` is a BLOCK_WDATA, so items are BLOCK_ITEM + id. */
    struct Loot
    {
        uint16_t stack = 0;
        unsigned int count = 0;
    };

    struct Plan
    {
        bool valid = false;
        int kind = KindNone;
        int cell_x = 0, cell_z = 0;
        /** World block coordinates of the structure's local origin (dx=dz=dy=0). */
        int origin_x = 0, origin_y = 0, origin_z = 0;
        /** Layout seed, derived from the world seed and the cell. */
        uint32_t seed = 0;
    };

    /** Deterministic mixture of the world seed and a cell coordinate. */
    uint32_t hashSeed(uint32_t world_seed, int cell_x, int cell_z, uint32_t salt);

    /** True when this cell hosts a structure at all (roughly one in six). */
    bool cellHasStructure(uint32_t world_seed, int cell_x, int cell_z);
    /** Which of the three kinds the cell would hold (independent of its site). */
    int cellKind(uint32_t world_seed, int cell_x, int cell_z);
    /** Origin of candidate `index` (0..CandidateCount-1) inside a cell. */
    void cellCandidate(uint32_t world_seed, int cell_x, int cell_z, int index, int &out_x, int &out_z);

    /**
     * Builds the plan for an accepted candidate. `ground_y` is the surface height
     * of the candidate column and `biome` its biome, both from the terrain
     * generator, so the caller and the structure always agree about the world.
     * Returns false when the site cannot hold this kind (water, too little rock
     * above a dungeon, a roof above the world ceiling, a temple outside a desert).
     */
    bool planStructure(uint32_t world_seed, int cell_x, int cell_z, int candidate, int ground_y, int biome, Plan &out);

    /** Receives one block, in world block coordinates; a 0 means "carve to air". */
    typedef void (*SetBlockFn)(void *context, int world_x, int world_y, int world_z, uint16_t block);

    /**
     * Emits every block of the plan that falls inside the given chunk, and
     * nothing else, so each chunk writes its own slice exactly once.
     */
    void emitChunk(const Plan &plan, int chunk_x, int chunk_y, int chunk_z, SetBlockFn set, void *context);

    /** Number of chests the plan holds. */
    int chestCount(const Plan &plan);
    /** World position of chest `index`; false when out of range. */
    bool chestPosition(const Plan &plan, int index, int &out_x, int &out_y, int &out_z);
    /** True when the plan defines a block at that world position (used by tests). */
    bool blockAtWorld(const Plan &plan, int world_x, int world_y, int world_z, uint16_t &out);

    /**
     * Fills the loot of chest `index` (deterministic from the plan seed), up to
     * `max_stacks` entries, and returns how many were written. Counts and choices
     * both come from the same hash, so two visits to the same chest agree.
     */
    int chestLoot(const Plan &plan, int index, Loot *out, int max_stacks);

    /** Highest world Y any block of this plan can occupy (for tests and callers). */
    int highestY(const Plan &plan);
}

#endif // STRUCTUREGEN_H
