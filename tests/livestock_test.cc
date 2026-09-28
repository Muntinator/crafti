// Host tests for the livestock definition tables: per-species stats, drop
// tables, the shared quadruped UV layout and the biome-weighted spawn rules.
//
// Build and run with `make -C tests`.

#include "livestockspecies.h"
#include "terrain.h"
#include "textures/items.h"

#include <stdio.h>
#include <stdint.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

using Livestock::Biome;
using Livestock::Species;

static int species_index(Species s) { return static_cast<int>(s); }

static void test_stats()
{
    CHECK(Livestock::SpeciesCount == 5);
    CHECK(Livestock::BiomeCount == 5);

    for(unsigned int i = 0; i < Livestock::SpeciesCount; ++i)
    {
        const Species s = static_cast<Species>(i);
        const Livestock::Stats &st = Livestock::stats(s);
        CHECK(st.width > 0);
        CHECK(st.height > 0);
        CHECK(st.health > 0);
        CHECK(st.render_scale > 0);
        CHECK(st.idle_interval > 0);
        CHECK(st.wander_speed > 0);
        CHECK(st.drop_count >= 1 && st.drop_count <= 2);
        for(unsigned int d = 0; d < st.drop_count; ++d)
        {
            // A drop may be a chance drop (min 0), but it must be a real stack
            // that can actually drop at least once.
            CHECK(st.drop_stack[d] != 0);
            CHECK(st.drop_max[d] >= 1);
            CHECK(st.drop_max[d] >= st.drop_min[d]);
        }
        CHECK(Livestock::name(s) != nullptr);
    }

    // Names are distinct so the spawner/debug output is unambiguous.
    for(unsigned int i = 0; i < Livestock::SpeciesCount; ++i)
        for(unsigned int j = i + 1; j < Livestock::SpeciesCount; ++j)
            CHECK(Livestock::name(static_cast<Species>(i)) != Livestock::name(static_cast<Species>(j)));

    // Out-of-range access must not read past the tables.
    CHECK(Livestock::stats(static_cast<Species>(200)).health > 0);
    CHECK(Livestock::name(static_cast<Species>(200)) != nullptr);
}

static void test_drops()
{
    CHECK(Livestock::stats(Species::Cow).drop_stack[0]
          == Livestock::itemStack(static_cast<uint8_t>(ItemTexture::RAW_BEEF)));
    CHECK(Livestock::stats(Species::Cow).drop_stack[1]
          == Livestock::itemStack(static_cast<uint8_t>(ItemTexture::LEATHER)));
    CHECK(Livestock::stats(Species::Pig).drop_stack[0]
          == Livestock::itemStack(static_cast<uint8_t>(ItemTexture::RAW_PORKCHOP)));
    CHECK(Livestock::stats(Species::Sheep).drop_stack[0] == Livestock::blockStack(BLOCK_WOOL_WHITE));
    CHECK(Livestock::stats(Species::Chicken).drop_stack[0]
          == Livestock::itemStack(static_cast<uint8_t>(ItemTexture::RAW_CHICKEN)));
    CHECK(Livestock::stats(Species::Horse).drop_stack[0]
          == Livestock::itemStack(static_cast<uint8_t>(ItemTexture::LEATHER)));

    // Item stacks must survive the BLOCK_ITEM encoding, through getITEMDATA():
    // an id of 128 or more does not fit in the 7-bit block reading.
    CHECK(getBLOCK(Livestock::itemStack(static_cast<uint8_t>(ItemTexture::RAW_BEEF))) == BLOCK_ITEM);
    CHECK(getITEMDATA(Livestock::itemStack(static_cast<uint8_t>(ItemTexture::RAW_BEEF)))
          == static_cast<uint8_t>(ItemTexture::RAW_BEEF));
    for(unsigned int i = 0; i < Livestock::SpeciesCount; ++i)
    {
        const Livestock::Stats &st = Livestock::stats(static_cast<Species>(i));
        for(unsigned int d = 0; d < st.drop_count; ++d)
        {
            const BLOCK_WDATA stack = st.drop_stack[d];
            // A drop is either an item (sheep wool is the exception: it is a
            // placeable block). An item id past the end of the 16x11 atlas would
            // be invisible, and a block id past the last real block would be an
            // unknown block.
            if(getBLOCK(stack) == BLOCK_ITEM)
                CHECK(static_cast<int>(getITEMDATA(stack)) < 16 * 11);
            else
                CHECK(getBLOCK(stack) <= BLOCK_SPECIAL_LAST);
        }
    }
}

namespace {
struct Rect { int x0, y0, x1, y1; };

Rect box_bounds(int u, int v, int w, int h, int d)
{
    // Unwrap width is 2*(w+d), height is d+h; see drawQuadBox.
    return { u, v, u + 2 * (w + d), v + d + h };
}

bool overlaps(const Rect &a, const Rect &b)
{
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}
}

