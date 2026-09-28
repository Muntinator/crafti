#include "structuregen.h"

/**
 * Block and item ids, as bare numbers so this file stays free of the engine and
 * of the texture tables. They are the ids documented in terrain.h and in
 * textures/items.h; tests/structuregen_test.cc checks every one of them against
 * the real headers, so a wrong number is a failing test rather than a world full
 * of the wrong blocks.
 */
namespace
{
    // Blocks (terrain.h)
    constexpr uint16_t Air = 0;
    constexpr uint16_t Sand = 3;
    constexpr uint16_t PlanksDark = 15;
    constexpr uint16_t Cobblestone = 24;
    constexpr uint16_t Glowstone = 25;
    constexpr uint16_t Spiderweb = 129; // a special block, but a plain block id
    constexpr uint16_t Chest = 45;      // BLOCK_CHEST

    // Items are BLOCK_ITEM (254) with the item id in the data byte, which is what
    // getBLOCKWDATA(BLOCK_ITEM, id) produces.
    constexpr uint16_t ItemBlock = 254;
    constexpr uint16_t packItem(uint16_t id) { return static_cast<uint16_t>((id << 8) | ItemBlock); }

    constexpr uint16_t ItemCoal = 6;            // ItemTexture::COAL
    constexpr uint16_t ItemString = 8;          // ItemTexture::STRING
    constexpr uint16_t ItemWheatSeeds = 9;      // ItemTexture::WHEAT_SEEDS
    constexpr uint16_t ItemApple = 10;          // ItemTexture::APPLE
    constexpr uint16_t ItemGoldenApple = 11;    // ItemTexture::GOLDEN_APPLE
    constexpr uint16_t ItemArrow = 37;          // ItemTexture::ARROW
    constexpr uint16_t ItemGoldIngot = 39;      // ItemTexture::GOLD_INGOT
    constexpr uint16_t ItemGunpowder = 40;      // ItemTexture::GUNPOWDER
    constexpr uint16_t ItemBread = 41;          // ItemTexture::BREAD
    constexpr uint16_t ItemStick = 53;          // ItemTexture::STICK
    constexpr uint16_t ItemCompass = 54;        // ItemTexture::COMPASS
    constexpr uint16_t ItemDiamond = 55;        // ItemTexture::DIAMOND
    constexpr uint16_t ItemRedstoneDust = 56;   // ItemTexture::REDSTONE_DUST
    constexpr uint16_t ItemPaper = 58;          // ItemTexture::PAPER
    constexpr uint16_t ItemBook = 59;           // ItemTexture::BOOK
    constexpr uint16_t ItemClock = 70;          // ItemTexture::CLOCK
    constexpr uint16_t ItemGlowstoneDust = 73;  // ItemTexture::GLOWSTONE_DUST
    constexpr uint16_t ItemIronPickaxe = 98;    // ItemTexture::IRON_PICKAXE
    constexpr uint16_t ItemSaddle = 104;        // ItemTexture::SADDLE
    constexpr uint16_t ItemGoldNugget = 124;    // ItemTexture::GOLD_NUGGET
    constexpr uint16_t ItemSpiderEye = 139;     // ItemTexture::SPIDER_EYE
    constexpr uint16_t ItemRottenFlesh = 141;   // ItemTexture::ROTTEN_FLESH
    constexpr uint16_t ItemLapisLazuli = 142;   // ItemTexture::LAPIS_LAZULI
    constexpr uint16_t ItemIronIngot = 145;     // ItemTexture::IRON_INGOT
    constexpr uint16_t ItemBoneMeal = 175;      // ItemTexture::BONE_MEAL

    // Layout constants
    constexpr int StructurePercent = 25;  // percent of cells that hold anything

    constexpr int DungeonHalf = 4;        // 9x9 footprint
    constexpr int DungeonOpenLayers = 3;  // open layers: dy = 0, 1, 2
    constexpr int RuinHalf = 3;           // 7x7 footprint
    constexpr int RuinWallHeight = 3;     // walls at dy = 0, 1, 2
    constexpr int RuinRoofY = 3;
    constexpr int TempleHalf = 4;         // 9x9 footprint
    constexpr int TempleTopY = 3;         // four layers, dy = 0..3
    constexpr int TempleChamberFloorY = -3;

