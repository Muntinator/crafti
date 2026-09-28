// Host tests for the item rules (itemrules.cpp).
//
// The module is the durability side of Minecraft 1.4: tables of uses for every
// tool and weapon, armour points and durability for every piece, the tool tiers
// the mining code harvests with, and the lifetime of a dropped stack. It is pure
// (a function of the stack value), so the whole table can be pinned down here.
//
// The interesting failure this guards against is the one the item atlas already
// caused once: 49 of the 177 item ids are 128 or more, and reading one with
// getBLOCKDATA() truncates it into a different item (an iron ingot became an iron
// helmet). Every lookup goes through getITEMDATA(), and the hoes (128..132) and
// BOW_PULLING_3 (133) below are exactly the ids that broke.
//
// Build and run with `make -C tests`.

#include "itemrules.h"

#include <stdio.h>

#include "textures/items.h"

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

#define CHECK_EQ(actual, expected) do { ++checks; \
    const long _a = (long)(actual); const long _e = (long)(expected); \
    if(_a != _e) { printf("FAIL %s:%d: %s = %ld, expected %ld\n", __FILE__, __LINE__, #actual, _a, _e); ++failures; } } while(0)

using namespace ItemRules;

static BLOCK_WDATA item(ItemTexture texture)
{
    return getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(texture));
}

static void test_tool_uses()
{
    // Wooden, stone, iron, diamond, golden — the order every tool run uses.
    struct Run { ItemTexture first; const char *name; };
    const Run runs[] = {
        { ItemTexture::WOODEN_SWORD,   "sword" },
        { ItemTexture::WOODEN_SHOVEL,  "shovel" },
        { ItemTexture::WOODEN_PICKAXE, "pickaxe" },
        { ItemTexture::WOODEN_AXE,     "axe" },
        { ItemTexture::WOODEN_HOE,     "hoe" },
    };

    const int uses[5] = { 59, 131, 250, 1561, 32 };

    for(const Run &run : runs)
    {
        for(int material = 0; material < 5; ++material)
        {
            const BLOCK_WDATA stack = item(static_cast<ItemTexture>(static_cast<uint8_t>(run.first) + material));
            CHECK_EQ(maxDamage(stack), uses[material]);
            CHECK(isDamageable(stack));
            CHECK_EQ(maxStackSize(stack), 1);
            if(maxDamage(stack) != uses[material])
                printf("  (%s material %d, id %u)\n", run.name, material, getITEMDATA(stack));
        }
    }

    // Shears, flint and steel, the rod and the bow wear out too, with their own
    // numbers, and the bow is one id per pull stage.
    CHECK_EQ(maxDamage(item(ItemTexture::SHEARS)), 238);
    CHECK_EQ(maxDamage(item(ItemTexture::FLINT_AND_STEEL)), 64);
    CHECK_EQ(maxDamage(item(ItemTexture::FISHING_ROD)), 64);
    CHECK_EQ(maxDamage(item(ItemTexture::BOW_PULLING_1)), 384);
    CHECK_EQ(maxDamage(item(ItemTexture::BOW_PULLING_3)), 384);

    // Things that are not tools never wear.
    CHECK(!isDamageable(item(ItemTexture::IRON_INGOT)));
    CHECK(!isDamageable(item(ItemTexture::STICK)));
    CHECK_EQ(maxDamage(item(ItemTexture::IRON_INGOT)), 0);
    CHECK_EQ(maxDamage(item(ItemTexture::DIAMOND)), 0);
    CHECK(!isDamageable(BLOCK_STONE));
    CHECK_EQ(maxDamage(getBLOCKWDATA(BLOCK_STONE, 0)), 0);

    // Blocks stack, tools do not.
    CHECK_EQ(maxStackSize(getBLOCKWDATA(BLOCK_DIRT, 0)), 64);
    CHECK_EQ(maxStackSize(item(ItemTexture::BONE_MEAL)), 64);
}

static void test_ids_above_127()
{
    // The hoes are 128..132 and BOW_PULLING_3 is 133: reading the data byte with
    // getBLOCKDATA() would return 0..4 and 5 instead, which are helmets and flint
    // and steel. getITEMDATA() must be what the rules use.
    CHECK_EQ(getITEMDATA(item(ItemTexture::WOODEN_HOE)), static_cast<uint8_t>(ItemTexture::WOODEN_HOE));
    CHECK(getITEMDATA(item(ItemTexture::WOODEN_HOE)) >= 128);
    CHECK_EQ(maxDamage(item(ItemTexture::WOODEN_HOE)), 59);
    CHECK_EQ(maxDamage(item(ItemTexture::GOLDEN_HOE)), 32);

    // The truncated reading really is a different item, which is why this matters.
    CHECK_EQ(getBLOCKDATA(item(ItemTexture::WOODEN_HOE)), 0);

    CHECK_EQ(toolKind(item(ItemTexture::DIAMOND_HOE)), Tool::Hoe);
    CHECK_EQ(toolKind(item(ItemTexture::GOLDEN_HOE)), Tool::Hoe);
    CHECK_EQ(toolKind(item(ItemTexture::IRON_HOE)), Tool::Hoe);

    // And a high-id item that is not a tool stays a non-tool.
    CHECK(!isDamageable(item(ItemTexture::COOKED_SALMON)));
    CHECK(!isDamageable(item(ItemTexture::ROTTEN_FLESH)));
}

static void test_armor_table()
{
    // Rows: leather, chainmail, iron, diamond, golden. Columns: helmet, chest,
    // legs, boots — the order of the ids in the atlas.
    static const int durability[5][4] = {
        { 55,  80,  75,  65 },
        { 165, 240, 225, 195 },
        { 165, 240, 225, 195 },
        { 363, 528, 495, 429 },
        { 77,  112, 105, 91  },
    };
    static const int points[5][4] = {
        { 1, 3, 2, 1 },
        { 2, 5, 4, 1 },
        { 2, 6, 5, 2 },
        { 3, 8, 6, 3 },
        { 2, 5, 3, 1 },
    };
    const ItemTexture first_of_piece[4] = {
        ItemTexture::LEATHER_HELMET, ItemTexture::LEATHER_CHESTPLATE,
        ItemTexture::LEATHER_LEGGINGS, ItemTexture::LEATHER_BOOTS,
    };

    for(int piece = 0; piece < 4; ++piece)
        for(int material = 0; material < 5; ++material)
        {
            const BLOCK_WDATA stack = item(static_cast<ItemTexture>(static_cast<uint8_t>(first_of_piece[piece]) + material));
            CHECK_EQ(armorSlot(stack), piece);
            CHECK_EQ(maxDamage(stack), durability[material][piece]);
            CHECK_EQ(armorPoints(stack), points[material][piece]);
            CHECK(isDamageable(stack));
            CHECK_EQ(maxStackSize(stack), 1);
            CHECK_EQ(toolKind(stack), Tool::None);
            if(maxDamage(stack) != durability[material][piece] || armorPoints(stack) != points[material][piece])
                printf("  (piece %d material %d, id %u)\n", piece, material, getITEMDATA(stack));
        }

    // A full diamond set is the strongest armour the game has: 20 points, which
    // the 80% cap keeps from being a 80% reduction in practice.
    CHECK_EQ(armorSlot(BLOCK_STONE), NoArmorSlot);
    CHECK_EQ(armorSlot(item(ItemTexture::IRON_INGOT)), NoArmorSlot);
    CHECK_EQ(armorPoints(item(ItemTexture::IRON_INGOT)), 0);
    CHECK_EQ(armorPoints(item(ItemTexture::DIAMOND_PICKAXE)), 0);
    CHECK_EQ(armorSlot(item(ItemTexture::GOLDEN_HORSE_ARMOR)), NoArmorSlot);

    const int full_diamond = armorPoints(item(ItemTexture::DIAMOND_HELMET))
                           + armorPoints(item(ItemTexture::DIAMOND_CHESTPLATE))
                           + armorPoints(item(ItemTexture::DIAMOND_LEGGINGS))
                           + armorPoints(item(ItemTexture::DIAMOND_BOOTS));
    CHECK_EQ(full_diamond, 20);
}

static void test_tool_kinds_and_tiers()
{
    CHECK_EQ(toolKind(item(ItemTexture::DIAMOND_PICKAXE)), Tool::Pickaxe);
    CHECK_EQ(toolKind(item(ItemTexture::IRON_AXE)), Tool::Axe);
    CHECK_EQ(toolKind(item(ItemTexture::STONE_SHOVEL)), Tool::Shovel);
    CHECK_EQ(toolKind(item(ItemTexture::WOODEN_SWORD)), Tool::Sword);
    CHECK_EQ(toolKind(item(ItemTexture::SHEARS)), Tool::Shears);
    CHECK_EQ(toolKind(item(ItemTexture::FLINT_AND_STEEL)), Tool::FlintAndSteel);
    CHECK_EQ(toolKind(item(ItemTexture::FISHING_ROD)), Tool::FishingRod);
    CHECK_EQ(toolKind(item(ItemTexture::BOW_PULLING_2)), Tool::Bow);
    CHECK_EQ(toolKind(item(ItemTexture::BREAD)), Tool::None);
    CHECK_EQ(toolKind(getBLOCKWDATA(BLOCK_STONE, 0)), Tool::None);

    CHECK_EQ(pickaxeTier(item(ItemTexture::WOODEN_PICKAXE)), 1);
    CHECK_EQ(pickaxeTier(item(ItemTexture::STONE_PICKAXE)), 2);
    CHECK_EQ(pickaxeTier(item(ItemTexture::IRON_PICKAXE)), 3);
    CHECK_EQ(pickaxeTier(item(ItemTexture::DIAMOND_PICKAXE)), 4);
    CHECK_EQ(pickaxeTier(item(ItemTexture::GOLDEN_PICKAXE)), 5);
    // Not a pickaxe, so no tier: this is the "bare hands" case the mining code
    // checks against.
    CHECK_EQ(pickaxeTier(item(ItemTexture::DIAMOND_AXE)), 0);
    CHECK_EQ(pickaxeTier(item(ItemTexture::DIAMOND_SWORD)), 0);
    CHECK_EQ(pickaxeTier(BLOCK_AIR), 0);

    CHECK_EQ(requiredPickaxeTierForDrop(BLOCK_STONE), 1);
    CHECK_EQ(requiredPickaxeTierForDrop(BLOCK_IRON_ORE), 2);
    CHECK_EQ(requiredPickaxeTierForDrop(BLOCK_DIAMOND_ORE), 3);
    CHECK_EQ(requiredPickaxeTierForDrop(BLOCK_DIRT), 0);
    CHECK(isPickaxeMinedBlock(BLOCK_STONE));
    CHECK(!isPickaxeMinedBlock(BLOCK_DIRT));
    CHECK(!isPickaxeMinedBlock(BLOCK_SAND));

    // Every harvestable block must actually be reachable: the best pickaxe in the
    // game is tier 5 (golden), so nothing may require more than that.
    const BLOCK blocks[] = { BLOCK_STONE, BLOCK_COBBLESTONE, BLOCK_COAL_ORE, BLOCK_FURNACE, BLOCK_NETHERRACK,
                             BLOCK_IRON_ORE, BLOCK_IRON, BLOCK_GOLD_ORE, BLOCK_GOLD, BLOCK_DIAMOND_ORE,
                             BLOCK_DIAMOND, BLOCK_REDSTONE_ORE };
    for(BLOCK b : blocks)
        CHECK(requiredPickaxeTierForDrop(b) <= 5);
}

static void test_wear_costs()
{
    // Mining wears tools by one, and by nothing else.
    CHECK_EQ(durabilityPerBlockMined(item(ItemTexture::WOODEN_PICKAXE)), 1);
    CHECK_EQ(durabilityPerBlockMined(item(ItemTexture::DIAMOND_AXE)), 1);
    CHECK_EQ(durabilityPerBlockMined(item(ItemTexture::GOLDEN_SHOVEL)), 1);
    CHECK_EQ(durabilityPerBlockMined(item(ItemTexture::IRON_SWORD)), 1);
    CHECK_EQ(durabilityPerBlockMined(item(ItemTexture::SHEARS)), 1);
    CHECK_EQ(durabilityPerBlockMined(item(ItemTexture::IRON_INGOT)), 0);
    CHECK_EQ(durabilityPerBlockMined(getBLOCKWDATA(BLOCK_DIRT, 0)), 0);

    // Attacking wears a sword by one and any other tool by two.
    CHECK_EQ(durabilityPerAttack(item(ItemTexture::DIAMOND_SWORD)), 1);
    CHECK_EQ(durabilityPerAttack(item(ItemTexture::WOODEN_SWORD)), 1);
    CHECK_EQ(durabilityPerAttack(item(ItemTexture::DIAMOND_PICKAXE)), 2);
    CHECK_EQ(durabilityPerAttack(item(ItemTexture::STONE_AXE)), 2);
    CHECK_EQ(durabilityPerAttack(item(ItemTexture::SHEARS)), 0);
    CHECK_EQ(durabilityPerAttack(item(ItemTexture::APPLE)), 0);

    CHECK_EQ(durabilityPerUse(item(ItemTexture::FLINT_AND_STEEL)), 1);
    CHECK_EQ(durabilityPerUse(item(ItemTexture::FISHING_ROD)), 1);
    CHECK_EQ(durabilityPerUse(item(ItemTexture::BOW_PULLING_3)), 1);
    CHECK_EQ(durabilityPerUse(item(ItemTexture::IRON_PICKAXE)), 0);
}

static void test_damage_and_breaking()
{
    unsigned short damage = 0;

    CHECK(!applyDamage(damage, 59, 1));
    CHECK_EQ(damage, 1);
    CHECK(!applyDamage(damage, 59, 10));
    CHECK_EQ(damage, 11);
    CHECK_EQ(remainingDurability(damage, 59), 48);

    // The use that reaches the limit breaks the item...
    CHECK(!applyDamage(damage, 59, 47));
    CHECK_EQ(damage, 58);
    CHECK(applyDamage(damage, 59, 1));
    CHECK_EQ(damage, 59);
    CHECK_EQ(remainingDurability(damage, 59), 0);

    // ...and overshooting it does too, without running past the maximum.
    damage = 0;
    CHECK(applyDamage(damage, 32, 100));
    CHECK_EQ(damage, 32);

    // An indestructible stack is never worn down and never breaks: this is the
    // guard the mining code relies on for blocks and food.
    damage = 0;
    CHECK(!applyDamage(damage, 0, 5));
    CHECK_EQ(damage, 0);
    CHECK(!applyDamage(damage, 59, 0));
    CHECK_EQ(damage, 0);

    // A fresh item survives exactly its table value of uses, no more.
    unsigned short worn = 0;
    int uses = 0;
    while(!applyDamage(worn, 250, 1) && uses < 10000)
        ++uses;
    CHECK_EQ(uses + 1, 250);
    CHECK_EQ(worn, 250);
}

static void test_armor_reduction()
{
    CHECK_EQ(armorDamageReductionPercent(0), 0);
    CHECK_EQ(armorDamageReductionPercent(-3), 0);
    CHECK_EQ(armorDamageReductionPercent(1), 4);
    CHECK_EQ(armorDamageReductionPercent(5), 20);
    CHECK_EQ(armorDamageReductionPercent(20), 80);
    // Beyond 20 points the cap holds.
    CHECK_EQ(armorDamageReductionPercent(25), 80);
    CHECK_EQ(armorDamageReductionPercent(1000), 80);

    // No armour leaves the damage alone.
    CHECK_EQ(reduceDamageWithArmor(7, 0), 7);
    // 5 points is 20%: 10 damage becomes 8.
    CHECK_EQ(reduceDamageWithArmor(10, 5), 8);
    // A full diamond set halves the damage of a big hit.
    CHECK_EQ(reduceDamageWithArmor(20, 20), 4);
    // Armour never makes a hit harmless, however small it was.
    CHECK_EQ(reduceDamageWithArmor(2, 20), 1);
    CHECK_EQ(reduceDamageWithArmor(1, 20), 1);
    // Zero damage stays zero.
    CHECK_EQ(reduceDamageWithArmor(0, 20), 0);
    // Reduction is always monotonic in the armour points.
    int previous = 1000;
    for(int points = 0; points <= 20; ++points)
    {
        const int reduced = reduceDamageWithArmor(100, points);
        CHECK(reduced <= previous);
        previous = reduced;
    }
    CHECK_EQ(ArmorDamagePerHit, 1);
}

static void test_drop_lifetime()
{
    // Half a second on the ground before it can be collected, so a block you
    // broke does not snap into the inventory mid-swing.
    CHECK(!dropPickupable(0));
    CHECK(!dropPickupable(DropPickupDelayMs - 1));
    CHECK(dropPickupable(DropPickupDelayMs));
    CHECK(dropPickupable(DropPickupDelayMs + 1));
    CHECK_EQ(DropPickupDelayMs, 500);

    // Five minutes, then it is gone. Real milliseconds, so a 300 ms calculator
    // tick and a 33 ms desktop tick agree on how long an item lasts.
    CHECK(!dropExpired(0));
    CHECK(!dropExpired(DropLifetimeMs - 1));
    CHECK(dropExpired(DropLifetimeMs));
    CHECK(dropExpired(DropLifetimeMs + 1));
    CHECK_EQ(DropLifetimeMs, 300000L);
    CHECK_EQ(DropLifetimeMs / 1000, 300);

    // The countdown runs down and stops at zero.
    CHECK_EQ(dropMsRemaining(0), DropLifetimeMs);
    CHECK_EQ(dropMsRemaining(DropLifetimeMs - 1), 1);
    CHECK_EQ(dropMsRemaining(DropLifetimeMs), 0);
    CHECK_EQ(dropMsRemaining(DropLifetimeMs * 3), 0);

    // The pickup delay is inside the lifetime, or a drop would expire before it
    // could ever be picked up.
    CHECK(DropPickupDelayMs < DropLifetimeMs);
}

// The melee table: vanilla 1.4's damage per material and kind, in half-hearts,
// with a fist at one point. What the game does with it is decided in
// worldtask.cpp (plus the weapon's enchantments), but the numbers themselves are
// this table, so they are pinned here -- the creeper has 20 health, which is why
// the values matter to how a fight goes.
static void test_attack_damage()
{
    static const int sword[5]   = { 4, 5, 6, 7, 4 };
    static const int axe[5]     = { 3, 4, 5, 6, 3 };
    static const int pickaxe[5] = { 2, 3, 4, 5, 2 };
    static const int shovel[5]  = { 1, 2, 3, 4, 1 };

    static const ItemTexture sword_run[5]   = { ItemTexture::WOODEN_SWORD, ItemTexture::STONE_SWORD,
                                                ItemTexture::IRON_SWORD, ItemTexture::DIAMOND_SWORD,
                                                ItemTexture::GOLDEN_SWORD };
    static const ItemTexture axe_run[5]     = { ItemTexture::WOODEN_AXE, ItemTexture::STONE_AXE,
                                                ItemTexture::IRON_AXE, ItemTexture::DIAMOND_AXE,
                                                ItemTexture::GOLDEN_AXE };
    static const ItemTexture pickaxe_run[5] = { ItemTexture::WOODEN_PICKAXE, ItemTexture::STONE_PICKAXE,
                                                ItemTexture::IRON_PICKAXE, ItemTexture::DIAMOND_PICKAXE,
                                                ItemTexture::GOLDEN_PICKAXE };
    static const ItemTexture shovel_run[5]  = { ItemTexture::WOODEN_SHOVEL, ItemTexture::STONE_SHOVEL,
                                                ItemTexture::IRON_SHOVEL, ItemTexture::DIAMOND_SHOVEL,
                                                ItemTexture::GOLDEN_SHOVEL };

    for(int material = 0; material < 5; ++material)
    {
        CHECK_EQ(attackDamage(item(sword_run[material])), sword[material]);
        CHECK_EQ(attackDamage(item(axe_run[material])), axe[material]);
        CHECK_EQ(attackDamage(item(pickaxe_run[material])), pickaxe[material]);
        CHECK_EQ(attackDamage(item(shovel_run[material])), shovel[material]);
    }

    // Hoes are not weapons, and a fist is a fist: one point. Blocks, items that
    // are not tools, and the bow all sit at the hand's value.
    for(int material = 0; material < 5; ++material)
        CHECK_EQ(attackDamage(getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::WOODEN_HOE) + material)),
                 HandAttackDamage);

    CHECK_EQ(attackDamage(BLOCK_AIR), HandAttackDamage);
    CHECK_EQ(attackDamage(getBLOCKWDATA(BLOCK_STONE, 0)), HandAttackDamage);
    CHECK_EQ(attackDamage(item(ItemTexture::STICK)), HandAttackDamage);
    CHECK_EQ(attackDamage(item(ItemTexture::BOW_PULLING_1)), HandAttackDamage);
    CHECK_EQ(attackDamage(item(ItemTexture::DIAMOND_BOOTS)), HandAttackDamage);

    // A diamond sword takes a 20-health creeper down in three hits and never in
    // two, which is the vanilla fight.
    CHECK_EQ(20 / attackDamage(item(ItemTexture::DIAMOND_SWORD)), 2);
    CHECK_EQ((20 + attackDamage(item(ItemTexture::DIAMOND_SWORD)) - 1)
             / attackDamage(item(ItemTexture::DIAMOND_SWORD)), 3);

    // A sword is the best weapon of its material -- that is what makes it a sword.
    CHECK(attackDamage(item(ItemTexture::IRON_SWORD)) > attackDamage(item(ItemTexture::IRON_AXE)));
    CHECK(attackDamage(item(ItemTexture::IRON_AXE)) > attackDamage(item(ItemTexture::IRON_PICKAXE)));
    CHECK(attackDamage(item(ItemTexture::IRON_PICKAXE)) > attackDamage(item(ItemTexture::IRON_SHOVEL)));
}

int main()
{
    test_tool_uses();
    test_ids_above_127();
    test_armor_table();
    test_attack_damage();
    test_tool_kinds_and_tiers();
    test_wear_costs();
    test_damage_and_breaking();
    test_armor_reduction();
    test_drop_lifetime();

    printf("itemrules_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
