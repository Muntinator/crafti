// Host tests for the Stage 2 world generation module (biomegen.cpp).
//
// The module is a pure function of (world seed, world coordinates), so all of it
// can be exercised on the host: the biome layout, the surface materials, the
// mountain boost, river carving, cave tunnels and ravine fissures. What cannot
// be tested here is how it looks on a calculator screen.
//
// The numbers below are deliberately ranges and not exact values: they exist to
// catch a field that stopped varying, a threshold that silently made a biome
// impossible, a carve that stopped finding anything, or a ravine that started
// breaking the surface. Where a range is wide it is because the field is noise
// and only its statistics are meaningful.
//
// Build and run with `make -C tests`.

#include "biomegen.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

namespace
{
    // Sampled area, in blocks. Large enough for several biome cells and several
    // ravine cells, small enough to run in well under a second.
    constexpr int SampleOrigin = -768;
    constexpr int SampleEnd = 768;
    constexpr int SampleStep = 7;

    uint32_t mix(uint32_t value)
    {
        value ^= value >> 16;
        value *= 0x7feb352du;
        value ^= value >> 15;
        value *= 0x846ca68bu;
        value ^= value >> 16;
        return value;
    }

    uint32_t hash2(int x, int z)
    {
        return mix(static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(z) * 19349663u ^ 0x51ed270bu);
    }

    /** Smooth value noise in 0..1023 on a 32-block lattice. */
    int smoothField(int x, int z)
    {
        constexpr int Shift = 5;
        constexpr int Mask = (1 << Shift) - 1;

        const auto weight = [](int f) {
            const int t = f << (8 - Shift); // 0..256
            return (t * t * (768 - 2 * t)) >> 16;
        };
        const auto lattice = [](int a, int b) {
            return static_cast<int>((hash2(a, b) & 0xFFFFu) >> 6);
        };

        const int gx = x >> Shift, gz = z >> Shift;
        const int wx = weight(x & Mask), wz = weight(z & Mask);
        const int c00 = lattice(gx, gz), c10 = lattice(gx + 1, gz);
        const int c01 = lattice(gx, gz + 1), c11 = lattice(gx + 1, gz + 1);

        const int top = c00 + (((c10 - c00) * wx) >> 8);
        const int bottom = c01 + (((c11 - c01) * wx) >> 8);
        return top + (((bottom - top) * wz) >> 8);
    }

    /**
     * A stand-in for the caller's Perlin base height.
     *
     * The real base is a smooth four-octave field that in practice spans world
     * Y 8..27, is centred just above sea level, and puts only about 12% of the
     * world at or below the water line. Reproducing that envelope keeps the
     * coverage numbers below representative without pulling nGL into the test of
     * a dependency-free module.
     */
    int baseHeight(int x, int z)
    {
        return 8 + (smoothField(x, z) * 19) / 1023; // 8..27
    }

    struct Stats
    {
        int columns = 0;
        int biome_columns[BiomeGen::BiomeCount] = {};
        int surface_blocks[256] = {};
        int max_height_step = 0;
        int river_columns = 0;
        int deepest_river = 0;
        int trees_expected = 0;
    };

    Stats scan(uint32_t seed)
    {
        Stats stats;

        for(int x = SampleOrigin; x < SampleEnd; x += SampleStep)
            for(int z = SampleOrigin; z < SampleEnd; z += SampleStep)
            {
                const int base = baseHeight(x, z);

                BiomeGen::Column column;
                BiomeGen::columnAt(seed, x, z, base, column);

                ++stats.columns;
                ++stats.biome_columns[column.biome];
                ++stats.surface_blocks[column.surface];

                // The height must always stay inside the terrain envelope, and
                // the biome must agree with the height it ended up with.
                CHECK(column.height >= BiomeGen::MinHeight);
                CHECK(column.height <= BiomeGen::MaxHeight);
                if(column.height < BiomeGen::SeaLevel)
                    CHECK(column.biome == BiomeGen::BiomeOcean);
                else if(column.height <= BiomeGen::SeaLevel + 1)
                    CHECK(column.biome == BiomeGen::BiomeBeach);
                else
                    CHECK(column.biome != BiomeGen::BiomeOcean && column.biome != BiomeGen::BiomeBeach);

                const int river = BiomeGen::riverDepth(seed, x, z);
                CHECK(river >= 0);
                CHECK(river <= 4);
                if(river > 0)
                    ++stats.river_columns;
                if(river > stats.deepest_river)
                    stats.deepest_river = river;

                stats.trees_expected += BiomeGen::treeDensityPercent(column.biome);

                // Neighbouring columns must not differ by a wall. The base
                // field has its own slope, so measure only what this module
                // adds on top of it (mountains and rivers).
                BiomeGen::Column right;
                const int base_right = baseHeight(x + 1, z);
                BiomeGen::columnAt(seed, x + 1, z, base_right, right);
                const int added = right.height - column.height;
                const int base_added = base_right - base;
                const int excess = added > base_added ? added - base_added : base_added - added;
                if(excess > stats.max_height_step)
                    stats.max_height_step = excess;

                // biomeAt() is a wrapper around the same computation.
                CHECK(BiomeGen::biomeAt(seed, x, z, base) == column.biome);
            }

        return stats;
    }

