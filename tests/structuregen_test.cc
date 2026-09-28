// Host tests for the procedural structures (dungeons, ruins, temples).
//
// structuregen.cpp is engine-free by design, exactly like villagegen.cpp and
// biomegen.cpp, so the grid, the site search, every layout and the loot can all
// be driven on a host here. The tests pin down what the world generator and the
// chest store rely on:
//
//  - a structure is a pure function of (world seed, cell), so chunks can stream
//    in any order,
//  - emitChunk never writes outside the chunk it was asked for, and the union of
//    all chunks covering a structure is exactly its own layout,
//  - a dungeon is sealed, a ruin has a doorway and a broken roof, and a temple's
//    chest sits on a solid floor under the steps,
//  - the block and item ids written here are the real ones from terrain.h and
//    textures/items.h (the module keeps its own numeric copies so it can be
//    tested without the engine).
//
// Build and run with `make -C tests`.

#include "structuregen.h"
#include "biomegen.h"
#include "terrain.h"
#include "textures/items.h"

#include <stdio.h>
#include <stdint.h>
#include <map>
#include <vector>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

namespace
{
    const uint32_t WorldSeed = 0x5eed1234u;

    // A stand-in for one chunk: records what the generator writes and refuses to
    // accept anything outside the chunk it belongs to.
    struct Recorder
    {
        int chunk_x, chunk_y, chunk_z;
        std::map<long long, uint16_t> blocks;
        int out_of_bounds = 0;

        static long long key(int x, int y, int z)
        {
            return (static_cast<long long>(x) << 42) ^ (static_cast<long long>(y) << 21) ^ static_cast<long long>(z);
        }

        void reset(int cx, int cy, int cz)
        {
            chunk_x = cx;
            chunk_y = cy;
            chunk_z = cz;
            blocks.clear();
            out_of_bounds = 0;
        }

        bool inChunk(int x, int y, int z) const
        {
            return x >= chunk_x * Structures::ChunkBlocks && x < (chunk_x + 1) * Structures::ChunkBlocks
                && y >= chunk_y * Structures::ChunkBlocks && y < (chunk_y + 1) * Structures::ChunkBlocks
                && z >= chunk_z * Structures::ChunkBlocks && z < (chunk_z + 1) * Structures::ChunkBlocks;
        }

        void put(int x, int y, int z, uint16_t block)
        {
            if(!inChunk(x, y, z))
            {
                ++out_of_bounds;
                return;
            }
            blocks[key(x, y, z)] = block;
        }

        static void write(void *context, int x, int y, int z, uint16_t block)
        {
            static_cast<Recorder *>(context)->put(x, y, z, block);
        }
    };

    /** Finds the first cell of a given kind that can hold a structure. */
    bool findPlan(uint32_t seed, int kind, int ground_y, int biome, Structures::Plan &out)
    {
        out = Structures::Plan();
        for(int cell_z = -12; cell_z <= 12; ++cell_z)
            for(int cell_x = -12; cell_x <= 12; ++cell_x)
            {
                if(!Structures::cellHasStructure(seed, cell_x, cell_z))
                    continue;
                if(Structures::cellKind(seed, cell_x, cell_z) != kind)
                    continue;

                for(int candidate = 0; candidate < Structures::CandidateCount; ++candidate)
                    if(Structures::planStructure(seed, cell_x, cell_z, candidate, ground_y, biome, out))
                        return true;
            }
        return false;
    }

    bool samePlan(const Structures::Plan &a, const Structures::Plan &b)
    {
        return a.valid == b.valid && a.kind == b.kind && a.cell_x == b.cell_x && a.cell_z == b.cell_z
            && a.origin_x == b.origin_x && a.origin_y == b.origin_y && a.origin_z == b.origin_z
            && a.seed == b.seed;
    }
}

