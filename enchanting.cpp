/**
 * Enchantments. See enchanting.h for what is vanilla here and what is this
 * game's own.
 *
 * The registry below is the 1.8-era table: the weight column decides how likely
 * an enchantment is to be offered, the minimum and maximum enchantability decide
 * whether a spell level is high enough to reach it, and the item-type mask
 * decides who can carry it. The costs are written as the level-1 range plus the
 * per-level step the wiki lists (min(2) = min1 + step, and so on), which is how
 * the per-level tables compress.
 *
 * Nothing here reads the world: it is handed an item, a bookshelf count and a
 * seed, and answers with what the table would offer and what the anvil would do.
 */

#include "enchanting.h"

namespace
{
    // Item ids, mirroring ItemTexture in textures/items.h. They are kept as
    // numbers so this module stays free of the texture atlas, and the host test
    // checks every one of them against that header.
    constexpr uint8_t LeatherHelmet = 0, ChainHelmet = 1, IronHelmet = 2, DiamondHelmet = 3, GoldHelmet = 4;
    constexpr uint8_t HelmetBase = 0, ChestBase = 16, LegsBase = 32, BootsBase = 48;
    constexpr uint8_t ArmorRow = 16; // ids repeat every armour row, so material = id % 16
    constexpr uint8_t Book = 59;
    constexpr uint8_t SwordBase = 64, ShovelBase = 80, PickaxeBase = 96, AxeBase = 112, HoeBase = 128;
    constexpr uint8_t ToolRow = 16;
    constexpr uint8_t Bow = 101, BowPulling2 = 117, BowPulling3 = 133;

    // The five tool/armour materials, in the games' own order: wood/leather,
    // stone/chain, iron, diamond, gold.
    constexpr int MaterialWood = 0, MaterialStone = 1, MaterialIron = 2, MaterialDiamond = 3, MaterialGold = 4;

    /** Vanilla enchantability per material, tools then armour. */
    constexpr int ToolEnchantability[5] = { 15, 5, 14, 10, 22 };
    constexpr int ArmorEnchantability[5] = { 15, 12, 9, 10, 25 };

    /**
     * A repair material per material index, as item ids, in the same five rows as
     * everything else: wood is repaired with planks (a block id, which is an item
     * like any other block), stone with cobblestone, and the metals with their own
     * ingots. The test pins them against terrain.h and textures/items.h.
     */
    constexpr uint8_t RepairMaterial[5] = {
        6,   // wood    -> BLOCK_PLANKS_NORMAL
        24,  // stone   -> BLOCK_COBBLESTONE
        145, // iron    -> ItemTexture::IRON_INGOT
        55,  // diamond -> ItemTexture::DIAMOND
        39,  // gold    -> ItemTexture::GOLD_INGOT
    };
}