    /** A structure's own hash, mixed so neighbouring cells look unrelated. */
    uint32_t mix32(uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    /** Rolls 0..99 for one position of a layout. */
    uint32_t roll(const Structures::Plan &plan, int a, int b, int c, uint32_t salt)
    {
        uint32_t h = Structures::hashSeed(plan.seed, a * 31 + b, c, salt);
        return h % 100u;
    }

    struct LootEntry
    {
        uint16_t stack;
        int min_count;
        int max_count;
        int weight; // relative, out of the sum of all weights
    };

    // Chest loot. Weighted, and deliberately top-heavy in common things: a chest
    // is a reward, not a jackpot.
    const LootEntry dungeon_loot[] = {
        { packItem(ItemCoal), 1, 8, 12 },
        { packItem(ItemBread), 1, 3, 10 },
        { packItem(ItemApple), 1, 3, 10 },
        { packItem(ItemArrow), 2, 8, 8 },
        { packItem(ItemString), 1, 4, 8 },
        { packItem(ItemGunpowder), 1, 4, 7 },
        { packItem(ItemRedstoneDust), 1, 4, 7 },
        { packItem(ItemRottenFlesh), 1, 4, 6 },
        { packItem(ItemBoneMeal), 1, 4, 6 },
        { packItem(ItemIronIngot), 1, 4, 8 },
        { packItem(ItemGoldIngot), 1, 2, 5 },
        { packItem(ItemSpiderEye), 1, 2, 4 },
        { packItem(ItemLapisLazuli), 1, 4, 4 },
        { packItem(ItemDiamond), 1, 2, 2 },
        { packItem(ItemGoldenApple), 1, 1, 1 },
        { packItem(ItemIronPickaxe), 1, 1, 1 },
    };

    const LootEntry ruin_loot[] = {
        { packItem(ItemBread), 1, 2, 14 },
        { packItem(ItemApple), 1, 2, 12 },
        { packItem(ItemWheatSeeds), 1, 4, 12 },
        { packItem(ItemStick), 1, 4, 10 },
        { packItem(ItemCoal), 1, 4, 10 },
        { packItem(ItemPaper), 1, 3, 8 },
        { packItem(ItemBoneMeal), 1, 3, 6 },
        { packItem(ItemIronIngot), 1, 2, 6 },
        { packItem(ItemRedstoneDust), 1, 3, 5 },
        { packItem(ItemGoldenApple), 1, 1, 1 },
    };

    const LootEntry temple_loot[] = {
        { packItem(ItemGoldNugget), 2, 6, 12 },
        { packItem(ItemGoldIngot), 1, 3, 10 },
        { packItem(ItemIronIngot), 1, 4, 10 },
        { packItem(ItemBoneMeal), 1, 6, 8 },
        { packItem(ItemArrow), 2, 8, 8 },
        { packItem(ItemLapisLazuli), 1, 4, 6 },
        { packItem(ItemGlowstoneDust), 1, 4, 6 },
        { packItem(ItemBook), 1, 2, 5 },
        { packItem(ItemCompass), 1, 1, 3 },
        { packItem(ItemClock), 1, 1, 3 },
        { packItem(ItemSaddle), 1, 1, 2 },
        { packItem(ItemDiamond), 1, 3, 4 },
        { packItem(ItemGoldenApple), 1, 1, 2 },
    };

    int lootRolls(int kind)
    {
        switch(kind)
        {
        case Structures::KindDungeon: return 3;
        case Structures::KindRuin:    return 2;
        case Structures::KindTemple:  return 3;
        default:                      return 0;
        }
    }
}

namespace Structures
{

uint32_t hashSeed(uint32_t world_seed, int cell_x, int cell_z, uint32_t salt)
{
    uint32_t h = mix32(world_seed ^ 0x9e3779b9u);
    h = mix32(h ^ (static_cast<uint32_t>(cell_x) * 0x85ebca6bu));
    h = mix32(h ^ (static_cast<uint32_t>(cell_z) * 0xc2b2ae35u));
    return mix32(h ^ salt);
}

bool cellHasStructure(uint32_t world_seed, int cell_x, int cell_z)
{
    return (hashSeed(world_seed, cell_x, cell_z, 1u) % 100u) < static_cast<uint32_t>(StructurePercent);
}

int cellKind(uint32_t world_seed, int cell_x, int cell_z)
{
    // Dungeons are the common find, ruins next, temples the rarest: a temple also
    // has to land in a desert, so its effective rate is lower still.
    const uint32_t r = hashSeed(world_seed, cell_x, cell_z, 2u) % 100u;
    if(r < 45u)
        return KindDungeon;
    if(r < 80u)
        return KindRuin;
    return KindTemple;
}

void cellCandidate(uint32_t world_seed, int cell_x, int cell_z, int index, int &out_x, int &out_z)
{
    const int centre_x = cell_x * CellBlocks + CellBlocks / 2;
    const int centre_z = cell_z * CellBlocks + CellBlocks / 2;

    const int span = 2 * CellJitter + 1;
    const int offset_x = static_cast<int>(hashSeed(world_seed, cell_x, cell_z, 10u + index) % static_cast<uint32_t>(span)) - CellJitter;
    const int offset_z = static_cast<int>(hashSeed(world_seed, cell_x, cell_z, 20u + index) % static_cast<uint32_t>(span)) - CellJitter;

    out_x = centre_x + offset_x;
    out_z = centre_z + offset_z;
}

bool planStructure(uint32_t world_seed, int cell_x, int cell_z, int candidate, int ground_y, int biome, Plan &out)
{
    if(candidate < 0 || candidate >= CandidateCount)
        return false;
    if(!cellHasStructure(world_seed, cell_x, cell_z))
        return false;

    const int kind = cellKind(world_seed, cell_x, cell_z);

    int origin_x, origin_z;
    cellCandidate(world_seed, cell_x, cell_z, candidate, origin_x, origin_z);

    if(kind == KindDungeon)
    {
        // Buried: the room needs rock above it, or it would be a hole in a field.
        if(ground_y < DungeonMinGroundY)
            return false;

        // Buried well below the surface: eight blocks of rock above the ceiling,
        // three open layers, then the floor. The floor has to be inside the world.
        const int ceiling_y = ground_y - 8;
        const int first_open_y = ceiling_y - DungeonOpenLayers;
        if(first_open_y - 1 < 1)
            return false;

        out.valid = true;
        out.kind = kind;
        out.cell_x = cell_x;
        out.cell_z = cell_z;
        out.origin_x = origin_x;
        out.origin_y = first_open_y;
        out.origin_z = origin_z;
        out.seed = hashSeed(world_seed, cell_x, cell_z, 30u + static_cast<uint32_t>(candidate));
        return true;
    }

    // Surface structures stand on dry land.
    if(ground_y < SurfaceMinGroundY || ground_y > SurfaceMaxGroundY)
        return false;

    if(kind == KindTemple)
    {
        // A temple is a desert find, and its steps need room under the ceiling.
        if(biome != BiomeGen::BiomeDesert)
            return false;
        if(ground_y + TempleTopY + 1 > BiomeGen::MaxHeight)
            return false;
    }
    else if(ground_y + RuinRoofY + 1 > BiomeGen::MaxHeight)
        return false;

    out.valid = true;
    out.kind = kind;
    out.cell_x = cell_x;
    out.cell_z = cell_z;
    out.origin_x = origin_x;
    out.origin_y = ground_y; // the first block above the surface block
    out.origin_z = origin_z;
    out.seed = hashSeed(world_seed, cell_x, cell_z, 30u + static_cast<uint32_t>(candidate));
    return true;
}

int highestY(const Plan &plan)
{
    switch(plan.kind)
    {
    case KindDungeon: return plan.origin_y + DungeonOpenLayers;   // the ceiling
    case KindRuin:    return plan.origin_y + RuinRoofY;           // the roof
    case KindTemple:  return plan.origin_y + TempleTopY;          // the top step
    default:          return plan.origin_y;
    }
}

namespace
{

/** The chest of a dungeon sits in the far corner of the room. */
void dungeonChestPosition(const Plan &plan, int &x, int &y, int &z)
{
    x = plan.origin_x + DungeonHalf - 1;
    y = plan.origin_y;
    z = plan.origin_z + DungeonHalf - 1;
}

void ruinChestPosition(const Plan &plan, int &x, int &y, int &z)
{
    x = plan.origin_x - RuinHalf + 1;
    y = plan.origin_y;
    z = plan.origin_z + RuinHalf - 1;
}

void templeChestPosition(const Plan &plan, int &x, int &y, int &z)
{
    x = plan.origin_x;
    y = plan.origin_y - 2;
    z = plan.origin_z;
}

bool chestSlot(const Plan &plan, int dx, int dy, int dz, int &out_index)
{
    int x = 0, y = 0, z = 0;
    for(int i = 0; i < chestCount(plan); ++i)
    {
        if(i == 0)
        {
            switch(plan.kind)
            {
            case KindDungeon: dungeonChestPosition(plan, x, y, z); break;
            case KindRuin:    ruinChestPosition(plan, x, y, z); break;
            case KindTemple:  templeChestPosition(plan, x, y, z); break;
            default: return false;
            }
        }
        else if(plan.kind == KindRuin && i == 1)
        {
            // A second, smaller chest in the opposite corner of one ruin in four.
            if(roll(plan, 0, 0, 0, 91u) >= 25u)
                return false;
            x = plan.origin_x + RuinHalf - 1;
            y = plan.origin_y;
            z = plan.origin_z - RuinHalf + 1;
        }

        if(dx == x - plan.origin_x && dy == y - plan.origin_y && dz == z - plan.origin_z)
        {
            out_index = i;
            return true;
        }
    }
    return false;
}

bool dungeonBlock(const Plan &plan, int dx, int dy, int dz, uint16_t &out)
{
    if(dx < -DungeonHalf || dx > DungeonHalf || dz < -DungeonHalf || dz > DungeonHalf)
        return false;
    if(dy < -1 || dy > DungeonOpenLayers)
        return false;

    const bool shell = (dx == -DungeonHalf || dx == DungeonHalf || dz == -DungeonHalf || dz == DungeonHalf);

    // Floor and ceiling, with a single glowstone as the only light in the room.
    if(dy == -1 || dy == DungeonOpenLayers)
    {
        out = (dy == DungeonOpenLayers && dx == 0 && dz == 0) ? Glowstone : Cobblestone;
        return true;
    }

    if(shell)
    {
        out = Cobblestone;
        return true;
    }

    int chest_index = 0;
    if(chestSlot(plan, dx, dy, dz, chest_index))
    {
        out = Chest;
        return true;
    }

    // Cobwebs in the corners, which is what a dungeon is remembered for. They go
    // inside the room, never in a wall, so the shell stays sealed.
    if(dy <= 1 && (dx <= -2 || dx >= 2) && (dz <= -2 || dz >= 2))
    {
        if(roll(plan, dx, dz, dy, 40u) < 40u)
        {
            out = Spiderweb;
            return true;
        }
    }

    out = Air;
    return true;
}

bool ruinBlock(const Plan &plan, int dx, int dy, int dz, uint16_t &out)
{
    if(dx < -RuinHalf || dx > RuinHalf || dz < -RuinHalf || dz > RuinHalf)
        return false;

    const bool wall = (dx == -RuinHalf || dx == RuinHalf || dz == -RuinHalf || dz == RuinHalf);

    if(dy < 0)
        return false;

    if(dy == RuinRoofY)
    {
        // The corners are gone and so are a third of the planks: it collapsed.
        if(wall && (dx == -RuinHalf || dx == RuinHalf) && (dz == -RuinHalf || dz == RuinHalf))
            return false;
        if(roll(plan, dx, dz, dy, 41u) < 30u)
            return false;
        out = PlanksDark;
        return true;
    }

    if(dy > RuinWallHeight - 1)
        return false;

    if(wall)
    {
        // A doorway on the -z side, and gaps where the wall fell down.
        if(dz == -RuinHalf && dy <= 1 && dx >= -1 && dx <= 0)
            return false;
        if(roll(plan, dx, dz, dy, 42u) < 25u)
            return false;
        out = Cobblestone;
        return true;
    }

    int chest_index = 0;
    if(chestSlot(plan, dx, dy, dz, chest_index))
    {
        out = Chest;
        return true;
    }

    // Rubble on the floor, and a clear interior above it.
    if(dy == 0 && roll(plan, dx, dz, dy, 43u) < 12u)
    {
        out = (roll(plan, dz, dx, dy, 44u) < 50u) ? Cobblestone : PlanksDark;
        return true;
    }

    out = Air;
    return true;
}

bool templeBlock(const Plan &plan, int dx, int dy, int dz, uint16_t &out)
{
    if(dx < -TempleHalf || dx > TempleHalf || dz < -TempleHalf || dz > TempleHalf)
        return false;
    if(dy > TempleTopY || dy < TempleChamberFloorY)
        return false;

    // The steps: 9x9, then 7x7, then 5x5, then 3x3, all solid.
    if(dy >= 0)
    {
        const int reach = TempleHalf - dy;
        if(reach < 0)
            return false;
        if(dx < -reach || dx > reach || dz < -reach || dz > reach)
            return false;

        out = Sand;
        return true;
    }

    // Under the floor: a cobblestone shell around a 3x3 chamber, with the chest
    // in the middle of its floor. The way in is to dig, like the real thing.
    const bool inside = (dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1);
    if(dy >= -2 && inside)
    {
        int chest_index = 0;
        if(dy == -2 && chestSlot(plan, dx, dy, dz, chest_index))
        {
            out = Chest;
            return true;
        }
        out = Air;
        return true;
    }

    out = Cobblestone;
    return true;
}

/** The layout of one structure: false when it defines no block there. */
bool blockAt(const Plan &plan, int dx, int dy, int dz, uint16_t &out)
{
    switch(plan.kind)
    {
    case KindDungeon: return dungeonBlock(plan, dx, dy, dz, out);
    case KindRuin:    return ruinBlock(plan, dx, dy, dz, out);
    case KindTemple:  return templeBlock(plan, dx, dy, dz, out);
    default:          return false;
    }
}

/** Half-extent of the layout, for the emission loop. */
int halfExtent(int kind)
{
    switch(kind)
    {
    case KindDungeon: return DungeonHalf;
    case KindRuin:    return RuinHalf;
    case KindTemple:  return TempleHalf;
    default:          return 0;
    }
}

} // namespace

void emitChunk(const Plan &plan, int chunk_x, int chunk_y, int chunk_z, SetBlockFn set, void *context)
{
    if(!plan.valid || set == nullptr)
        return;

    const int half = halfExtent(plan.kind);
    if(half <= 0)
        return;

    const int low_y = plan.kind == KindTemple ? TempleChamberFloorY : (plan.kind == KindDungeon ? -1 : 0);
    const int high_y = plan.kind == KindDungeon ? DungeonOpenLayers : (plan.kind == KindTemple ? TempleTopY : RuinRoofY);

    const int chunk_base_x = chunk_x * ChunkBlocks;
    const int chunk_base_y = chunk_y * ChunkBlocks;
    const int chunk_base_z = chunk_z * ChunkBlocks;

    for(int dy = low_y; dy <= high_y; ++dy)
        for(int dz = -half; dz <= half; ++dz)
            for(int dx = -half; dx <= half; ++dx)
            {
                uint16_t block = 0;
                if(!blockAt(plan, dx, dy, dz, block))
                    continue;

                const int world_x = plan.origin_x + dx;
                const int world_y = plan.origin_y + dy;
                const int world_z = plan.origin_z + dz;

                // Clip to the chunk. Every chunk writes its own slice, so no
                // bookkeeping and no pending changes are needed.
                if(world_x < chunk_base_x || world_x >= chunk_base_x + ChunkBlocks)
                    continue;
                if(world_y < chunk_base_y || world_y >= chunk_base_y + ChunkBlocks)
                    continue;
                if(world_z < chunk_base_z || world_z >= chunk_base_z + ChunkBlocks)
                    continue;

                set(context, world_x, world_y, world_z, block);
            }
}

bool blockAtWorld(const Plan &plan, int world_x, int world_y, int world_z, uint16_t &out)
{
    if(!plan.valid)
        return false;
    return blockAt(plan, world_x - plan.origin_x, world_y - plan.origin_y, world_z - plan.origin_z, out);
}

int chestCount(const Plan &plan)
{
    if(!plan.valid)
        return 0;

    switch(plan.kind)
    {
    case KindDungeon:
        return 1;
    case KindRuin:
        // One ruin in four has a second chest; the roll is part of the layout
        // seed, so the count is the same however the chunk is visited.
        return roll(plan, 0, 0, 0, 91u) < 25u ? 2 : 1;
    case KindTemple:
        return 1;
    default:
        return 0;
    }
}

bool chestPosition(const Plan &plan, int index, int &out_x, int &out_y, int &out_z)
{
    if(index < 0 || index >= chestCount(plan))
        return false;

    switch(plan.kind)
    {
    case KindDungeon:
        dungeonChestPosition(plan, out_x, out_y, out_z);
        return true;
    case KindTemple:
        templeChestPosition(plan, out_x, out_y, out_z);
        return true;
    case KindRuin:
        if(index == 0)
        {
            ruinChestPosition(plan, out_x, out_y, out_z);
            return true;
        }
        out_x = plan.origin_x + RuinHalf - 1;
        out_y = plan.origin_y;
        out_z = plan.origin_z - RuinHalf + 1;
        return true;
    default:
        return false;
    }
}

int chestLoot(const Plan &plan, int index, Loot *out, int max_stacks)
{
    if(out == nullptr || max_stacks <= 0 || index < 0 || index >= chestCount(plan))
        return 0;

    const LootEntry *table = nullptr;
    int entries = 0;
    switch(plan.kind)
    {
    case KindDungeon:
        table = dungeon_loot;
        entries = static_cast<int>(sizeof(dungeon_loot) / sizeof(dungeon_loot[0]));
        break;
    case KindRuin:
        table = ruin_loot;
        entries = static_cast<int>(sizeof(ruin_loot) / sizeof(ruin_loot[0]));
        break;
    case KindTemple:
        table = temple_loot;
        entries = static_cast<int>(sizeof(temple_loot) / sizeof(temple_loot[0]));
        break;
    default:
        return 0;
    }
    if(table == nullptr || entries <= 0)
        return 0;

    int total_weight = 0;
    for(int i = 0; i < entries; ++i)
        total_weight += table[i].weight;

    int rolls = lootRolls(plan.kind);
    if(rolls > max_stacks)
        rolls = max_stacks;

    int written = 0;
    for(int r = 0; r < rolls; ++r)
    {
        if(total_weight <= 0)
            break;

        const uint32_t h = hashSeed(plan.seed, index * 97 + r, r, 55u + static_cast<uint32_t>(r));

        int pick = static_cast<int>(h % static_cast<uint32_t>(total_weight));
        const LootEntry *chosen = nullptr;
        for(int i = 0; i < entries; ++i)
        {
            if(pick < table[i].weight)
            {
                chosen = &table[i];
                break;
            }
            pick -= table[i].weight;
        }
        if(chosen == nullptr || chosen->max_count <= 0)
            continue;

        const int span = chosen->max_count - chosen->min_count + 1;
        const int count = chosen->min_count + static_cast<int>((h >> 13) % static_cast<uint32_t>(span));

        out[written].stack = chosen->stack;
        out[written].count = static_cast<unsigned int>(count);
        ++written;
    }

    return written;
}

} // namespace Structures
