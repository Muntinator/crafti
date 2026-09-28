// Host tests for the snow cover rules (snowcover.cpp).
//
// The rules are pure, so the whole of them can be walked through here: the depth
// a step grows, the depth it melts, the cadence implied by the constants, what a
// layer can rest on, and where a step's columns are picked. The block ids the
// module keeps as numbers are checked against terrain.h, so a change to the id
// table cannot silently turn "snow cannot sit on a torch" into "snow sits on
// whatever a torch's id became".
//
// Build and run with `make -C tests`.

#include "snowcover.h"

#include "terrain.h"

#include <stdio.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

namespace
{
    void testGrowth()
    {
        // Cold and snowing: one step per call, no jumps, and it stops at the cap.
        int layers = 0;
        for(int i = 1; i <= SnowCover::MaxLayers; ++i)
        {
            const int next = SnowCover::step(layers, true, true);
            CHECK(next == layers + 1);
            layers = next;
        }
        CHECK(layers == SnowCover::MaxLayers);

        // A thousand more steps of the same weather cannot make it deeper.
        for(int i = 0; i < 1000; ++i)
            CHECK(SnowCover::step(layers, true, true) == SnowCover::MaxLayers);

        // Cold and dry: whatever depth is there stays there.
        for(int depth = 0; depth <= SnowCover::MaxLayers; ++depth)
            CHECK(SnowCover::step(depth, false, true) == depth);

        // Every step moves the depth by at most one, in either direction.
        for(int depth = -3; depth <= SnowCover::MaxLayers + 3; ++depth)
            for(int snowing = 0; snowing <= 1; ++snowing)
                for(int freezing = 0; freezing <= 1; ++freezing)
                {
                    const int next = SnowCover::step(depth, snowing != 0, freezing != 0);
                    CHECK(next >= 0 && next <= SnowCover::MaxLayers);
                    const int delta = next - (depth < 0 ? 0 : (depth > SnowCover::MaxLayers ? SnowCover::MaxLayers : depth));
                    CHECK(delta <= 1 && delta >= -1);
                }
    }

    void testMelt()
    {
        // A warm place loses one step per step, and snow that was carried there
        // (or placed by hand) does not survive: that is what keeps a desert bare
        // and what eventually clears a drift off a roof.
        int layers = SnowCover::MaxLayers;
        for(int i = 0; i < SnowCover::MaxLayers; ++i)
        {
            const int next = SnowCover::step(layers, false, false);
            CHECK(next == layers - 1);
            layers = next;
        }
        CHECK(layers == 0);
        // Having melted away, it stays gone however long the warmth lasts.
        for(int i = 0; i < 100; ++i)
            CHECK(SnowCover::step(0, false, false) == 0);

        // Snowing in a warm place still melts: a flake that lands where the air is
        // above freezing does not build a layer.
        CHECK(SnowCover::step(0, true, false) == 0);
        CHECK(SnowCover::step(2, true, false) == 1);

        // Cold is what keeps it: the same depth with the same weather but a
        // freezing temperature grows instead of shrinking.
        CHECK(SnowCover::step(2, true, true) == 3);
        CHECK(SnowCover::step(2, false, false) == 1);
    }

    void testDepthFitsTheBlockData()
    {
        // The depth is stored in the block's data byte, which terrain.h packs with
        // a 7-bit mask that bit 15 (the redstone power flag) shares a field with.
        CHECK(SnowCover::MaxLayers <= 0x7F);
        CHECK(SnowCover::MaxLayers >= 1);

        // Half a block at the deepest, which is what makes every layer a slab the
        // player walks through rather than a cube that traps them (snowrenderer.h).
        CHECK(SnowCover::LayerSixteenths * SnowCover::MaxLayers == 8);
        CHECK(SnowCover::LayerSixteenths > 0);

        // layersOf() is the readable form of the data byte: a layer placed without
        // a depth still shows something, and a nonsense depth cannot overdraw.
        CHECK(SnowCover::layersOf(0) == 1);
        for(int depth = 1; depth <= SnowCover::MaxLayers; ++depth)
            CHECK(SnowCover::layersOf(static_cast<uint8_t>(depth)) == depth);
        CHECK(SnowCover::layersOf(static_cast<uint8_t>(SnowCover::MaxLayers + 1)) == SnowCover::MaxLayers);
        CHECK(SnowCover::layersOf(127) == SnowCover::MaxLayers);
    }