static void test_hash_is_deterministic()
{
    // Same inputs, same answer, always.
    for(int i = 0; i < 16; ++i)
        CHECK(Structures::hashSeed(WorldSeed, 3, -7, 1u + i) == Structures::hashSeed(WorldSeed, 3, -7, 1u + i));

    // A grid of cells must not collapse onto a handful of values, and changing
    // the world seed has to change nearly all of them.
    std::map<uint32_t, int> distinct, distinct_other;
    int different_seeds = 0, cells = 0;
    for(int cell_z = 0; cell_z < 32; ++cell_z)
        for(int cell_x = 0; cell_x < 32; ++cell_x)
        {
            const uint32_t h = Structures::hashSeed(WorldSeed, cell_x, cell_z, 1u);
            const uint32_t g = Structures::hashSeed(WorldSeed + 1u, cell_x, cell_z, 1u);
            distinct[h] = 1;
            distinct_other[g] = 1;
            if(h != g)
                ++different_seeds;
            ++cells;
        }

    CHECK(static_cast<int>(distinct.size()) > cells * 9 / 10);
    CHECK(static_cast<int>(distinct_other.size()) > cells * 9 / 10);
    CHECK(different_seeds > cells * 9 / 10);
}

static void test_cell_density_and_kinds()
{
    // Roughly a quarter of the cells hold something, split between the three
    // kinds: about 45% dungeons, 35% ruins, 20% temples.
    int total = 0, with_structure = 0;
    int kinds[Structures::KindCount] = {};
    for(int cell_z = -32; cell_z < 32; ++cell_z)
        for(int cell_x = -32; cell_x < 32; ++cell_x)
        {
            ++total;
            if(!Structures::cellHasStructure(WorldSeed, cell_x, cell_z))
                continue;
            ++with_structure;
            ++kinds[Structures::cellKind(WorldSeed, cell_x, cell_z)];
        }

    CHECK(total == 64 * 64);
    CHECK(with_structure > total / 4 - total / 20);
    CHECK(with_structure < total / 4 + total / 20);

    const int dungeon = kinds[Structures::KindDungeon];
    const int ruin = kinds[Structures::KindRuin];
    const int temple = kinds[Structures::KindTemple];
    CHECK(dungeon + ruin + temple == with_structure);
    CHECK(kinds[Structures::KindNone] == 0);
    CHECK(dungeon > ruin && ruin > temple); // the intended order of rarity
    CHECK(dungeon > with_structure * 35 / 100 && dungeon < with_structure * 55 / 100);
    CHECK(ruin > with_structure * 25 / 100 && ruin < with_structure * 45 / 100);
    CHECK(temple > with_structure * 10 / 100 && temple < with_structure * 30 / 100);
}

static void test_candidates_stay_inside_their_cell()
{
    // A structure must never reach into a neighbouring cell, or two of them could
    // grow into each other.
    for(int cell_z = -8; cell_z <= 8; ++cell_z)
        for(int cell_x = -8; cell_x <= 8; ++cell_x)
            for(int candidate = 0; candidate < Structures::CandidateCount; ++candidate)
            {
                int x = 0, z = 0;
                Structures::cellCandidate(WorldSeed, cell_x, cell_z, candidate, x, z);

                CHECK(x - Structures::MaxReach >= cell_x * Structures::CellBlocks);
                CHECK(x + Structures::MaxReach < (cell_x + 1) * Structures::CellBlocks);
                CHECK(z - Structures::MaxReach >= cell_z * Structures::CellBlocks);
                CHECK(z + Structures::MaxReach < (cell_z + 1) * Structures::CellBlocks);
            }
}