    void testDeterminism()
    {
        // The same query twice, and the same query in a different order, must
        // give the same answer: the module keeps no state at all.
        const uint32_t seed = 0x1234abcd;

        BiomeGen::Column first_a, first_b, second_a, second_b;
        BiomeGen::columnAt(seed, -1234, 5678, 20, first_a);
        BiomeGen::columnAt(seed, 999, -999, 8, first_b);
        // Same two queries, other order: the module holds no state, so the
        // answers may not depend on what was asked before.
        BiomeGen::columnAt(seed, 999, -999, 8, second_b);
        BiomeGen::columnAt(seed, -1234, 5678, 20, second_a);
        CHECK(first_a.biome == second_a.biome);
        CHECK(first_a.height == second_a.height);
        CHECK(first_a.surface == second_a.surface);
        CHECK(first_a.subsurface == second_a.subsurface);
        CHECK(first_b.biome == second_b.biome);
        CHECK(first_b.height == second_b.height);
        CHECK(first_b.surface == second_b.surface);

        // Caves and ravines too.
        for(int i = 0; i < 64; ++i)
        {
            const int x = -200 + i * 7, y = 3 + (i % 25), z = 500 - i * 3;
            const bool cave = BiomeGen::caveAt(seed, x, y, z);
            BiomeGen::Ravine ravine, again;
            BiomeGen::ravineAt(seed, x, z, 30, ravine);
            BiomeGen::ravineAt(seed, x, z, 30, again);
            CHECK(cave == BiomeGen::caveAt(seed, x, y, z));
            CHECK(ravine.present == again.present);
            CHECK(ravine.lowest == again.lowest);
            CHECK(ravine.highest == again.highest);
        }

        // A different seed has to give a different world, or the seed is unused.
        int differences = 0;
        for(int i = 0; i < 256; ++i)
        {
            BiomeGen::Column one, two;
            const int x = i * 13 - 1500, z = i * 29 + 300;
            BiomeGen::columnAt(0xaaaa1111u, x, z, baseHeight(x, z), one);
            BiomeGen::columnAt(0x5555eee2u, x, z, baseHeight(x, z), two);
            if(one.height != two.height || one.biome != two.biome)
                ++differences;
        }
        CHECK(differences > 40);
    }

    void testBiomeCoverage()
    {
        const Stats stats = scan(0x0badc0de);

        CHECK(stats.columns > 40000);

        // Every biome has to be reachable, and no land biome may swallow the
        // world. The bounds are wide: this is noise, the point is reachability.
        for(int biome = 0; biome < BiomeGen::BiomeCount; ++biome)
            CHECK(stats.biome_columns[biome] > 0);

        const int land = stats.columns - stats.biome_columns[BiomeGen::BiomeOcean] - stats.biome_columns[BiomeGen::BiomeBeach];
        CHECK(land > stats.columns / 4);

        for(int biome = BiomeGen::BiomeDesert; biome <= BiomeGen::BiomeMountains; ++biome)
        {
            const int share = (stats.biome_columns[biome] * 100) / land;
            printf("    %-10s %2d%% of land (%d columns)\n", BiomeGen::biomeName(biome), share, stats.biome_columns[biome]);
            CHECK(share >= 2);
            CHECK(share <= 75);
        }

        const int mountains = (stats.biome_columns[BiomeGen::BiomeMountains] * 100) / land;
        CHECK(mountains <= 25); // mountains are a landmark, not the default
    }