    /**
     * A column of a test world: a table of blocks by world height, so the step
     * rule can be walked over exactly the columns the game will hand it -- ground,
     * an existing drift, a roof, a torch in the cell, the top of the world.
     */
    struct TestWorld
    {
        static constexpr int Top = 40; // World::HEIGHT * Chunk::SIZE
        uint8_t column[Top];
        uint8_t data[Top];
        int reads = 0;

        TestWorld(uint8_t fill = SnowCover::BlockAir)
        {
            for(int i = 0; i < Top; ++i)
            {
                column[i] = fill;
                data[i] = 0;
            }
        }

        void set(const int y, const uint8_t block, const uint8_t block_data = 0)
        {
            column[y] = block;
            data[y] = block_data;
        }

        SnowCover::Block read(const int y) const
        {
            ++const_cast<TestWorld *>(this)->reads;
            if(y < 0 || y >= Top)
                return SnowCover::makeBlock(SnowCover::BlockAir, 0);
            return SnowCover::makeBlock(column[y], data[y]);
        }
    };

    /** Runs one step over a test column. */
    int stepColumn(const TestWorld &world, const int feet_y, const bool snowing, const bool freezing, int &cover_y)
    {
        return SnowCover::columnStep(feet_y, TestWorld::Top,
            [&world](const int y) { return world.read(y); }, snowing, freezing, cover_y);
    }

