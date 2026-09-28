// Host tests for the per-block light.
//
// blocklight.cpp is engine-free on purpose: the depth falloff, the shading curve,
// the emitter table and the light lattice are all plain integer arithmetic, so
// the whole thing runs on a development host with no nGL, no chunk and no
// hardware. The block ids it keeps as numbers are checked against terrain.h here,
// which is what keeps them honest.
//
// Build and run with `make -C tests`.

#include "blocklight.h"
#include "terrain.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <vector>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

using namespace BlockLight;

static void test_sky_level_falls_off_with_depth()
{
    // The block at the surface sees the sky; each block below it loses one level.
    for(int sky_y = 1; sky_y < 40; ++sky_y)
    {
        CHECK(skyLevel(sky_y - 1, sky_y) == MaxLevel);
        CHECK(skyLevel(sky_y, sky_y) == MaxLevel);
        CHECK(skyLevel(sky_y - 2, sky_y) == MaxLevel - 1);
        CHECK(skyLevel(sky_y - MaxLevel, sky_y) == 1);
        CHECK(skyLevel(sky_y - MaxLevel - 1, sky_y) == 0);
        CHECK(skyLevel(sky_y - 30, sky_y) == 0);
    }

    // Light never increases as a block gets deeper, and it is bounded.
    for(int depth = 0; depth < 40; ++depth)
    {
        const int level = skyLevel(30 - depth, 30);
        CHECK(level >= 0 && level <= MaxLevel);
        if(depth > 0)
            CHECK(level <= skyLevel(30 - depth + 1, 30));
    }

    // A sky that is always open means no darkening at any depth at all, which is
    // what the flat and graph worlds ask for.
    for(int world_y = 0; world_y < 40; ++world_y)
        CHECK(skyLevel(world_y, SkyAlwaysOpen) == MaxLevel);
    CHECK(skyLevel(0, 40) == 0); // ...while a real height 40 blocks up does darken it
}

static void test_shade_curve()
{
    // Monotone, bounded, and exactly full at the top so a fully lit face is
    // shaded by its direction alone -- the surface looks the way it did before
    // per-block light existed.
    CHECK(shadeForLevel(0) == MinShade);
    CHECK(shadeForLevel(MaxLevel) == MaxShade);
    CHECK(shadeForLevel(-5) == MinShade);
    CHECK(shadeForLevel(99) == MaxShade);

    for(int level = 0; level <= MaxLevel; ++level)
    {
        const int shade = shadeForLevel(level);
        CHECK(shade >= MinShade && shade <= MaxShade);
        if(level > 0)
            CHECK(shade > shadeForLevel(level - 1));
    }

    // Combining is the same 8-bit multiply the rasteriser uses for the day/night
    // tint, and a vertex that brought no shade of its own is neutral.
    CHECK(combineShade(0, MaxLevel) == (NeutralFaceShade * MaxShade) >> 8);
    CHECK(combineShade(NeutralFaceShade, MaxLevel) == (NeutralFaceShade * MaxShade) >> 8);
    CHECK(combineShade(160, MaxLevel) == (160 * MaxShade) >> 8);
    CHECK(combineShade(160, 0) == (160 * MinShade) >> 8);
    CHECK(combineShade(255, MaxLevel) == 254); // 255*255/256, i.e. never brighter than a byte
    CHECK(combineShade(255, MaxLevel) < 255); // a full-brightness face is not overflowed

    // A darker light level always gives a darker, never a negative, shade.
    for(int face = 0; face <= 255; face += 15)
        for(int level = 0; level <= MaxLevel; ++level)
        {
            const int shade = combineShade(face, level);
            CHECK(shade >= 0 && shade <= 255);
            if(level > 0)
                CHECK(shade >= combineShade(face, level - 1));
        }
}