    void testSurfaceMaterials()
    {
        const uint32_t seed = 0x5eed1234;

        for(int x = -400; x < 400; x += 11)
            for(int z = -400; z < 400; z += 11)
            {
                BiomeGen::Column column;
                BiomeGen::columnAt(seed, x, z, baseHeight(x, z), column);

                switch(column.biome)
                {
                case BiomeGen::BiomeOcean:
                case BiomeGen::BiomeBeach:
                case BiomeGen::BiomeDesert:
                    CHECK(column.surface == BiomeGen::BlockSand);
                    CHECK(column.subsurface == BiomeGen::BlockSand);
                    break;
                case BiomeGen::BiomeMountains:
                    if(column.height >= 24)
                    {
                        CHECK(column.surface == BiomeGen::BlockStone);
                        CHECK(column.subsurface == BiomeGen::BlockStone);
                    }
                    else
                        CHECK(column.surface == BiomeGen::BlockGrass);
                    break;
                case BiomeGen::BiomePlains:
                case BiomeGen::BiomeForest:
                case BiomeGen::BiomeSavanna:
                    // Grass above the water line, sand in a river bed below it.
                    CHECK(column.surface == BiomeGen::BlockGrass || column.surface == BiomeGen::BlockSand);
                    CHECK(column.subsurface == BiomeGen::BlockDirt || column.subsurface == BiomeGen::BlockSand);
                    break;
                default:
                    CHECK(false);
                    break;
                }
            }

        // Grass is the common land surface and sand the common wet one.
        const Stats stats = scan(0x5eed1234);
        CHECK(stats.surface_blocks[BiomeGen::BlockGrass] > 0);
        CHECK(stats.surface_blocks[BiomeGen::BlockSand] > 0);
        CHECK(stats.surface_blocks[BiomeGen::BlockStone] > 0);
        CHECK(stats.surface_blocks[BiomeGen::BlockDirt] == 0); // dirt is never a surface
    }

    void testMountainsRaiseTheGround()
    {
        // Somewhere in the sampled area the mountain field has to have lifted a
        // column well above its base height, otherwise "improved mountains" is
        // just a colour change.
        const uint32_t seed = 0x11112222;
        int boosted_columns = 0;
        int tallest_boost = 0;

        for(int x = -768; x < 768; x += 3)
            for(int z = -768; z < 768; z += 3)
            {
                const int base = baseHeight(x, z);
                BiomeGen::Column column;
                BiomeGen::columnAt(seed, x, z, base, column);
                if(column.biome != BiomeGen::BiomeMountains)
                    continue;
                ++boosted_columns;
                const int boost = column.height - base;
                if(boost > tallest_boost)
                    tallest_boost = boost;
            }

        printf("    mountain columns: %d, largest lift above base: %d\n", boosted_columns, tallest_boost);
        CHECK(boosted_columns > 1000);
        CHECK(tallest_boost >= 6);
        CHECK(tallest_boost <= 16);
    }

    void testRivers()
    {
        // Rivers must exist, must be a minority of the world and must have a
        // shallow bank fading into a deeper middle.
        const uint32_t seed = 0x0ddba11;
        int counts[8] = {};
        int columns = 0;

        for(int x = -1024; x < 1024; x += 3)
            for(int z = -1024; z < 1024; z += 3)
            {
                const int depth = BiomeGen::riverDepth(seed, x, z);
                CHECK(depth >= 0 && depth <= 4);
                ++counts[depth];
                ++columns;
            }

        const int wet = counts[1] + counts[2] + counts[3] + counts[4];
        printf("    river columns: %d of %d (%.1f%%), deepest %d\n",
               wet, columns, 100.0 * wet / columns, counts[4] > 0 ? 4 : (counts[3] > 0 ? 3 : 2));
        CHECK(wet > 0);
        CHECK(wet < columns / 5); // rivers are not the terrain
        CHECK(counts[4] > 0);     // and they get deep somewhere

        // Adjacent columns step by at most one block, or the "river" would be a
        // trench with vertical walls.
        for(int x = -600; x < 600; x += 5)
            for(int z = -600; z < 600; z += 5)
            {
                const int here = BiomeGen::riverDepth(seed, x, z);
                const int right = BiomeGen::riverDepth(seed, x + 1, z);
                const int step = right > here ? right - here : here - right;
                CHECK(step <= 1);
            }
    }

    void testHeightIsContinuous()
    {
        // The module's own contribution to a neighbour step, i.e. how much of
        // the difference the mountains and rivers are responsible for.
        const Stats stats = scan(0x77aa33cc);
        printf("    largest neighbour height step added by biomes/rivers: %d\n", stats.max_height_step);
        CHECK(stats.max_height_step <= 2);
    }

