// Host tests for the livestock definition tables: per-species stats, drop
// tables, the vanilla model layouts and the biome-weighted spawn rules.
//
// Build and run with `make -C tests`.

#include "livestockspecies.h"
#include "terrain.h"
#include "textures/items.h"

#include <stdio.h>
#include <stdint.h>
#include <math.h>

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

// --- where the models actually land -------------------------------------
//
// The tables above are vanilla's numbers and read fine as numbers; the mistake
// that puts a torso the wrong way up is not in them but in the frame they are
// drawn in. mobmodel.h's `angleInDrawnFrame` is the renderer's own rule for
// that, so the geometry below is computed through it -- if the renderer and
// this test ever disagreed about the sign, one of them would be wrong and these
// checks would say so.
namespace
{
struct Extent
{
    double x0, x1, y0, y1, z0, z1;
};

// Where one box ends up, in model units above the feet, exactly as mobmodel.cpp
// puts it there: the part's pivot translated with the y axis flipped once, the
// head yaw, then the part's own rotation -- both angles passed through the same
// `angleInDrawnFrame` the renderer uses.
Extent drawnBox(const Mob::MobPart &part, const Mob::MobBox &b, double head_yaw_deg)
{
    const double g = b.grow / 2.0;
    const double lx[2] = { b.ox - g, b.ox - g + b.w + g * 2 };
    const double ly[2] = { -(b.oy + b.h) - g, -b.oy + g };
    const double lz[2] = { b.oz - g, b.oz - g + b.d + g * 2 };

    const double hy = head_yaw_deg;
    const double rx = static_cast<double>(Mob::angleInDrawnFrame(GLFix(part.rot_x)));
    const double ya = hy * M_PI / 180.0, ra = rx * M_PI / 180.0;
    const double sy = sin(ya), cy = cos(ya), sr = sin(ra), cr = cos(ra);

    Extent e = { 1e30, -1e30, 1e30, -1e30, 1e30, -1e30 };
    for(int i = 0; i < 2; ++i)
        for(int j = 0; j < 2; ++j)
            for(int k = 0; k < 2; ++k)
            {
                double x = lx[i], y = ly[j], z = lz[k];
                if(part.pose == Mob::Pose::Head && hy != 0.0)
                {
                    const double nx = x * cy + z * sy;
                    z = -x * sy + z * cy;
                    x = nx;
                }
                if(rx != 0.0)
                {
                    const double ny = y * cr - z * sr;
                    z = y * sr + z * cr;
                    y = ny;
                }
                x += part.px;
                y += 24 - part.py;
                z += part.pz;
                if(x < e.x0) e.x0 = x;
                if(x > e.x1) e.x1 = x;
                if(y < e.y0) e.y0 = y;
                if(y > e.y1) e.y1 = y;
                if(z < e.z0) e.z0 = z;
                if(z > e.z1) e.z1 = z;
            }
    return e;
}

// The part's box with the largest drawn volume: the torso of a biped, and for
// a quadruped the body box (the head and the legs are smaller).
int biggestBox(const Mob::MobModel &m, const Mob::MobPart &part)
{
    int best = part.first_box;
    int best_vol = -1;
    for(unsigned int b = 0; b < part.box_count; ++b)
    {
        const Mob::MobBox &box = m.boxes[part.first_box + b];
        const int vol = box.w * box.h * box.d;
        if(vol > best_vol) { best_vol = vol; best = part.first_box + b; }
    }
    return best;
}
}