static void test_plan_is_pure_and_sites_are_rejected()
{
    // The same question, twice, from different orders: the same plan.
    Structures::Plan dungeon, dungeon_again;
    CHECK(findPlan(WorldSeed, Structures::KindDungeon, 25, BiomeGen::BiomePlains, dungeon));
    CHECK(dungeon.valid);
    CHECK(Structures::planStructure(WorldSeed, dungeon.cell_x, dungeon.cell_z, 0, 25, BiomeGen::BiomePlains, dungeon_again)
          || Structures::planStructure(WorldSeed, dungeon.cell_x, dungeon.cell_z, 1, 25, BiomeGen::BiomePlains, dungeon_again));
    CHECK(samePlan(dungeon, dungeon_again));

    // Too little rock above it, and a dungeon would be a hole in a field.
    Structures::Plan rejected;
    for(int candidate = 0; candidate < Structures::CandidateCount; ++candidate)
        CHECK(!Structures::planStructure(WorldSeed, dungeon.cell_x, dungeon.cell_z, candidate,
                                        Structures::DungeonMinGroundY - 1, BiomeGen::BiomePlains, rejected));
    CHECK(!rejected.valid);

    // A candidate that does not exist, and a cell with no structure at all.
    CHECK(!Structures::planStructure(WorldSeed, dungeon.cell_x, dungeon.cell_z, Structures::CandidateCount,
                                     25, BiomeGen::BiomePlains, rejected));

    // Surface structures stay off water: a ground height at or below sea level is
    // a river, a lake or in a beach.
    Structures::Plan ruin;
    CHECK(findPlan(WorldSeed, Structures::KindRuin, 20, BiomeGen::BiomePlains, ruin));
    CHECK(ruin.valid);
    for(int candidate = 0; candidate < Structures::CandidateCount; ++candidate)
        CHECK(!Structures::planStructure(WorldSeed, ruin.cell_x, ruin.cell_z, candidate,
                                        Structures::SurfaceMinGroundY - 1, BiomeGen::BiomePlains, rejected));

    // A temple is a desert find only, and its steps have to fit under the ceiling.
    Structures::Plan temple;
    CHECK(findPlan(WorldSeed, Structures::KindTemple, 20, BiomeGen::BiomeDesert, temple));
    CHECK(temple.valid);
    for(int candidate = 0; candidate < Structures::CandidateCount; ++candidate)
    {
        CHECK(!Structures::planStructure(WorldSeed, temple.cell_x, temple.cell_z, candidate,
                                        20, BiomeGen::BiomePlains, rejected));
        CHECK(!Structures::planStructure(WorldSeed, temple.cell_x, temple.cell_z, candidate,
                                        20, BiomeGen::BiomeForest, rejected));
        CHECK(!Structures::planStructure(WorldSeed, temple.cell_x, temple.cell_z, candidate,
                                        Structures::SurfaceMaxGroundY + 1, BiomeGen::BiomeDesert, rejected));
    }

    // The plan knows how tall it is, and never claims to poke through the world.
    CHECK(Structures::highestY(ruin) == ruin.origin_y + 3);
    CHECK(Structures::highestY(temple) == temple.origin_y + 3);
    CHECK(Structures::highestY(dungeon) == dungeon.origin_y + 3);
    CHECK(Structures::highestY(ruin) < BiomeGen::MaxHeight);
}