    void testTrees()
    {
        // Forests are the dense biome, plains are sparser and the rest are bare,
        // which is what makes a forest look like a forest.
        CHECK(BiomeGen::treeDensityPercent(BiomeGen::BiomeForest) > BiomeGen::treeDensityPercent(BiomeGen::BiomePlains));
        CHECK(BiomeGen::treeDensityPercent(BiomeGen::BiomePlains) >= BiomeGen::treeDensityPercent(BiomeGen::BiomeSavanna));
        CHECK(BiomeGen::treeDensityPercent(BiomeGen::BiomeDesert) == 0);
        CHECK(BiomeGen::treeDensityPercent(BiomeGen::BiomeOcean) == 0);
        CHECK(BiomeGen::treeDensityPercent(BiomeGen::BiomePlains) > 0);

        // A canopy is 7x7 blocks, so a forest has to stay around one tree per
        // twenty columns or the whole biome is a solid roof of leaves.
        CHECK(BiomeGen::treeDensityPercent(BiomeGen::BiomeForest) <= 10);
    }

    void testCaves()
    {
        const uint32_t seed = 0xcafe1e;

        // Carve coverage over the underground volume. Too low and there is
        // nothing to explore; too high and the underground is a sponge.
        int sampled = 0, carved = 0;
        int deepest = 0;

        for(int x = -300; x < 300; x += 3)
            for(int z = -300; z < 300; z += 3)
            {
                const int base = baseHeight(x, z);
                BiomeGen::Column column;
                BiomeGen::columnAt(seed, x, z, base, column);
                for(int y = 2; y < column.height - 1; ++y)
                {
                    ++sampled;
                    if(BiomeGen::caveAt(seed, x, y, z))
                    {
                        ++carved;
                        if(y > deepest)
                            deepest = y;
                    }
                }
            }

        const double coverage = 100.0 * carved / sampled;
        printf("    cave coverage: %.2f%% of underground blocks (%d samples), highest carved y %d\n",
               coverage, sampled, deepest);
        CHECK(sampled > 10000);
        CHECK(coverage > 0.5);
        CHECK(coverage < 25.0);

        // Caves must be connected enough to walk through, not isolated pores:
        // a tunnel column should usually have a carved neighbour vertically.
        int carve_columns = 0, connected = 0;
        for(int x = -200; x < 200; x += 2)
            for(int z = -200; z < 200; z += 2)
                for(int y = 3; y < 30; ++y)
                    if(BiomeGen::caveAt(seed, x, y, z))
                    {
                        ++carve_columns;
                        if(BiomeGen::caveAt(seed, x, y + 1, z) || BiomeGen::caveAt(seed, x + 1, y, z)
                           || BiomeGen::caveAt(seed, x, y, z + 1) || BiomeGen::caveAt(seed, x, y - 1, z))
                            ++connected;
                    }

        printf("    connected carved blocks: %d of %d (%.1f%%)\n",
               connected, carve_columns, carve_columns ? 100.0 * connected / carve_columns : 0.0);
        CHECK(carve_columns > 500);
        CHECK(connected * 100 > carve_columns * 90);
    }

    void testRavines()
    {
        const uint32_t seed = 0xdead1234;

        int columns = 0, ravine_columns = 0, checked_surface = 0;
        int lowest_seen = 999, highest_seen = -1;

        // Cover several ravine cells (256 blocks each) so cell borders and
        // segments running into a neighbouring cell are exercised too.
        for(int x = -1024; x < 1024; x += 3)
            for(int z = -1024; z < 1024; z += 3)
            {
                const int base = baseHeight(x, z);
                BiomeGen::Column column;
                BiomeGen::columnAt(seed, x, z, base, column);
                ++columns;

                BiomeGen::Ravine ravine;
                BiomeGen::ravineAt(seed, x, z, column.height, ravine);
                if(!ravine.present)
                    continue;

                ++ravine_columns;
                CHECK(ravine.lowest >= 5);
                CHECK(ravine.highest >= ravine.lowest);
                // The forbidden case: a ravine that reaches the surface block
                // would open the sky and could drain a lake or the ocean.
                CHECK(ravine.highest <= column.height - 1);
                if(column.height > 12)
                {
                    ++checked_surface;
                    CHECK(ravine.highest < column.height);
                }
                CHECK(ravine.strength > 0 && ravine.strength <= 1024);
                if(ravine.lowest < lowest_seen)
                    lowest_seen = ravine.lowest;
                if(ravine.highest > highest_seen)
                    highest_seen = ravine.highest;
            }

        printf("    ravine columns: %d of %d (%.2f%%), y range %d..%d, with room: %d\n",
               ravine_columns, columns, 100.0 * ravine_columns / columns, lowest_seen, highest_seen, checked_surface);
        CHECK(ravine_columns > 20);
        CHECK(ravine_columns < columns / 10);
        CHECK(checked_surface > 0);

        // A ravine is much taller than it is wide, which is the whole point.
        int tall = 0;
        for(int x = -400; x < 400; x += 2)
            for(int z = -400; z < 400; z += 2)
            {
                BiomeGen::Ravine ravine;
                BiomeGen::ravineAt(seed, x, z, 30, ravine);
                if(ravine.present && ravine.highest - ravine.lowest >= 8)
                    ++tall;
            }
        CHECK(tall > 0);
    }