static void test_emitter_table_matches_the_block_ids()
{
    // The numbers in blocklight.h have to be the real block ids.
    CHECK(BlockGlowstone == BLOCK_GLOWSTONE);
    CHECK(BlockTorch == BLOCK_TORCH);
    CHECK(BlockLava == BLOCK_LAVA);
    CHECK(BlockRedstoneTorch == BLOCK_REDSTONE_TORCH);

    CHECK(emitterLevel(BLOCK_GLOWSTONE) == MaxLevel);
    CHECK(emitterLevel(BLOCK_LAVA) == MaxLevel);
    CHECK(emitterLevel(BLOCK_TORCH) == 14);
    CHECK(emitterLevel(BLOCK_REDSTONE_TORCH) == 7);
    CHECK(isEmitter(BLOCK_TORCH));
    CHECK(isEmitter(BLOCK_GLOWSTONE));

    // Everything else is dark, and in particular the blocks that shape the world
    // must not glow: a stone floor lit by itself would defeat the point.
    const BLOCK dark[] = {
        BLOCK_AIR, BLOCK_STONE, BLOCK_DIRT, BLOCK_GRASS, BLOCK_SAND, BLOCK_WATER,
        BLOCK_WOOD, BLOCK_LEAVES, BLOCK_GLASS, BLOCK_CHEST, BLOCK_FURNACE, BLOCK_COAL_ORE
    };
    for(unsigned int i = 0; i < sizeof(dark) / sizeof(dark[0]); ++i)
    {
        CHECK(!isEmitter(dark[i]));
        CHECK(emitterLevel(dark[i]) == 0);
    }

    // And the ids in the table really are distinct ids of the game.
    CHECK(BLOCK_TORCH != BLOCK_GLOWSTONE && BLOCK_TORCH != BLOCK_LAVA && BLOCK_TORCH != BLOCK_REDSTONE_TORCH);
    CHECK(BLOCK_GLOWSTONE != BLOCK_LAVA && BLOCK_GLOWSTONE != BLOCK_REDSTONE_TORCH);
    CHECK(BLOCK_LAVA != BLOCK_REDSTONE_TORCH);
}

namespace
{
    std::vector<uint8_t> blank()
    {
        return std::vector<uint8_t>(Field::Cells, 0);
    }

    /** Relaxes until nothing changes, with a hard cap so a broken field fails the test. */
    int settle(uint8_t *field)
    {
        int passes = 0;
        while(passes < 4 * Field::Cells)
        {
            ++passes;
            if(Field::relax(field) == 0)
                break;
        }
        return passes;
    }

    int manhattan(int ax, int ay, int az, int bx, int by, int bz)
    {
        return (ax > bx ? ax - bx : bx - ax) + (ay > by ? ay - by : by - ay) + (az > bz ? az - bz : bz - az);
    }
}