static void test_quadruped_layout()
{
    const Livestock::QuadrupedModel &m = Livestock::quadrupedModel();
    CHECK(m.tex_width == 64);
    CHECK(m.tex_height == 64);

    const Rect head = box_bounds(m.head_u, m.head_v, m.head_w, m.head_h, m.head_d);
    const Rect body = box_bounds(m.body_u, m.body_v, m.body_w, m.body_h, m.body_d);
    const Rect leg = box_bounds(m.leg_u, m.leg_v, m.leg_w, m.leg_h, m.leg_d);
    const Rect detail = box_bounds(m.detail_u, m.detail_v, m.detail_w, m.detail_h, m.detail_d);

    const Rect regions[4] = { head, body, leg, detail };
    for(const Rect &r : regions)
    {
        CHECK(r.x0 >= 0 && r.y0 >= 0);
        CHECK(r.x1 <= m.tex_width);
        CHECK(r.y1 <= m.tex_height);
    }

    for(unsigned int i = 0; i < 4; ++i)
        for(unsigned int j = i + 1; j < 4; ++j)
            CHECK(!overlaps(regions[i], regions[j]));
}

static void test_weights()
{
    for(unsigned int b = 0; b < Livestock::BiomeCount; ++b)
    {
        unsigned int sum = 0;
        for(unsigned int s = 0; s < Livestock::SpeciesCount; ++s)
        {
            const unsigned int w = Livestock::spawnWeight(static_cast<Biome>(b), static_cast<Species>(s));
            CHECK(w <= 100);
            sum += w;
        }
        CHECK(sum <= 100);
    }

    // Water never spawns anything; deserts stay mostly empty.
    for(uint32_t roll = 0; roll < 100; ++roll)
        CHECK(Livestock::pickSpecies(roll, Biome::Water) == Species::Count);
    CHECK(Livestock::pickSpecies(99, Biome::Desert) == Species::Count);
    CHECK(Livestock::pickSpecies(0, Biome::Desert) == Species::Sheep);
    CHECK(Livestock::pickSpecies(29, Biome::Desert) == Species::Chicken);
    CHECK(Livestock::pickSpecies(30, Biome::Desert) == Species::Count);
}

static void test_pick_boundaries()
{
    // Grassland {25, 20, 20, 25, 10}
    CHECK(species_index(Livestock::pickSpecies(0, Biome::Grassland)) == species_index(Species::Cow));
    CHECK(species_index(Livestock::pickSpecies(24, Biome::Grassland)) == species_index(Species::Cow));
    CHECK(species_index(Livestock::pickSpecies(25, Biome::Grassland)) == species_index(Species::Pig));
    CHECK(species_index(Livestock::pickSpecies(44, Biome::Grassland)) == species_index(Species::Pig));
    CHECK(species_index(Livestock::pickSpecies(45, Biome::Grassland)) == species_index(Species::Sheep));
    CHECK(species_index(Livestock::pickSpecies(64, Biome::Grassland)) == species_index(Species::Sheep));
    CHECK(species_index(Livestock::pickSpecies(65, Biome::Grassland)) == species_index(Species::Chicken));
    CHECK(species_index(Livestock::pickSpecies(89, Biome::Grassland)) == species_index(Species::Chicken));
    CHECK(species_index(Livestock::pickSpecies(90, Biome::Grassland)) == species_index(Species::Horse));
    CHECK(species_index(Livestock::pickSpecies(99, Biome::Grassland)) == species_index(Species::Horse));

    // Every roll must land on exactly one species (or none) in every biome.
    for(unsigned int b = 0; b < Livestock::BiomeCount; ++b)
        for(uint32_t roll = 0; roll < 200; ++roll)
        {
            const Species s = Livestock::pickSpecies(roll, static_cast<Biome>(b));
            CHECK(species_index(s) >= 0 && species_index(s) <= species_index(Species::Count));
        }
}

static void test_classify()
{
    using Livestock::Surface;
    CHECK(Livestock::classify(false, false, Surface::Water) == Biome::Water);
    CHECK(Livestock::classify(true, false, Surface::Grass) == Biome::Shore);
    CHECK(Livestock::classify(false, false, Surface::Sand) == Biome::Desert);
    CHECK(Livestock::classify(false, true, Surface::Grass) == Biome::Forest);
    CHECK(Livestock::classify(false, false, Surface::Grass) == Biome::Grassland);
    CHECK(Livestock::classify(false, false, Surface::Stone) == Biome::Grassland);
    // Water wins over the shore/tree signals.
    CHECK(Livestock::classify(true, true, Surface::Water) == Biome::Water);
}

static void test_limits()
{
    CHECK(Livestock::maxEntities() > 0);
    CHECK(Livestock::despawnDistanceBlocks() > 0);
    // Despawning has to happen beyond the spawn ring (<= 16 blocks).
    CHECK(Livestock::despawnDistanceBlocks() > 16);
}

int main()
{
    test_stats();
    test_drops();
    test_quadruped_layout();
    test_weights();
    test_pick_boundaries();
    test_classify();
    test_limits();

    printf("livestock_test\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