static void test_emit_chunk_matches_the_layout()
{
    // Every chunk of a structure's bounding box, generated alone, has to write
    // exactly its own slice of the layout -- and nothing outside itself.
    const int kinds[3] = { Structures::KindDungeon, Structures::KindRuin, Structures::KindTemple };
    const int ground[3] = { 25, 20, 20 };
    const int biomes[3] = { BiomeGen::BiomePlains, BiomeGen::BiomePlains, BiomeGen::BiomeDesert };

    for(int k = 0; k < 3; ++k)
    {
        Structures::Plan plan;
        CHECK(findPlan(WorldSeed, kinds[k], ground[k], biomes[k], plan));
        if(!plan.valid)
            continue;

        std::map<long long, uint16_t> union_blocks;
        int emitted = 0;

        // A generous window around the origin, so the test also proves that
        // nothing spills further than the layout does.
        for(int cy = plan.origin_y / Structures::ChunkBlocks - 2; cy <= plan.origin_y / Structures::ChunkBlocks + 2; ++cy)
            for(int cz = plan.origin_z / Structures::ChunkBlocks - 2; cz <= plan.origin_z / Structures::ChunkBlocks + 2; ++cz)
                for(int cx = plan.origin_x / Structures::ChunkBlocks - 2; cx <= plan.origin_x / Structures::ChunkBlocks + 2; ++cx)
                {
                    Recorder recorder;
                    recorder.reset(cx, cy, cz);
                    Structures::emitChunk(plan, cx, cy, cz, &Recorder::write, &recorder);
                    CHECK(recorder.out_of_bounds == 0);

                    // Emitting the same chunk twice is idempotent.
                    Recorder again;
                    again.reset(cx, cy, cz);
                    Structures::emitChunk(plan, cx, cy, cz, &Recorder::write, &again);
                    CHECK(again.blocks == recorder.blocks);

                    for(std::map<long long, uint16_t>::const_iterator it = recorder.blocks.begin(); it != recorder.blocks.end(); ++it)
                    {
                        // One writer per block: two chunks must not both claim it.
                        CHECK(union_blocks.find(it->first) == union_blocks.end());
                        union_blocks[it->first] = it->second;
                        ++emitted;
                    }
                }

        CHECK(emitted > 20);

        // The union is exactly the layout, block for block.
        int layout_blocks = 0;
        const int reach = (kinds[k] == Structures::KindRuin) ? 3 : 4;
        for(int y = -4; y <= 4; ++y)
            for(int z = -reach - 1; z <= reach + 1; ++z)
                for(int x = -reach - 1; x <= reach + 1; ++x)
                {
                    uint16_t block = 0;
                    const bool defined = Structures::blockAtWorld(plan, plan.origin_x + x, plan.origin_y + y, plan.origin_z + z, block);
                    const std::map<long long, uint16_t>::const_iterator found =
                        union_blocks.find(Recorder::key(plan.origin_x + x, plan.origin_y + y, plan.origin_z + z));

                    if(defined)
                    {
                        ++layout_blocks;
                        CHECK(found != union_blocks.end());
                        if(found != union_blocks.end())
                            CHECK(found->second == block);
                    }
                    else
                        CHECK(found == union_blocks.end());
                }

        CHECK(layout_blocks == emitted);
    }
}

static uint16_t layout(const Structures::Plan &plan, int x, int y, int z, bool &defined)
{
    uint16_t block = 0;
    defined = Structures::blockAtWorld(plan, plan.origin_x + x, plan.origin_y + y, plan.origin_z + z, block);
    return block;
}

static void test_dungeon_is_a_sealed_room()
{
    Structures::Plan plan;
    CHECK(findPlan(WorldSeed, Structures::KindDungeon, 25, BiomeGen::BiomePlains, plan));
    if(!plan.valid)
        return;

    // The room is three layers tall with a cobblestone floor and ceiling; the
    // only glowstone is the block in the middle of the ceiling, which is the one
    // thing that lights it.
    int glowstone = 0, cobblestone = 0, spiderweb = 0, chests = 0, air = 0;
    for(int y = -1; y <= 3; ++y)
        for(int z = -4; z <= 4; ++z)
            for(int x = -4; x <= 4; ++x)
            {
                bool defined = false;
                const uint16_t block = layout(plan, x, y, z, defined);
                if(!defined)
                    continue;

                if(block == getBLOCKWDATA(BLOCK_GLOWSTONE, 0)) { ++glowstone; CHECK(x == 0 && z == 0 && y == 3); }
                else if(block == getBLOCKWDATA(BLOCK_COBBLESTONE, 0)) ++cobblestone;
                else if(block == getBLOCKWDATA(BLOCK_SPIDERWEB, 0)) ++spiderweb;
                else if(block == getBLOCKWDATA(BLOCK_CHEST, 0)) ++chests;
                else if(block == BLOCK_AIR) ++air;
                else CHECK(false); // nothing else belongs in a dungeon
            }

    CHECK(glowstone == 1);
    CHECK(chests == 1);
    CHECK(spiderweb > 0);
    CHECK(air > 10);
    CHECK(cobblestone > 100);

    // Sealed: the shell is solid cobblestone on all four sides of all three open
    // layers, so the room cannot be seen into before it is dug open.
    for(int y = 0; y <= 2; ++y)
        for(int z = -4; z <= 4; ++z)
            for(int x = -4; x <= 4; ++x)
            {
                const bool shell = (x == -4 || x == 4 || z == -4 || z == 4);
                if(!shell)
                    continue;
                bool defined = false;
                CHECK(layout(plan, x, y, z, defined) == getBLOCKWDATA(BLOCK_COBBLESTONE, 0));
                CHECK(defined);
            }

    // The chest stands against the far corner, on the floor, and the room is
    // tall enough to walk in.
    int chest_x = 0, chest_y = 0, chest_z = 0;
    CHECK(Structures::chestCount(plan) == 1);
    CHECK(Structures::chestPosition(plan, 0, chest_x, chest_y, chest_z));
    CHECK(chest_x == plan.origin_x + 3 && chest_y == plan.origin_y && chest_z == plan.origin_z + 3);

    bool defined = false;
    CHECK(layout(plan, 3, 0, 3, defined) == getBLOCKWDATA(BLOCK_CHEST, 0));
    CHECK(layout(plan, 3, -1, 3, defined) == getBLOCKWDATA(BLOCK_COBBLESTONE, 0)); // it stands on the floor
    CHECK(!Structures::chestPosition(plan, 1, chest_x, chest_y, chest_z));
}