static void test_field_lattice()
{
    // The lattice is one cell per block, and the index layout has to agree with
    // the cube it is stored in.
    CHECK(Field::Size == 8);
    CHECK(Field::Cells == 512);
    for(int y = 0; y < Field::Size; ++y)
        for(int z = 0; z < Field::Size; ++z)
            for(int x = 0; x < Field::Size; ++x)
            {
                const int index = Field::Index(x, y, z);
                CHECK(index >= 0 && index < Field::Cells);
                CHECK(Field::inBounds(x, y, z));
            }
    CHECK(!Field::inBounds(-1, 0, 0));
    CHECK(!Field::inBounds(0, Field::Size, 0));
    CHECK(!Field::inBounds(0, 0, Field::Size));

    // Distinct cells must not alias: walk the whole cube and check every index is
    // visited exactly once.
    std::vector<int> seen(Field::Cells, 0);
    for(int y = 0; y < Field::Size; ++y)
        for(int z = 0; z < Field::Size; ++z)
            for(int x = 0; x < Field::Size; ++x)
                ++seen[Field::Index(x, y, z)];
    for(int i = 0; i < Field::Cells; ++i)
        CHECK(seen[i] == 1);

    // Planting a source and relaxing gives exactly the distance falloff: one level
    // of light lost per block walked away from it.
    std::vector<uint8_t> field = blank();
    CHECK(Field::plant(field.data(), 3, 4, 2, MaxLevel) == MaxLevel);
    CHECK(settle(field.data()) < 4 * Field::Cells);

    for(int y = 0; y < Field::Size; ++y)
        for(int z = 0; z < Field::Size; ++z)
            for(int x = 0; x < Field::Size; ++x)
            {
                const int distance = manhattan(x, y, z, 3, 4, 2);
                const int expected = distance >= MaxLevel ? 0 : (distance == 0 ? MaxLevel : MaxLevel - distance);
                CHECK(Field::at(field.data(), x, y, z) == expected);
            }

    CHECK(Field::brightest(field.data()) == MaxLevel);

    // A weaker source spreads less far, and two sources take the brighter of the
    // two rather than adding up.
    std::vector<uint8_t> weak = blank();
    Field::plant(weak.data(), 0, 0, 0, 7);
    settle(weak.data());
    CHECK(Field::at(weak.data(), 0, 0, 0) == 7);
    CHECK(Field::at(weak.data(), 3, 0, 0) == 4);
    CHECK(Field::at(weak.data(), 7, 0, 0) == 0);

    Field::plant(weak.data(), 3, 0, 0, 15);
    settle(weak.data());
    CHECK(Field::at(weak.data(), 3, 0, 0) == 15);
    CHECK(Field::at(weak.data(), 0, 0, 0) == 12); // three blocks from the brighter source
    CHECK(Field::brightest(weak.data()) == 15);

    // An empty field costs one pass and stops: that is what makes a chunk with no
    // light in it free.
    std::vector<uint8_t> empty = blank();
    CHECK(Field::relax(empty.data()) == 0);
    CHECK(Field::brightest(empty.data()) == 0);

    // Light is clipped to the lattice instead of being planted out of bounds, and
    // a level above the maximum is clamped rather than wrapping around a byte.
    CHECK(Field::plant(empty.data(), -1, 0, 0, 15) == 0);
    CHECK(Field::plant(empty.data(), Field::Size, 0, 0, 15) == 0);
    CHECK(Field::at(empty.data(), -1, 0, 0) == 0);
    CHECK(Field::at(empty.data(), Field::Size, 0, 0) == 0);
    CHECK(Field::plant(empty.data(), 1, 1, 1, 99) == MaxLevel);
    CHECK(Field::at(empty.data(), 1, 1, 1) == MaxLevel);

    // Clearing wipes it, and the null field is handled rather than crashing.
    Field::clear(empty.data());
    CHECK(Field::brightest(empty.data()) == 0);
    CHECK(Field::at(empty.data(), 1, 1, 1) == 0);
    Field::clear(nullptr);
    CHECK(Field::plant(nullptr, 0, 0, 0, 15) == 0);
    CHECK(Field::relax(nullptr) == 0);
    CHECK(Field::at(nullptr, 0, 0, 0) == 0);

    // It converges from two sources as well: a light at the corner and one in the
    // middle both end up where they should.
    std::vector<uint8_t> two = blank();
    Field::plant(two.data(), 0, 0, 0, 14);
    Field::plant(two.data(), 7, 7, 7, 10);
    settle(two.data());
    CHECK(Field::at(two.data(), 0, 0, 0) == 14);
    CHECK(Field::at(two.data(), 7, 7, 7) == 10);
    CHECK(Field::at(two.data(), 1, 0, 0) == 13);
    CHECK(Field::at(two.data(), 6, 7, 7) == 9);
}

// Not a correctness test: a cost check, so that a change that turns the light
// field into something quadratic shows up here instead of on the calculator.
// The CX runs at about 1/100 of this host, so the printed number is the shape of
// the answer, not the answer itself.
static void test_field_cost()
{
    const int rebuilds = 500;
    std::vector<uint8_t> field = blank();

    const clock_t empty_start = clock();
    for(int i = 0; i < rebuilds; ++i)
    {
        Field::clear(field.data());
        Field::relax(field.data()); // a chunk with no light in it: one pass, then it stops
    }
    const clock_t empty_end = clock();

    const clock_t lit_start = clock();
    for(int i = 0; i < rebuilds; ++i)
    {
        Field::clear(field.data());
        Field::plant(field.data(), 4, 4, 4, 14); // a torch, the worst realistic case
        settle(field.data());
    }
    const clock_t lit_end = clock();

    const double per_rebuild_us = 1000000.0 * static_cast<double>(lit_end - lit_start) / CLOCKS_PER_SEC / rebuilds;
    const double per_empty_us = 1000000.0 * static_cast<double>(empty_end - empty_start) / CLOCKS_PER_SEC / rebuilds;

    printf("    light field: %.1f us per lit chunk, %.1f us per unlit chunk\n", per_rebuild_us, per_empty_us);

    CHECK(per_rebuild_us < 5000.0); // loose: catches an accidental exponential
    CHECK(per_empty_us < 200.0);
}

int main()
{
    printf("blocklight_test\n");

    test_sky_level_falls_off_with_depth();
    test_shade_curve();
    test_emitter_table_matches_the_block_ids();
    test_field_lattice();
    test_field_cost();

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