    void testColumnStep()
    {
        constexpr int Feet = 21; // the player stands on the block at y = 20

        // A grassy column with open sky: the first step lays one layer on it.
        {
            TestWorld world;
            world.set(20, 20); // BLOCK_GRASS

            int cover_y = -1;
            CHECK(stepColumn(world, Feet, true, true, cover_y) == 1);
            CHECK(cover_y == 21);
        }

        // An existing drift grows in place: it is skipped as ground, so the write
        // stays in its own cell instead of stacking a new block on top of it. This
        // is the difference between a drift deepening and the world growing a white
        // column one block per second.
        {
            TestWorld world;
            world.set(20, 20);
            world.set(21, SnowCover::BlockSnow, 2);

            int cover_y = -1;
            CHECK(stepColumn(world, Feet, true, true, cover_y) == 3);
            CHECK(cover_y == 21);
        }

        // ...and the same from the top of a deeper drift, however thick it is.
        {
            TestWorld world;
            world.set(20, 20);
            world.set(21, SnowCover::BlockSnow, 4);
            world.set(22, SnowCover::BlockSnow, 4);

            int cover_y = -1;
            // Already at the cap: nothing to do at all.
            CHECK(stepColumn(world, 23, true, true, cover_y) == -1);
            CHECK(cover_y == -1);

            // Two steps deep grows the *lowest* layer, not a new one above it.
            TestWorld shallow;
            shallow.set(20, 20);
            shallow.set(21, SnowCover::BlockSnow, 1);
            shallow.set(22, SnowCover::BlockSnow, 1);
            CHECK(stepColumn(shallow, 23, true, true, cover_y) == 2);
            CHECK(cover_y == 21);
        }

        // A drift in a warm place melts even though it is standing in its own way:
        // the sky scan looks above the layer, not at it. This is the bug the pure
        // rule exists to prevent.
        {
            TestWorld world;
            world.set(20, 20);
            world.set(21, SnowCover::BlockSnow, 1);

            int cover_y = -1;
            CHECK(stepColumn(world, 22, false, false, cover_y) == 0);
            CHECK(cover_y == 21);
        }

        // Under a roof: nothing is added and nothing melts, whatever the weather.
        {
            TestWorld world;
            world.set(20, 20);
            world.set(22, 6); // planks over the cell

            int cover_y = -1;
            CHECK(stepColumn(world, Feet, true, true, cover_y) == -1);
            CHECK(cover_y == -1);

            world.set(21, SnowCover::BlockSnow, 1);
            CHECK(stepColumn(world, Feet, false, false, cover_y) == -1);
            CHECK(cover_y == -1);
        }

        // A cell that holds something else is left alone: the game must not bury a
        // torch, a flower or a block the player put there.
        {
            TestWorld world;
            world.set(20, 20);
            world.set(21, SnowCover::BlockTorch);

            int cover_y = -1;
            CHECK(stepColumn(world, 22, true, true, cover_y) == -1);
            CHECK(cover_y == -1);
        }

        // Water and lava are not surfaces: water is a fluid and lava would melt the
        // snow, so a flooded column gets nothing.
        {
            TestWorld water;
            water.set(20, SnowCover::BlockWater);

            int cover_y = -1;
            CHECK(stepColumn(water, Feet, true, true, cover_y) == -1);
            CHECK(cover_y == -1);

            TestWorld lava;
            lava.set(20, SnowCover::BlockLava);
            CHECK(stepColumn(lava, Feet, true, true, cover_y) == -1);
        }

        // A player in the air gets no snow: there is no ground within reach.
        {
            TestWorld world;
            int cover_y = -1;
            CHECK(stepColumn(world, TestWorld::Top - 1, true, true, cover_y) == -1);
            CHECK(cover_y == -1);
        }

        // The ground search only reaches MaxGroundDrop blocks: a player hovering
        // above the ground does not snow on it from a distance.
        {
            TestWorld world;
            world.set(20, 20);

            int cover_y = -1;
            const int just_in_reach = 20 + SnowCover::MaxGroundDrop + 1;
            CHECK(stepColumn(world, just_in_reach, true, true, cover_y) == 1);
            CHECK(cover_y == 21);
            CHECK(stepColumn(world, just_in_reach + 1, true, true, cover_y) == -1);
            CHECK(cover_y == -1);

            // A column whose ground is the ceiling has nowhere to put a layer.
            TestWorld ceiling;
            ceiling.set(TestWorld::Top - 1, 20);
            CHECK(stepColumn(ceiling, TestWorld::Top, true, true, cover_y) == -1);
        }

        // A column already at its deepest is left alone rather than reported as a
        // change, so an idle snowfall writes nothing at all.
        {
            TestWorld world;
            world.set(20, 20);
            world.set(21, SnowCover::BlockSnow, SnowCover::MaxLayers);

            int cover_y = -1;
            CHECK(stepColumn(world, Feet, true, true, cover_y) == -1);
            CHECK(cover_y == -1);
            // ...but the same column still melts, one step at a time.
            CHECK(stepColumn(world, Feet, false, false, cover_y) == SnowCover::MaxLayers - 1);
            CHECK(cover_y == 21);
        }

        // A covered column costs a bounded number of reads: the scan stops at the
        // first opaque block, it does not walk to the sky.
        {
            TestWorld world;
            world.set(20, 20);
            world.set(24, 6);

            int cover_y = -1;
            stepColumn(world, Feet, true, true, cover_y);
            CHECK(world.reads <= SnowCover::MaxSkyScan + SnowCover::MaxGroundDrop + 2);
            CHECK(world.reads < SnowCover::MaxSkyScan);
        }
    }