static void test_ruin_has_a_doorway_and_a_broken_roof()
{
    Structures::Plan plan;
    CHECK(findPlan(WorldSeed, Structures::KindRuin, 20, BiomeGen::BiomePlains, plan));
    if(!plan.valid)
        return;

    // The doorway: the -z wall is open at the two middle columns of the two
    // lowest layers, which is what makes the hut enterable.
    for(int y = 0; y <= 1; ++y)
        for(int x = -1; x <= 0; ++x)
        {
            bool defined = true;
            layout(plan, x, y, -3, defined);
            CHECK(!defined);
        }

    // The roof is planks with holes in it: at least one hole and at least one
    // plank, and never anything else.
    int roof_planks = 0, roof_holes = 0;
    for(int z = -3; z <= 3; ++z)
        for(int x = -3; x <= 3; ++x)
        {
            bool defined = false;
            const uint16_t block = layout(plan, x, 3, z, defined);
            if(!defined) { ++roof_holes; continue; }
            ++roof_planks;
            CHECK(block == getBLOCKWDATA(BLOCK_PLANKS_DARK, 0));
        }
    CHECK(roof_holes > 0);
    CHECK(roof_planks > 0);

    // Walls are cobblestone, the interior is clear apart from rubble, and there
    // is a chest in a corner standing on the ground.
    int chests = 0, cobblestone = 0, rubble = 0;
    for(int y = 0; y <= 2; ++y)
        for(int z = -3; z <= 3; ++z)
            for(int x = -3; x <= 3; ++x)
            {
                bool defined = false;
                const uint16_t block = layout(plan, x, y, z, defined);
                if(!defined)
                    continue;

                const bool wall = (x == -3 || x == 3 || z == -3 || z == 3);
                if(wall)
                {
                    CHECK(block == getBLOCKWDATA(BLOCK_COBBLESTONE, 0));
                    ++cobblestone;
                }
                else if(block == getBLOCKWDATA(BLOCK_CHEST, 0)) ++chests;
                else if(block == getBLOCKWDATA(BLOCK_COBBLESTONE, 0) || block == getBLOCKWDATA(BLOCK_PLANKS_DARK, 0)) ++rubble;
                else CHECK(block == BLOCK_AIR);
            }

    CHECK(cobblestone > 20);
    CHECK(chests == Structures::chestCount(plan));
    CHECK(chests >= 1 && chests <= Structures::MaxChests);

    // Every chest of the ruin is inside the walls and on the ground layer.
    for(int i = 0; i < chests; ++i)
    {
        int x = 0, y = 0, z = 0;
        CHECK(Structures::chestPosition(plan, i, x, y, z));
        CHECK(y == plan.origin_y);
        CHECK(x > plan.origin_x - 3 && x < plan.origin_x + 3);
        CHECK(z > plan.origin_z - 3 && z < plan.origin_z + 3);
    }
}

