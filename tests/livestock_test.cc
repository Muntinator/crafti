// Host tests for the livestock definition tables: per-species stats, drop
// tables, the vanilla model layouts and the biome-weighted spawn rules.
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
    CHECK(Livestock::SpeciesCount == 8);
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
    // The wolf has no vanilla drop, so it drops its pelt as leather like the horse.
    CHECK(Livestock::stats(Species::Wolf).drop_stack[0]
          == Livestock::itemStack(static_cast<uint8_t>(ItemTexture::LEATHER)));
    CHECK(Livestock::stats(Species::Mooshroom).drop_stack[0]
          == Livestock::itemStack(static_cast<uint8_t>(ItemTexture::RAW_BEEF)));
    CHECK(Livestock::stats(Species::Mooshroom).drop_stack[1]
          == Livestock::itemStack(static_cast<uint8_t>(ItemTexture::LEATHER)));
    CHECK(Livestock::stats(Species::Donkey).drop_stack[0]
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
bool is_leg(Mob::Pose pose)
{
    return pose == Mob::Pose::LegFrontLeft || pose == Mob::Pose::LegFrontRight
        || pose == Mob::Pose::LegBackLeft || pose == Mob::Pose::LegBackRight;
}

// A box's unwrap is 2*(w+d) wide and d+h tall, starting at its uv origin. If it
// runs past the skin the box samples whatever is next to it, so it has to fit.
bool box_fits(const Mob::MobBox &b, int tw, int th)
{
    return b.u >= 0 && b.v >= 0 && b.w > 0 && b.h > 0 && b.d > 0
        && b.u + 2 * (b.w + b.d) <= tw
        && b.v + b.d + b.h <= th;
}
}

static void test_vanilla_models()
{
    // Every mob but the chicken draws the vanilla 1.17.1 box model, so its UV
    // origins have to land inside the official skin it is drawn with.
    for(unsigned int i = 0; i < Livestock::SpeciesCount; ++i)
    {
        const Species s = static_cast<Species>(i);
        const Mob::MobModel &m = Livestock::model(s);

        if(s == Species::Chicken)
        {
            CHECK(m.empty()); // the renderer still draws this one itself
            continue;
        }

        CHECK(!m.empty());
        CHECK(m.tex_width == 64);
        CHECK(m.part_count > 0);
        CHECK(m.box_count > 0);

        for(unsigned int b = 0; b < m.box_count; ++b)
            CHECK(box_fits(m.boxes[b], m.tex_width, m.tex_height));

        unsigned int legs = 0;
        for(unsigned int p = 0; p < m.part_count; ++p)
        {
            const Mob::MobPart &part = m.parts[p];
            CHECK(part.box_count > 0);
            CHECK(part.first_box + part.box_count <= m.box_count);
            if(is_leg(part.pose))
            {
                ++legs;
                // The y axis counts down, so a foot on the ground ends at 24.
                const Mob::MobBox &leg = m.boxes[part.first_box];
                CHECK(part.py + leg.oy + leg.h == 24);
            }
        }
        CHECK(legs >= 2); // something has to hold the mob up
    }
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
    // Grassland {20, 15, 15, 20, 10, 10, 5, 5}
    CHECK(species_index(Livestock::pickSpecies(0, Biome::Grassland)) == species_index(Species::Cow));
    CHECK(species_index(Livestock::pickSpecies(19, Biome::Grassland)) == species_index(Species::Cow));
    CHECK(species_index(Livestock::pickSpecies(20, Biome::Grassland)) == species_index(Species::Pig));
    CHECK(species_index(Livestock::pickSpecies(34, Biome::Grassland)) == species_index(Species::Pig));
    CHECK(species_index(Livestock::pickSpecies(35, Biome::Grassland)) == species_index(Species::Sheep));
    CHECK(species_index(Livestock::pickSpecies(49, Biome::Grassland)) == species_index(Species::Sheep));
    CHECK(species_index(Livestock::pickSpecies(50, Biome::Grassland)) == species_index(Species::Chicken));
    CHECK(species_index(Livestock::pickSpecies(69, Biome::Grassland)) == species_index(Species::Chicken));
    CHECK(species_index(Livestock::pickSpecies(70, Biome::Grassland)) == species_index(Species::Horse));
    CHECK(species_index(Livestock::pickSpecies(79, Biome::Grassland)) == species_index(Species::Horse));
    CHECK(species_index(Livestock::pickSpecies(80, Biome::Grassland)) == species_index(Species::Wolf));
    CHECK(species_index(Livestock::pickSpecies(89, Biome::Grassland)) == species_index(Species::Wolf));
    CHECK(species_index(Livestock::pickSpecies(90, Biome::Grassland)) == species_index(Species::Mooshroom));
    CHECK(species_index(Livestock::pickSpecies(94, Biome::Grassland)) == species_index(Species::Mooshroom));
    CHECK(species_index(Livestock::pickSpecies(95, Biome::Grassland)) == species_index(Species::Donkey));
    CHECK(species_index(Livestock::pickSpecies(99, Biome::Grassland)) == species_index(Species::Donkey));

    // Wolves hunt in the forest and never turn up on sand.
    CHECK(Livestock::spawnWeight(Biome::Forest, Species::Wolf)
          > Livestock::spawnWeight(Biome::Grassland, Species::Wolf));
    CHECK(Livestock::spawnWeight(Biome::Desert, Species::Wolf) == 0);

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
    test_vanilla_models();
    test_weights();
    test_pick_boundaries();
    test_classify();
    test_limits();

    printf("livestock_test\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