    void testBlockIds()
    {
        // The ids this module was written against, in the engine's own spelling.
        CHECK(SnowCover::BlockAir == BLOCK_AIR);
        CHECK(SnowCover::BlockWater == BLOCK_WATER);
        CHECK(SnowCover::BlockWaterFast == BLOCK_WATER_FAST);
        CHECK(SnowCover::BlockLava == BLOCK_LAVA);
        CHECK(SnowCover::BlockTorch == BLOCK_TORCH);
        CHECK(SnowCover::BlockRedstoneTorch == BLOCK_REDSTONE_TORCH);
        CHECK(SnowCover::BlockRedstoneWire == BLOCK_REDSTONE_WIRE);
        CHECK(SnowCover::BlockPressurePlate == BLOCK_PRESSURE_PLATE);
        CHECK(SnowCover::BlockFlower == BLOCK_FLOWER);
        CHECK(SnowCover::BlockMushroom == BLOCK_MUSHROOM);
        CHECK(SnowCover::BlockWheat == BLOCK_WHEAT);
        CHECK(SnowCover::BlockSpiderweb == BLOCK_SPIDERWEB);
        CHECK(SnowCover::BlockItem == BLOCK_ITEM);

        // The snow block exists, is a normal block (so the renderer registry and
        // the block names cover it), and is not an emitter or a fluid.
        CHECK(BLOCK_SNOW > BLOCK_AIR);
        CHECK(BLOCK_SNOW <= BLOCK_NORMAL_LAST);
        CHECK(SnowCover::layersOf(SnowCover::MaxLayers) <= 127);
        CHECK(SnowCover::BlockSnow == BLOCK_SNOW);

        // The packing this header reads the world through has to be terrain.h's,
        // because the world's blocks are handed to columnStep() unconverted.
        CHECK(SnowCover::blockId(getBLOCKWDATA(BLOCK_SNOW, 3)) == BLOCK_SNOW);
        CHECK(SnowCover::blockData(getBLOCKWDATA(BLOCK_SNOW, 3)) == 3);
        CHECK(SnowCover::blockId(getBLOCKWDATAPower(BLOCK_FURNACE, 5, true)) == BLOCK_FURNACE);
        CHECK(SnowCover::blockData(getBLOCKWDATAPower(BLOCK_FURNACE, 5, true)) == 5);
        CHECK(SnowCover::blockId(SnowCover::makeBlock(BLOCK_SNOW, 4)) == BLOCK_SNOW);
        CHECK(SnowCover::blockData(SnowCover::makeBlock(BLOCK_SNOW, 4)) == 4);
        CHECK(SnowCover::makeBlock(BLOCK_SNOW, 4) == getBLOCKWDATA(BLOCK_SNOW, 4));
    }

    void testWhatSnowCanRestOn()
    {
        // Precipitation falls through these, so they cannot carry a layer.
        const uint8_t permeable[] = {
            BLOCK_AIR, BLOCK_WATER, BLOCK_WATER_FAST, BLOCK_LAVA, BLOCK_TORCH,
            BLOCK_REDSTONE_TORCH, BLOCK_REDSTONE_WIRE, BLOCK_PRESSURE_PLATE,
            BLOCK_FLOWER, BLOCK_MUSHROOM, BLOCK_WHEAT, BLOCK_SPIDERWEB, BLOCK_ITEM
        };
        for(unsigned int i = 0; i < sizeof(permeable) / sizeof(permeable[0]); ++i)
        {
            CHECK(SnowCover::isPermeable(permeable[i]));
            // ...and lava is the one permeable block that is not a surface either,
            // so snow cannot sit on it without melting: the support rule excludes
            // it explicitly rather than by omission.
            CHECK(!SnowCover::supportsSnow(permeable[i]));
        }

        // Everything the ground is made of carries snow.
        const uint8_t solid[] = {
            BLOCK_STONE, BLOCK_DIRT, BLOCK_GRASS, BLOCK_SAND, BLOCK_COBBLESTONE,
            BLOCK_WOOD, BLOCK_LEAVES, BLOCK_PLANKS_NORMAL, BLOCK_PLANKS_DARK,
            BLOCK_PLANKS_BRIGHT, BLOCK_GLASS, BLOCK_WOOL_WHITE, BLOCK_GLOWSTONE,
            BLOCK_NETHERRACK, BLOCK_BEDROCK, BLOCK_IRON, BLOCK_DIAMOND,
            46, // the bed: a block added long after this module was written
            BLOCK_SNOW
        };
        for(unsigned int i = 0; i < sizeof(solid) / sizeof(solid[0]); ++i)
        {
            CHECK(!SnowCover::isPermeable(solid[i]));
            CHECK(SnowCover::supportsSnow(solid[i]));
        }

        // The list is a list of what lets water through, so a block id this game
        // does not have yet counts as solid cover: the safe direction to be wrong.
        CHECK(!SnowCover::isPermeable(200));
        CHECK(SnowCover::supportsSnow(200));
        CHECK(!SnowCover::isPermeable(255));
    }