// The registry. Order matches Enchanting::Id exactly, and the test pins that.
static const Enchanting::Def defs[] = {
    //              id                                       name               weight max  min1 max1 step  types                                  conflicts
    { Enchanting::Protection,           "Protection",             10, 4,  1, 21, 10, Enchanting::TypeAnyArmor,
      (1u << Enchanting::FireProtection) | (1u << Enchanting::BlastProtection) | (1u << Enchanting::ProjectileProtection) },
    { Enchanting::FireProtection,       "Fire Protection",         5, 4, 10, 18, 10, Enchanting::TypeAnyArmor,
      (1u << Enchanting::Protection) | (1u << Enchanting::BlastProtection) | (1u << Enchanting::ProjectileProtection) },
    { Enchanting::FeatherFalling,       "Feather Falling",        5, 4,  5, 11,  6, (1u << Enchanting::TypeBoots), 0 },
    { Enchanting::BlastProtection,      "Blast Protection",      2, 4,  5, 13,  8, Enchanting::TypeAnyArmor,
      (1u << Enchanting::Protection) | (1u << Enchanting::FireProtection) | (1u << Enchanting::ProjectileProtection) },
    { Enchanting::ProjectileProtection, "Projectile Protection",  5, 4,  3,  9,  6, Enchanting::TypeAnyArmor,
      (1u << Enchanting::Protection) | (1u << Enchanting::FireProtection) | (1u << Enchanting::BlastProtection) },
    { Enchanting::Respiration,          "Respiration",           2, 3, 10, 40, 10, (1u << Enchanting::TypeHelmet), 0 },
    { Enchanting::AquaAffinity,         "Aqua Affinity",          2, 1,  1, 41,  0, (1u << Enchanting::TypeHelmet), 0 },
    { Enchanting::Thorns,               "Thorns",                 1, 3, 10, 60, 20, Enchanting::TypeAnyArmor, 0 },
    { Enchanting::Sharpness,            "Sharpness",             10, 5,  1, 21, 10, Enchanting::TypeAnyWeapon,
      (1u << Enchanting::Smite) | (1u << Enchanting::BaneOfArthropods) },
    { Enchanting::Smite,                "Smite",                  5, 5,  5, 25,  8, Enchanting::TypeAnyWeapon,
      (1u << Enchanting::Sharpness) | (1u << Enchanting::BaneOfArthropods) },
    { Enchanting::BaneOfArthropods,     "Bane of Arthropods",     5, 5,  5, 25,  8, Enchanting::TypeAnyWeapon,
      (1u << Enchanting::Sharpness) | (1u << Enchanting::Smite) },
    { Enchanting::Knockback,            "Knockback",              5, 2,  5, 25, 20, Enchanting::TypeAnyWeapon, 0 },
    { Enchanting::FireAspect,           "Fire Aspect",            2, 2, 10, 60, 20, Enchanting::TypeAnyWeapon, 0 },
    { Enchanting::Looting,              "Looting",                2, 3, 15, 65,  9, (1u << Enchanting::TypeSword), 0 },
    { Enchanting::Efficiency,           "Efficiency",            10, 5,  1, 51, 10, Enchanting::TypeAnyMiningWithHoe, 0 },
    { Enchanting::SilkTouch,            "Silk Touch",             1, 1, 15, 65,  0, Enchanting::TypeAnyMiningWithHoe,
      (1u << Enchanting::Fortune) },
    { Enchanting::Unbreaking,           "Unbreaking",             5, 3,  5, 55, 10, Enchanting::TypeEverything, 0 },
    { Enchanting::Fortune,              "Fortune",                2, 3, 15, 65,  9, Enchanting::TypeAnyMiningWithHoe,
      (1u << Enchanting::SilkTouch) },
    { Enchanting::Power,                "Power",                 10, 5,  1, 16, 10, (1u << Enchanting::TypeBow), 0 },
    { Enchanting::Punch,                "Punch",                  2, 2, 12, 37, 20, (1u << Enchanting::TypeBow), 0 },
    { Enchanting::Flame,                "Flame",                  2, 1, 20, 50,  0, (1u << Enchanting::TypeBow), 0 },
    { Enchanting::Infinity,             "Infinity",               1, 1, 20, 50,  0, (1u << Enchanting::TypeBow), 0 },
};

static_assert(sizeof(defs) / sizeof(defs[0]) == Enchanting::IdCount,
              "the registry must have one row per enchantment, in Id order");

// ---------------------------------------------------------------- the registry

const Enchanting::Def &Enchanting::definition(Id id)
{
    if(id >= IdCount)
        return defs[0];
    return defs[id];
}

const char *Enchanting::name(Id id)
{
    return definition(id).name;
}

namespace
{
    /** Folds away case, spaces and underscores, the way the command parser does. */
    bool nameMatches(const char *a, const char *b)
    {
        while(*a && *b)
        {
            if(*a == ' ' || *a == '_') { ++a; continue; }
            if(*b == ' ' || *b == '_') { ++b; continue; }
            const char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
            const char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
            if(ca != cb)
                return false;
            ++a;
            ++b;
        }
        while(*a == ' ' || *a == '_')
            ++a;
        while(*b == ' ' || *b == '_')
            ++b;
        return *a == '\0' && *b == '\0';
    }
}

Enchanting::Id Enchanting::byName(const char *name)
{
    if(name == nullptr)
        return IdCount;

    for(int i = 0; i < IdCount; ++i)
        if(nameMatches(defs[i].name, name))
            return static_cast<Id>(i);

    return IdCount;
}

