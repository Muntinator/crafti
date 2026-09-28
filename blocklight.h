#ifndef BLOCKLIGHT_H
#define BLOCKLIGHT_H

#include <stdint.h>

/**
 * Per-block light: how bright one block's faces are, from how deep the block
 * sits under the sky and from the blocks that glow.
 *
 * The lighting is split in two, which is what keeps the day/night cycle cheap:
 *
 *  - the **spatial** part is baked into every vertex while the chunk is meshed
 *    (a cave is dark at noon, a hilltop is bright at night, a torch lights the
 *    blocks around it). Building a chunk already costs a mesh, so this adds no
 *    per-frame work at all,
 *  - the **time** part is the global day/night tint (`nglSetGlobalShade`), which
 *    the rasteriser multiplies into every vertex it draws. A day passing
 *    therefore costs nothing but that one multiply, and nothing has to be
 *    re-meshed as the sun moves.
 *
 * The block ids are passed in as bare numbers (the same way biomegen.h does it)
 * so this module stays free of the engine and of the texture tables: the tests
 * check every id against terrain.h instead.
 *
 * Everything here is integer math and pure, so the curve, the depth falloff and
 * the light lattice are unit tested on the host (tests/blocklight_test.cc).
 */
namespace BlockLight
{
    /** Light levels are vanilla's 0..15. */
    constexpr int MaxLevel = 15;

    /**
     * A sky height that means "the sky is straight overhead": every block is fully
     * lit whatever its depth. It is out of the world's height range (40 blocks),
     * which is what makes it usable as a marker for the flat and graph worlds,
     * where the terrain height means nothing and nothing should be darkened.
     */
    constexpr int SkyAlwaysOpen = 255;

    /**
     * Darkest shade any block can have, as a 0..255 factor. Full black would be
     * unreadable on the CX's reflective screen, and the day/night tint is
     * multiplied on top of this, so a cave in the middle of the night stays just
     * about navigable instead of turning into a black rectangle.
     */
    constexpr int MinShade = 88;
    /** Face shade of a vertex that brought none of its own (the rasteriser's neutral). */
    constexpr int NeutralFaceShade = 128;
    /** Full brightness, as a 0..255 factor. */
    constexpr int MaxShade = 255;

    // Block ids of everything that glows. Kept numeric on purpose; the host tests
    // pin them to terrain.h's BLOCK_* constants.
    constexpr int BlockGlowstone = 25;     ///< BLOCK_GLOWSTONE
    constexpr int BlockTorch = 127;        ///< BLOCK_TORCH
    constexpr int BlockLava = 134;         ///< BLOCK_LAVA
    constexpr int BlockRedstoneTorch = 149; ///< BLOCK_REDSTONE_TORCH

    /**
     * Sky light of a block at `world_y`, in a column whose highest sky-blocking
     * block is at `sky_y`. The block at the surface itself is fully lit, and the
     * light falls off by one level per block below it until it runs out, which is
     * what makes a cave or a tunnel dark and the ground bright.
     */
    int skyLevel(int world_y, int sky_y);

    /** The texture-shade factor (0..255) of a light level. */
    int shadeForLevel(int level);

    /**
     * The shade byte for a vertex: the face's own shade (0 meaning "neutral"),
     * multiplied by the light level, in the same 8-bit fixed point the rasteriser
     * uses for the global tint.
     */
    int combineShade(int face_shade, int level);

    /** The level a block emits, or 0 when it does not glow. `block_id` is a BLOCK id. */
    int emitterLevel(int block_id);
    /** True when a block lights itself, so its own faces must stay bright. */
    bool isEmitter(int block_id);

    /**
     * The light a chunk's own emitters spread, as a cubic lattice of levels.
     *
     * A source is planted at its own cell and then relaxed until it stops
     * changing: every cell takes the brightest of its six neighbours minus one.
     * That is a small, fixed amount of work per chunk *that has a light in it* —
     * a chunk without one is cleared and relaxed once, and stops immediately.
     *
     * The lattice is chunk-local, which is the one approximation here: a torch at
     * a chunk border is carried over by seeding the border cell of the neighbour's
     * lattice from the block just outside it, but light further away than that
     * does not cross. On a calculator screen, with a 16-block view distance, that
     * is not visible; it is also why the field is rebuilt only when a chunk is
     * re-meshed, i.e. when a block changed anyway.
     */
    namespace Field
    {
        /** Edge of the lattice. chunk.cpp static_asserts this against Chunk::SIZE. */
        constexpr int Size = 8;
        constexpr int Cells = Size * Size * Size;

        constexpr int Index(int x, int y, int z) { return (y * Size + z) * Size + x; }
        constexpr bool inBounds(int x, int y, int z)
        {
            return x >= 0 && y >= 0 && z >= 0 && x < Size && y < Size && z < Size;
        }

        /** Sets every cell to zero. */
        void clear(uint8_t *field);
        /**
         * Plants a light source of `level` at one cell, ignoring anything outside
         * the lattice. Returns the level that was planted (0 when it was clipped).
         */
        int plant(uint8_t *field, int x, int y, int z, int level);
        /**
         * One relaxation pass over the six axis neighbours. Returns how many
         * cells changed, so a caller can stop as soon as a pass changes nothing.
         */
        int relax(uint8_t *field);
        /** Level at a cell; 0 outside the lattice. */
        int at(const uint8_t *field, int x, int y, int z);
        /** Highest level in the lattice, for tests and callers. */
        int brightest(const uint8_t *field);
    }
}

#endif // BLOCKLIGHT_H