    void testColumnPicks()
    {
        const uint32_t seed = 0x5e00d7u;
        const int span = SnowCover::Radius * 2 + 1;

        // Deterministic, and inside the advertised radius.
        for(unsigned long long step = 0; step < 2000; ++step)
            for(int index = 0; index < SnowCover::ColumnsPerStep; ++index)
            {
                int dx = 0, dz = 0;
                SnowCover::columnOffset(seed, step, index, dx, dz);

                int again_dx = 0, again_dz = 0;
                SnowCover::columnOffset(seed, step, index, again_dx, again_dz);
                CHECK(dx == again_dx && dz == again_dz);

                CHECK(dx >= -SnowCover::Radius && dx <= SnowCover::Radius);
                CHECK(dz >= -SnowCover::Radius && dz <= SnowCover::Radius);
            }

        // Every offset is reachable, and both signs are common: snow has to settle
        // all around the player rather than in one corner of the ring.
        int dx_hits[64] = {0}, dz_hits[64] = {0};
        int positives = 0, negatives = 0, same = 0, total = 0;
        long long sum_abs_dx = 0;
        int distinct_columns = 0;

        const int steps = 4000;
        for(unsigned long long step = 0; step < static_cast<unsigned long long>(steps); ++step)
        {
            int previous_dx = 0, previous_dz = 0;
            bool all_distinct = true;

            for(int index = 0; index < SnowCover::ColumnsPerStep; ++index)
            {
                int dx = 0, dz = 0;
                SnowCover::columnOffset(seed, step, index, dx, dz);

                CHECK(dx + SnowCover::Radius < 64 && dz + SnowCover::Radius < 64);
                ++dx_hits[dx + SnowCover::Radius];
                ++dz_hits[dz + SnowCover::Radius];

                if(dx > 0 || dz > 0)
                    ++positives;
                if(dx < 0 || dz < 0)
                    ++negatives;
                if(dx == dz)
                    ++same;
                sum_abs_dx += dx < 0 ? -dx : dx;
                ++total;

                if(index > 0 && dx == previous_dx && dz == previous_dz)
                    all_distinct = false;
                previous_dx = dx;
                previous_dz = dz;
            }

            if(all_distinct)
                ++distinct_columns;
        }

        for(int i = 0; i < span; ++i)
        {
            CHECK(dx_hits[i] > 0);
            CHECK(dz_hits[i] > 0);
        }

        printf("    columns: %d offsets, %d positives, %d negatives, %d on the diagonal, mean |dx| %.2f\n",
               total, positives, negatives, same, static_cast<double>(sum_abs_dx) / total);

        CHECK(positives > total / 4);
        CHECK(negatives > total / 4);
        // The two halves of the hash are independent, so landing on the diagonal is
        // as unlikely as one offset in the span -- not the ~100% that reusing one
        // value for both would give.
        CHECK(same < total / 8);
        // ...and the ring is not biased outwards: the mean distance from the player
        // is about half the radius, which an outward-skewed hash would overshoot.
        const double mean_abs_dx = static_cast<double>(sum_abs_dx) / total;
        CHECK(mean_abs_dx > SnowCover::Radius * 0.35);
        CHECK(mean_abs_dx < SnowCover::Radius * 0.65);
        // The three columns of one step are meant to be three different places.
        CHECK(distinct_columns > steps * 9 / 10);

        // A different seed puts the snow somewhere else.
        int other_dx = 0, other_dz = 0;
        int a_dx = 0, a_dz = 0;
        SnowCover::columnOffset(0x11111111u, 7, 0, a_dx, a_dz);
        SnowCover::columnOffset(0x22222222u, 7, 0, other_dx, other_dz);
        CHECK(a_dx != other_dx || a_dz != other_dz);
    }
}

int main()
{
    printf("snowcover_test\n");

    testGrowth();
    testMelt();
    testDepthFitsTheBlockData();
    testColumnStep();
    testBlockIds();
    testWhatSnowCanRestOn();
    testColumnPicks();

    printf("snowcover_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