static void test_temple_is_a_step_pyramid_over_a_chamber()
{
    Structures::Plan plan;
    CHECK(findPlan(WorldSeed, Structures::KindTemple, 20, BiomeGen::BiomeDesert, plan));
    if(!plan.valid)
        return;

    // Four solid layers of sand, each two blocks narrower than the one below.
    for(int y = 0; y <= 3; ++y)
    {
        const int reach = 4 - y;
        for(int z = -4; z <= 4; ++z)
            for(int x = -4; x <= 4; ++x)
            {
                bool defined = false;
                const uint16_t block = layout(plan, x, y, z, defined);
                const bool inside = (x >= -reach && x <= reach && z >= -reach && z <= reach);
                CHECK(defined == inside);
                if(inside)
                    CHECK(block == getBLOCKWDATA(BLOCK_SAND, 0));
            }
    }

    // Nothing above the top step.
    bool defined = true;
    layout(plan, 0, 4, 0, defined);
    CHECK(!defined);

    // The chamber: a 3x3 room two blocks tall, a cobblestone shell around it and
    // the chest in the middle of the floor.
    int chamber_air = 0, shell = 0, chests = 0;
    for(int y = -3; y <= -1; ++y)
        for(int z = -2; z <= 2; ++z)
            for(int x = -2; x <= 2; ++x)
            {
                bool defined = false;
                const uint16_t block = layout(plan, x, y, z, defined);
                CHECK(defined);

                const bool room = (x >= -1 && x <= 1 && z >= -1 && z <= 1 && y >= -2);
                if(room)
                {
                    if(block == getBLOCKWDATA(BLOCK_CHEST, 0)) { ++chests; CHECK(x == 0 && z == 0 && y == -2); }
                    else { CHECK(block == BLOCK_AIR); ++chamber_air; }
                }
                else
                {
                    CHECK(block == getBLOCKWDATA(BLOCK_COBBLESTONE, 0));
                    ++shell;
                }
            }

    CHECK(chamber_air == 3 * 3 * 2 - 1); // the 3x3x2 room, minus the chest
    CHECK(shell == 5 * 5 * 3 - 3 * 3 * 2);
    CHECK(chests == 1);

    // The chest stands on the floor of the chamber, and the sand base above it
    // keeps the sky out.
    CHECK(layout(plan, 0, -3, 0, defined) == getBLOCKWDATA(BLOCK_COBBLESTONE, 0));
    CHECK(layout(plan, 0, 0, 0, defined) == getBLOCKWDATA(BLOCK_SAND, 0));
    CHECK(Structures::chestCount(plan) == 1);

    int chest_x = 0, chest_y = 0, chest_z = 0;
    CHECK(Structures::chestPosition(plan, 0, chest_x, chest_y, chest_z));
    CHECK(chest_x == plan.origin_x && chest_y == plan.origin_y - 2 && chest_z == plan.origin_z);
}