int Enchanting::weight(Id id) { return definition(id).weight; }
int Enchanting::maxLevel(Id id) { return definition(id).max_level; }

int Enchanting::minEnchantability(Id id, int level)
{
    const Def &d = definition(id);
    if(level < 1)
        level = 1;
    return d.min1 + d.step * (level - 1);
}

int Enchanting::maxEnchantability(Id id, int level)
{
    const Def &d = definition(id);
    if(level < 1)
        level = 1;
    return d.max1 + d.step * (level - 1);
}

bool Enchanting::appliesTo(Id id, ItemType type)
{
    if(type == TypeNone)
        return false;
    if(type == TypeBook)
        return true; // a book can be enchanted with anything
    return (definition(id).types & (1u << type)) != 0;
}

bool Enchanting::conflicts(Id a, Id b)
{
    if(a == b)
        return false;
    return (definition(a).conflicts & (1u << b)) != 0;
}

int Enchanting::anvilRarityPrice(Id id)
{
    // Vanilla prices an enchantment on the anvil by its rarity multiplier, and
    // rarity is the weight: 10 common, 5 uncommon, 2 rare, 1 very rare.
    const int w = weight(id);
    if(w >= 10)
        return 1;
    if(w >= 5)
        return 2;
    if(w >= 2)
        return 4;
    return 8;
}

// ------------------------------------------------------------------ the item

uint8_t Enchanting::pack(Id id, int level)
{
    if(level < 1)
        level = 1;
    if(level > 7)
        level = 7;
    return static_cast<uint8_t>((static_cast<uint8_t>(id) & 0x1F) | (level << 5));
}

Enchanting::Id Enchanting::unpackId(uint8_t entry)
{
    const Id id = static_cast<Id>(entry & 0x1F);
    return id < IdCount ? id : IdCount;
}

int Enchanting::unpackLevel(uint8_t entry)
{
    return (entry >> 5) & 0x07;
}

int Enchanting::Set::levelOf(Id id) const
{
    for(int i = 0; i < count && i < MaxPerItem; ++i)
        if(unpackId(entries[i]) == id)
            return unpackLevel(entries[i]);
    return 0;
}

bool Enchanting::Set::add(Id id, int level)
{
    if(id >= IdCount || level < 1)
        return false;

    // An item can never hold two enchantments that exclude each other, however
    // they arrive: the table skips them and the anvil will not create them.
    for(int i = 0; i < count && i < MaxPerItem; ++i)
    {
        const Id present = unpackId(entries[i]);
        if(present == id)
        {
            if(unpackLevel(entries[i]) >= level)
                return false; // already at least this deep
            entries[i] = pack(id, level);
            return true;
        }
        if(conflicts(present, id))
            return false;
    }

    if(count >= MaxPerItem)
        return false;

    entries[count++] = pack(id, level);
    return true;
}

Enchanting::ItemType Enchanting::typeOfItem(uint8_t item_id)
{
    if(item_id == Bow || item_id == BowPulling2 || item_id == BowPulling3)
        return TypeBow;
    if(item_id == Book)
        return TypeBook;

    // Armour: four rows, one per slot, with the five materials at the start of
    // each row. The rest of every row is other items entirely (a stick, a
    // compass, paper), so the material offset has to be inside the row's five --
    // a row being 16 ids wide is an artefact of the atlas, not of the game.
    const int row_offset = item_id % ArmorRow;
    const bool in_material_range = row_offset <= MaterialGold;
    if(in_material_range && item_id < HelmetBase + ArmorRow)
        return TypeHelmet;
    if(in_material_range && item_id >= ChestBase && item_id < ChestBase + ArmorRow)
        return TypeChestplate;
    if(in_material_range && item_id >= LegsBase && item_id < LegsBase + ArmorRow)
        return TypeLeggings;
    if(in_material_range && item_id >= BootsBase && item_id < BootsBase + ArmorRow)
        return TypeBoots;

    // Tools: five rows of five, one tool per row, same story.
    if(item_id >= SwordBase && item_id - SwordBase <= MaterialGold)
        return TypeSword;
    if(item_id >= ShovelBase && item_id - ShovelBase <= MaterialGold)
        return TypeShovel;
    if(item_id >= PickaxeBase && item_id - PickaxeBase <= MaterialGold)
        return TypePickaxe;
    if(item_id >= AxeBase && item_id - AxeBase <= MaterialGold)
        return TypeAxe;
    if(item_id >= HoeBase && item_id - HoeBase <= MaterialGold)
        return TypeHoe;

    return TypeNone;
}