// A mob that is not upside down: it stands on its feet, its torso rests on top
// of its legs rather than floating over them, and nothing reaches down through
// the floor. The quadruped torso is the sharp case -- vanilla lays that box on
// its side (rot_x 90) so its long axis runs front-to-back, and drawn with the
// wrong sense of that rotation the box keeps its size but lands a quarter turn
// out: clear of the legs, and the cow's udder up on its back.
static void test_drawn_models_are_the_right_way_up()
{
    for(unsigned int i = 0; i < Livestock::SpeciesCount; ++i)
    {
        const Species s = static_cast<Species>(i);
        const Mob::MobModel &m = Livestock::model(s);
        if(m.empty())
            continue; // the chicken draws itself

        double ground = 1e30, roof = -1e30, leg_top = -1e30;
        bool have_leg = false;

        for(unsigned int p = 0; p < m.part_count; ++p)
        {
            const Mob::MobPart &part = m.parts[p];
            for(unsigned int b = 0; b < part.box_count; ++b)
            {
                const Extent e = drawnBox(part, m.boxes[part.first_box + b], 0.0);
                if(e.y0 < ground) ground = e.y0;
                if(e.y1 > roof) roof = e.y1;
                if(is_leg(part.pose))
                {
                    if(e.y1 > leg_top) leg_top = e.y1;
                    have_leg = true;
                }
            }
        }

        CHECK(have_leg);
        // The feet are on the floor and nothing sinks through it.
        CHECK(fabs(ground) < 0.001);
        // A mob is between half a block and four blocks tall. A torso that flew
        // off would blow past the top of this, and one that dropped would go
        // under it. (A wolf is genuinely under a block, hence the low bound.)
        CHECK(roof >= 8.0);
        CHECK(roof <= 64.0);

        // The torso is the first part that is neither a leg nor the head: every
        // vanilla table lists the body before the tail and the mane. Neck,
        // mane, tail and ears are checked by the bounds above, not here --
        // they are meant to stick out past the body.
        int torso_part = -1;
        for(unsigned int p = 0; p < m.part_count; ++p)
        {
            const Mob::MobPart &part = m.parts[p];
            if(!is_leg(part.pose) && part.pose != Mob::Pose::Head)
            {
                torso_part = static_cast<int>(p);
                break;
            }
        }
        CHECK(torso_part >= 0);

        if(torso_part >= 0)
        {
            const Mob::MobPart &part = m.parts[torso_part];
            const int big = biggestBox(m, part);
            const Mob::MobBox &torso = m.boxes[big];
            const Extent t = drawnBox(part, torso, 0.0);
            const bool sideways = part.rot_x == 90;

            // The box vanilla laid on its side: its *depth* is the height it
            // stands at and its *height* is the length it runs along.
            if(sideways)
            {
                CHECK(fabs((t.y1 - t.y0) - torso.d) < 0.001);
                CHECK(fabs((t.z1 - t.z0) - torso.h) < 0.001);
            }
            // Nothing else on the torso's part may rise above the torso: the
            // cow's udder hangs under the belly, it does not sit on the back.
            for(unsigned int b = 0; b < part.box_count; ++b)
            {
                if(static_cast<int>(part.first_box + b) == big)
                    continue;
                const Extent o = drawnBox(part, m.boxes[part.first_box + b], 0.0);
                CHECK(o.y1 <= t.y1 + 0.001);
            }
            // It reaches down to the legs: either resting on them (the cow and
            // the pig both sit exactly on top) or overlapping them, which is
            // what a wolf's low body does. What it must never do is float clear
            // above them -- that is the quarter-turn error this whole thing is
            // about, and it lifted the cow's body four units off its legs.
            CHECK(t.y0 <= leg_top + 0.001);
            // And it is not the tallest thing on the animal.
            CHECK(t.y1 <= roof + 0.001);
        }

        // The head has to be the tallest thing on the animal. A torso poking
        // past it is the same bug seen from the other side. Every box of the
        // head's part counts: the cow's horns stand above its head box.
        double head_top = -1e30;
        for(unsigned int p = 0; p < m.part_count; ++p)
        {
            const Mob::MobPart &part = m.parts[p];
            if(part.pose != Mob::Pose::Head)
                continue;
            for(unsigned int b = 0; b < part.box_count; ++b)
            {
                const Extent e = drawnBox(part, m.boxes[part.first_box + b], 0.0);
                if(e.y1 > head_top) head_top = e.y1;
            }
        }
        if(head_top > -1e29)
            CHECK(head_top >= roof - 0.001);
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
    test_drawn_models_are_the_right_way_up();
    test_weights();
    test_pick_boundaries();
    test_classify();
    test_limits();

    printf("livestock_test\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