static void test_loot_is_valid_and_reproducible()
{
    const int kinds[3] = { Structures::KindDungeon, Structures::KindRuin, Structures::KindTemple };
    const int ground[3] = { 25, 20, 20 };
    const int biomes[3] = { BiomeGen::BiomePlains, BiomeGen::BiomePlains, BiomeGen::BiomeDesert };
    const int item_count = static_cast<int>(ItemTexture::COOKED_SALMON) + 1;

    Structures::Loot loot[Structures::MaxLootStacks];
    Structures::Loot again[Structures::MaxLootStacks];

    for(int k = 0; k < 3; ++k)
    {
        int plans = 0;
        int chests_total = 0;
        int with_loot = 0;
        std::map<long long, int> distinct_loot;
        std::map<int, int> items_seen;

        for(int cell_z = -12; cell_z <= 12; ++cell_z)
            for(int cell_x = -12; cell_x <= 12; ++cell_x)
            {
                if(!Structures::cellHasStructure(WorldSeed, cell_x, cell_z))
                    continue;
                if(Structures::cellKind(WorldSeed, cell_x, cell_z) != kinds[k])
                    continue;

                Structures::Plan plan;
                if(!Structures::planStructure(WorldSeed, cell_x, cell_z, 0, ground[k], biomes[k], plan))
                    continue;
                ++plans;

                for(int index = 0; index < Structures::chestCount(plan); ++index)
                {
                    ++chests_total;
                    const int count = Structures::chestLoot(plan, index, loot, Structures::MaxLootStacks);
                    CHECK(count >= 1);
                    CHECK(count <= Structures::MaxLootStacks);

                    // Out of range and invalid plans yield nothing at all.
                    CHECK(Structures::chestLoot(plan, Structures::chestCount(plan), loot, Structures::MaxLootStacks) == 0);
                    CHECK(Structures::chestLoot(plan, -1, loot, Structures::MaxLootStacks) == 0);
                    CHECK(Structures::chestLoot(plan, index, loot, 0) == 0);

                    // ...and the same chest, asked twice, holds the same things.
                    const int count_again = Structures::chestLoot(plan, index, again, Structures::MaxLootStacks);
                    CHECK(count_again == count);
                    for(int i = 0; i < count; ++i)
                    {
                        CHECK(again[i].stack == loot[i].stack);
                        CHECK(again[i].count == loot[i].count);
                    }

                    long long signature = 0;
                    for(int i = 0; i < count; ++i)
                    {
                        // A stack is an item, never a block, with a real atlas id
                        // and a count a slot can hold.
                        CHECK(getBLOCK(loot[i].stack) == BLOCK_ITEM);
                        const int item = static_cast<int>(getITEMDATA(loot[i].stack));
                        CHECK(item > 0 && item < item_count);
                        CHECK(loot[i].count >= 1 && loot[i].count <= 64);

                        ++items_seen[item];
                        signature = signature * 1000003 + loot[i].stack * 64 + static_cast<long long>(loot[i].count);
                    }
                    distinct_loot[signature] = 1;
                    if(count > 0)
                        ++with_loot;

                }
            }

        CHECK(plans > 20);
        CHECK(chests_total >= plans);
        CHECK(with_loot == chests_total); // every chest in the world holds something
        // Different structures hold different things: the tables are not constant.
        CHECK(static_cast<int>(distinct_loot.size()) > plans * 3 / 4);
        CHECK(static_cast<int>(items_seen.size()) > 6);
    }

    // A small stack limit is honoured exactly.
    Structures::Plan dungeon;
    CHECK(findPlan(WorldSeed, Structures::KindDungeon, 25, BiomeGen::BiomePlains, dungeon));
    if(dungeon.valid)
    {
        CHECK(Structures::chestLoot(dungeon, 0, loot, 1) == 1);
        CHECK(Structures::chestLoot(dungeon, 0, loot, 2) == 2);
        // Asking for fewer stacks gives a prefix of the same loot.
        Structures::Loot two[2];
        CHECK(Structures::chestLoot(dungeon, 0, two, 2) == 2);
        Structures::Loot one[1];
        CHECK(Structures::chestLoot(dungeon, 0, one, 1) == 1);
        CHECK(one[0].stack == two[0].stack && one[0].count == two[0].count);
        CHECK(Structures::chestLoot(dungeon, 0, nullptr, 3) == 0);
    }
}

int main()
{
    printf("structuregen_test\n");

    test_hash_is_deterministic();
    test_cell_density_and_kinds();
    test_candidates_stay_inside_their_cell();
    test_plan_is_pure_and_sites_are_rejected();
    test_emit_chunk_matches_the_layout();
    test_dungeon_is_a_sealed_room();
    test_ruin_has_a_doorway_and_a_broken_roof();
    test_temple_is_a_step_pyramid_over_a_chamber();
    test_loot_is_valid_and_reproducible();

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