int Enchanting::enchantability(uint8_t item_id)
{
    const ItemType type = typeOfItem(item_id);
    if(type == TypeNone)
        return 0;
    if(type == TypeBow || type == TypeBook)
        return 1; // vanilla: a bow and a book are worth almost nothing to a table

    if(type == TypeHelmet || type == TypeChestplate || type == TypeLeggings || type == TypeBoots)
    {
        const int material = item_id % ArmorRow;
        return material <= MaterialGold ? ArmorEnchantability[material] : 0;
    }

    int material = 0;
    if(item_id >= SwordBase && item_id < SwordBase + ToolRow)
        material = item_id - SwordBase;
    else if(item_id >= ShovelBase && item_id < ShovelBase + ToolRow)
        material = item_id - ShovelBase;
    else if(item_id >= PickaxeBase && item_id < PickaxeBase + ToolRow)
        material = item_id - PickaxeBase;
    else if(item_id >= AxeBase && item_id < AxeBase + ToolRow)
        material = item_id - AxeBase;
    else if(item_id >= HoeBase && item_id < HoeBase + ToolRow)
        material = item_id - HoeBase;
    else
        return 0;

    return material <= MaterialGold ? ToolEnchantability[material] : 0;
}

// ----------------------------------------------------------------- the table

uint32_t Enchanting::Rng::next()
{
    // xorshift32: small, fast, and good enough for a loot table. Not Java's
    // generator, so the numbers are this game's own (enchanting.h says so).
    uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

int Enchanting::Rng::nextInt(int bound)
{
    if(bound <= 0)
        return 0;
    return static_cast<int>(next() % static_cast<uint32_t>(bound));
}

float Enchanting::Rng::nextFloat()
{
    return static_cast<float>(next() >> 8) / 16777216.0f; // 24 bits, in [0,1)
}

int Enchanting::baseOfferLevel(Rng &rng, int power)
{
    if(power < 0)
        power = 0;
    if(power > MaxBookshelfPower)
        power = MaxBookshelfPower;
    // Vanilla: randomInt(1,8) + floor(power/2) + randomInt(0,power).
    return 1 + rng.nextInt(8) + (power / 2) + rng.nextInt(power + 1);
}

int Enchanting::offerLevelForSlot(int base, int slot)
{
    int level = base;
    if(slot == 0)
        level = base / 3;
    else if(slot == 1)
        level = base * 2 / 3 + 1;
    return level < 1 ? 1 : level;
}

namespace
{
    /**
     * The spell level of an offer: the offer's own level, plus the item's
     * enchantability in two rolls, then the triangular +/-15% wobble. This is what
     * makes two identical diamond pickaxes offer different spells at the same
     * table.
     */
    int modifiedLevel(Enchanting::Rng &rng, int offer_level, int enchantability)
    {
        int level = offer_level + 1 + rng.nextInt(enchantability / 4 + 1) + rng.nextInt(enchantability / 4 + 1);
        const float wobble = (rng.nextFloat() + rng.nextFloat() - 1.0f) * 0.15f;
        level = static_cast<int>(static_cast<float>(level) * (1.0f + wobble) + 0.5f);
        return level < 1 ? 1 : level;
    }

    /** How many candidates the spell level pays for, and their weights. */
    int candidateTotal(int level, Enchanting::ItemType type, const bool banned[Enchanting::IdCount],
                       bool allowed[Enchanting::IdCount])
    {
        int total = 0;
        for(int i = 0; i < Enchanting::IdCount; ++i)
        {
            const Enchanting::Id id = static_cast<Enchanting::Id>(i);
            allowed[i] = false;
            if(banned[i] || !Enchanting::appliesTo(id, type))
                continue;

            // The deepest level this spell level reaches. An enchantment whose
            // cheapest level is too expensive is not a candidate at all.
            for(int lvl = Enchanting::maxLevel(id); lvl >= 1; --lvl)
                if(level >= Enchanting::minEnchantability(id, lvl))
                {
                    allowed[i] = true;
                    total += Enchanting::weight(id);
                    break;
                }
        }
        return total;
    }

    /** The spell level of the deepest level of `id` this level reaches, or 0. */
    int candidateLevel(Enchanting::Id id, int level)
    {
        for(int lvl = Enchanting::maxLevel(id); lvl >= 1; --lvl)
            if(level >= Enchanting::minEnchantability(id, lvl))
                return lvl;
        return 0;
    }

    /** Picks one candidate by weight and adds it, retiring what it excludes. */
    bool pickWeighted(Enchanting::Rng &rng, int level, Enchanting::ItemType type,
                      bool banned[Enchanting::IdCount], Enchanting::Set &set)
    {
        bool allowed[Enchanting::IdCount];
        const int total = candidateTotal(level, type, banned, allowed);
        if(total <= 0)
            return false;

        int roll = rng.nextInt(total);
        for(int i = 0; i < Enchanting::IdCount; ++i)
        {
            if(!allowed[i])
                continue;
            const Enchanting::Id id = static_cast<Enchanting::Id>(i);
            const int w = Enchanting::weight(id);
            if(roll < w)
            {
                const int lvl = candidateLevel(id, level);
                if(lvl > 0)
                    set.add(id, lvl);
                // What this choice excludes can never be offered alongside it.
                banned[i] = true;
                for(int other = 0; other < Enchanting::IdCount; ++other)
                    if(Enchanting::conflicts(id, static_cast<Enchanting::Id>(other)))
                        banned[other] = true;
                return true;
            }
            roll -= w;
        }
        return false;
    }
}

void Enchanting::rollOffers(uint32_t seed, int power, int enchantability, ItemType type,
                            Offer offers[OfferCount])
{
    Rng rng;
    rng.seed(seed);

    // One roll for the whole table, not one per offer: a single bookshelf reading
    // is scaled into a third, two thirds and all of it, which is what makes the
    // three offers climb instead of being three unrelated dice. The level 30 wall
    // everyone builds for is exactly this number at fifteen bookshelves --
    // 8 + 15/2 + 15 = 30 -- and cannot be exceeded however the roll falls.
    const int base = baseOfferLevel(rng, power);

    for(int slot = 0; slot < OfferCount; ++slot)
    {
        offers[slot].set.clear();
        offers[slot].available = false;

        const int slot_level = offerLevelForSlot(base, slot);
        offers[slot].level = slot_level;

        if(type == TypeNone || enchantability <= 0)
            continue; // nothing to enchant: the table shows three empty offers

        int level = modifiedLevel(rng, slot_level, enchantability);
        bool banned[IdCount] = {};

        if(!pickWeighted(rng, level, type, banned, offers[slot].set))
            continue;

        // More enchantments while the roll keeps passing: vanilla halves the
        // spell level each time, which is why a level 30 offer is usually two or
        // three enchantments rather than a shopping list.
        while(rng.nextInt(50) <= level)
        {
            level /= 2;
            if(level < 1)
                break;
            if(offers[slot].set.count >= MaxPerItem)
                break;
            if(!pickWeighted(rng, level, type, banned, offers[slot].set))
                break;
        }

        offers[slot].available = !offers[slot].set.empty();
    }
}

void Enchanting::applyOffer(const Offer &offer, Set &set)
{
    for(int i = 0; i < offer.set.count && i < MaxPerItem; ++i)
    {
        const Id id = unpackId(offer.set.entries[i]);
        const int level = unpackLevel(offer.set.entries[i]);
        if(id < IdCount && level > 0)
            set.add(id, level);
    }
}

// ----------------------------------------------------------------- the anvil

int Enchanting::anvilPriorPenalty(int uses)
{
    if(uses <= 0)
        return 0;
    if(uses > 5)
        uses = 5; // vanilla caps the penalty at 31 levels
    return (1 << uses) - 1;
}

uint8_t Enchanting::repairMaterialFor(uint8_t item_id)
{
    const ItemType type = typeOfItem(item_id);
    if(type == TypeNone)
        return NoRepairMaterial;
    if(type == TypeBow || type == TypeBook)
        return NoRepairMaterial; // a bow is repaired with string, which this game has not got

    if(type == TypeHelmet || type == TypeChestplate || type == TypeLeggings || type == TypeBoots)
    {
        const int material = item_id % ArmorRow;
        return material <= MaterialGold ? RepairMaterial[material] : NoRepairMaterial;
    }

    int material = -1;
    if(item_id >= SwordBase && item_id < SwordBase + ToolRow)
        material = item_id - SwordBase;
    else if(item_id >= ShovelBase && item_id < ShovelBase + ToolRow)
        material = item_id - ShovelBase;
    else if(item_id >= PickaxeBase && item_id < PickaxeBase + ToolRow)
        material = item_id - PickaxeBase;
    else if(item_id >= AxeBase && item_id < AxeBase + ToolRow)
        material = item_id - AxeBase;
    else if(item_id >= HoeBase && item_id < HoeBase + ToolRow)
        material = item_id - HoeBase;

    if(material < 0 || material > MaterialGold)
        return NoRepairMaterial;
    return RepairMaterial[material];
}

int Enchanting::repairUnitsNeeded(int damage, int max_damage)
{
    if(max_damage <= 0 || damage <= 0)
        return 0;
    const int per_unit = (max_damage * RepairPercentPerUnit) / 100;
    if(per_unit <= 0)
        return 0;
    return (damage + per_unit - 1) / per_unit;
}

int Enchanting::repairedDamage(int damage, int max_damage, int units)
{
    if(units <= 0 || damage <= 0 || max_damage <= 0)
        return damage;
    const int per_unit = (max_damage * RepairPercentPerUnit) / 100;
    const int restored = per_unit * units;
    return damage > restored ? damage - restored : 0;
}

Enchanting::AnvilResult Enchanting::combine(const AnvilInput &left, const AnvilInput &right, bool renamed)
{
    AnvilResult result;

    if(!left.present)
        return result; // an empty left slot makes no job at all

    if(!right.present)
    {
        // Renaming one item on its own: one level, and vanilla's only anvil job
        // that costs nothing else.
        result.set = left.set;
        result.damage = left.damage;
        result.prior_work = left.prior_work + 1;
        result.cost = renamed ? 1 : 0;
        result.possible = renamed;
        return result;
    }

    // Two items. The caller has already made sure they are the same kind of
    // thing, which is the rule vanilla enforces with "these are not the same".
    result.combined = true;
    result.set = left.set;

    const int left_left = left.max_damage - left.damage;
    const int right_left = right.max_damage - right.damage;
    const int bonus = (left.max_damage * CombineBonusPercent) / 100;
    int total = left_left + right_left + bonus;
    if(total > left.max_damage)
        total = left.max_damage;
    result.damage = left.max_damage - total;
    result.prior_work = (left.prior_work > right.prior_work ? left.prior_work : right.prior_work) + 1;

    int cost = anvilPriorPenalty(left.prior_work) + anvilPriorPenalty(right.prior_work) + 2;

    // The right item's enchantments: the same spell on both deepens by one level
    // (never past its own maximum), anything else merges in.
    for(int i = 0; i < right.set.count && i < MaxPerItem; ++i)
    {
        const Id id = unpackId(right.set.entries[i]);
        const int level = unpackLevel(right.set.entries[i]);
        if(id >= IdCount || level <= 0)
            continue;

        const int own = result.set.levelOf(id);
        int merged = level;
        if(own > 0)
            merged = own == level ? (own + 1 < maxLevel(id) ? own + 1 : maxLevel(id)) : (own > level ? own : level);

        if(result.set.add(id, merged))
            cost += anvilRarityPrice(id) * merged;
    }

    if(renamed)
        ++cost;

    result.cost = cost;
    result.too_expensive = cost >= TooExpensive;
    result.possible = true;
    return result;
}

// ------------------------------------------------------------------ effects

int Enchanting::efficiencyPercent(int level)
{
    if(level < 1)
        return 100;
    return (level * level + 2) * 100;
}

int Enchanting::unbreakingKeepPercent(int level)
{
    if(level < 1)
        return 100;
    return 100 / (level + 1);
}

int Enchanting::sharpnessHalfHearts(int level)
{
    if(level < 1)
        return 0;
    return (level + 2) / 2; // vanilla: 0.5 + 0.5 per level, rounded to whole health
}

int Enchanting::smiteHalfHearts(int level)
{
    if(level < 1)
        return 0;
    return (5 * level + 1) / 2; // vanilla: 2.5 per level
}

int Enchanting::baneHalfHearts(int level)
{
    return smiteHalfHearts(level);
}

int Enchanting::meleeDamage(int base_damage, const Set &weapon, TargetKind target)
{
    int damage = base_damage + sharpnessHalfHearts(weapon.levelOf(Sharpness));

    if(target == TargetKind::Undead)
        damage += smiteHalfHearts(weapon.levelOf(Smite));
    else if(target == TargetKind::Arthropod)
        damage += baneHalfHearts(weapon.levelOf(BaneOfArthropods));

    // Vanilla never lets a hit become a heal.
    return damage < 1 ? 1 : damage;
}

Enchanting::Id Enchanting::protectionEnchantment(ProtectionKind kind)
{
    switch(kind)
    {
    case ProtectionKind::FromFire: return FireProtection;
    case ProtectionKind::FromFall: return FeatherFalling;
    case ProtectionKind::FromBlast: return BlastProtection;
    case ProtectionKind::FromProjectile: return ProjectileProtection;
    case ProtectionKind::FromAll: break;
    }
    return IdCount;
}

int Enchanting::protectionPointsFor(const Set &set, ProtectionKind kind)
{
    // Protection guards against everything, so it is counted for every kind; the
    // named enchantment is added on top and only for its own kind.
    int points = protectionPoints(Protection, set.levelOf(Protection));

    const Id specific = protectionEnchantment(kind);
    if(specific != IdCount)
        points += protectionPoints(specific, set.levelOf(specific));

    return points;
}

int Enchanting::protectionPoints(Id id, int level)
{
    if(level < 1)
        return 0;
    switch(id)
    {
    case FireProtection:
    case BlastProtection:
    case ProjectileProtection:
        return 2 * level; // vanilla: the specific protections are worth double
    case FeatherFalling:
        return 3 * level;
    case Protection:
        return level;
    default:
        return 0;
    }
}

int Enchanting::thornsHalfHearts(int level)
{
    return level < 1 ? 0 : (level > 4 ? 4 : level);
}

int Enchanting::fireAspectTicks(int level)
{
    return level < 1 ? 0 : 80 * level; // four seconds per level
}

int Enchanting::knockbackSteps(int level)
{
    return level < 1 ? 0 : level;
}

int Enchanting::lootingExtraDrops(int level)
{
    return level < 1 ? 0 : level;
}

int Enchanting::fortuneExtraDropPercent(int level)
{
    switch(level)
    {
    case 1: return 33;
    case 2: return 75;
    case 3: return 120;
    default: return level > 3 ? 120 : 0;
    }
}

bool Enchanting::silkTouch(int level) { return level >= 1; }

int Enchanting::featherFallingPercentReduction(int level)
{
    if(level < 1)
        return 0;
    const int percent = 20 * level;
    return percent > 80 ? 80 : percent; // vanilla caps it at four levels' worth
}

int Enchanting::respirationSeconds(int level)
{
    return level < 1 ? 0 : 15 * level;
}

int Enchanting::powerPercent(int level)
{
    if(level < 1)
        return 100;
    return 100 + 25 * level; // vanilla: a quarter more damage per level
}

int Enchanting::punchSteps(int level)
{
    return level < 1 ? 0 : level;
}

int Enchanting::flameTicks(int level)
{
    return level < 1 ? 0 : 80 * level;
}

bool Enchanting::infinity(int level) { return level >= 1; }

bool Enchanting::aquaAffinity(int level) { return level >= 1; }