    void testRavineNeverBreaksTheSurface()
    {
        // Sweep surface heights across the range where a fissure still fits, and
        // check the ceiling against each one. This is the invariant the caller
        // relies on to keep caves and ravines from opening the world.
        const uint32_t seed = 0x2468ace;
        int present = 0;

        for(int x = -500; x < 500; x += 2)
            for(int z = -500; z < 500; z += 2)
                for(int surface = 10; surface <= BiomeGen::MaxHeight; surface += 3)
                {
                    BiomeGen::Ravine ravine;
                    BiomeGen::ravineAt(seed, x, z, surface, ravine);
                    if(!ravine.present)
                        continue;
                    ++present;
                    CHECK(ravine.highest <= surface - 1);
                }

        CHECK(present > 0);
    }

    void testTemperatureField()
    {
        // The temperature field is what decides whether the weather rains or
        // snows and whether snow on the ground melts (weather.h, snowcover.h), so
        // it is checked against the biome it is supposed to describe: a column the
        // field calls cold is never a hot biome, and vice versa.
        const uint32_t seed = 0x7e4f00d;
        int cold = 0, warm = 0, hot = 0, sampled = 0;
        int coldest = 1024, hottest = -1;

        for(int x = -600; x < 600; x += 5)
            for(int z = -600; z < 600; z += 5)
            {
                const int temperature = BiomeGen::temperatureAt(seed, x, z);
                CHECK(temperature >= 0 && temperature <= 1023);
                // Pure: the same column always answers the same thing.
                CHECK(BiomeGen::temperatureAt(seed, x, z) == temperature);

                if(temperature < coldest)
                    coldest = temperature;
                if(temperature > hottest)
                    hottest = temperature;

                const bool is_cold = BiomeGen::isColdAt(seed, x, z);
                CHECK(is_cold == (temperature <= BiomeGen::ColdTemperature));
                if(is_cold)
                    ++cold;

                // The biome is resolved from the same field (plus height and
                // humidity), so the two can never contradict each other the way a
                // copied constant would: no desert is cold enough to snow, and no
                // cold column is a savanna.
                const BiomeGen::Column column = [&] {
                    BiomeGen::Column c;
                    BiomeGen::columnAt(seed, x, z, 20, c);
                    return c;
                }();
                if(is_cold)
                    CHECK(column.biome != BiomeGen::BiomeDesert && column.biome != BiomeGen::BiomeSavanna);
                else if(column.biome == BiomeGen::BiomeDesert)
                    ++hot;
                else if(column.biome == BiomeGen::BiomeSavanna)
                    ++warm;

                ++sampled;
            }

        printf("    temperature: %d..%d, cold %d%%, desert %d%%, savanna %d%% of %d columns\n",
               coldest, hottest, cold * 100 / sampled, hot * 100 / sampled, warm * 100 / sampled, sampled);

        // The field has to use its whole range and actually produce both kinds of
        // weather, or one of the two would be a feature nobody could ever see.
        CHECK(coldest < BiomeGen::ColdTemperature);
        CHECK(hottest > BiomeGen::ColdTemperature);
        CHECK(cold > sampled / 100);
        CHECK(cold < sampled / 2);
    }
}

int main()
{
    printf("biomegen_test\n");

    testDeterminism();
    testBiomeCoverage();
    testSurfaceMaterials();
    testMountainsRaiseTheGround();
    testRivers();
    testHeightIsContinuous();
    testTrees();
    testCaves();
    testRavines();
    testRavineNeverBreaksTheSurface();
    testTemperatureField();

    printf("biomegen_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
