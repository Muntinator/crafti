#ifndef ENCHANTING_H
#define ENCHANTING_H

#include <stdint.h>

/**
 * Enchantments: the registry, the table's offer algorithm, the anvil's combining
 * rules and the numbers the effects are worth.
 *
 * Like survival.h, itemrules.h and weather.h, this is pure: no engine header, no
 * memory outside its arguments, and it is unit tested whole on the host
 * (tests/enchanting_test.cc). The caller owns the item, the world and the screen.
 *
 * **What it follows, and what it approximates.** The goal is Minecraft's own
 * behaviour, so the algorithm is the vanilla one rather than a lookalike:
 *
 *  - the three offers are `base = randomInt(1,8) + bookshelfPower/2 + randomInt(0,bookshelfPower)`
 *    scaled by which offer it is (a third, two thirds, all of it), with the
 *    bookshelf power capped at 15 bookshelves,
 *  - the spell level is that offer plus the item's own enchantability roll and a
 *    triangular ±15% wobble, which is what makes two diamond picks differ,
 *  - candidates are the enchantments the item can carry whose *highest reachable
 *    level* the spell level pays for, picked by weight, then repeatedly re-picked
 *    while the level (halved each time) keeps passing a 1-in-50 roll -- so a
 *    level-30 offer is usually two or three enchantments and never a shopping
 *    list,
 *  - enchantments that exclude each other (Sharpness/Smite/Bane, Silk Touch/
 *    Fortune, the five armour protections) cannot be offered together,
 *  - the anvil's cost is the two items' prior work plus the enchantments' rarity
 *    prices, doubling per prior use, with vanilla's "Too Expensive" ceiling of 39
 *    and its combining rules (same enchantment deepens, different ones merge,
 *    durability adds with a 12% bonus).
 *
 * Where the numbers are the wiki's but the version differences are real, the
 * table below documents the era it was taken from. The two deliberate departures
 * are: the random number generator is this game's own (a xorshift, not Java's
 * LCG), so the *distribution* matches vanilla but not the exact sequence for a
 * given seed; and there is no lapis lazuli in this game, so an offer costs
 * levels and the item only, never a gem.
 */
namespace Enchanting
{
    /** Every enchantment this game knows. Stored in an item's byte, so fewer than 32. */
    enum Id : uint8_t
    {
        Protection = 0,
        FireProtection,
        FeatherFalling,
        BlastProtection,
        ProjectileProtection,
        Respiration,
        AquaAffinity,
        Thorns,
        Sharpness,
        Smite,
        BaneOfArthropods,
        Knockback,
        FireAspect,
        Looting,
        Efficiency,
        SilkTouch,
        Unbreaking,
        Fortune,
        Power,
        Punch,
        Flame,
        Infinity,
        IdCount
    };

    /** What an item is, for deciding which enchantments it can carry. */
    enum ItemType : uint8_t
    {
        TypeNone = 0,
        TypeSword,
        TypeAxe,
        TypePickaxe,
        TypeShovel,
        TypeHoe,
        TypeBow,
        TypeHelmet,
        TypeChestplate,
        TypeLeggings,
        TypeBoots,
        TypeBook, // an enchanted book: everything
        TypeCount
    };

    /** Vanilla's bit for "any armour piece", used by the registry. */
    constexpr uint16_t TypeAnyArmor = (1u << TypeHelmet) | (1u << TypeChestplate)
        | (1u << TypeLeggings) | (1u << TypeBoots);
    constexpr uint16_t TypeAnyTool = (1u << TypeAxe) | (1u << TypePickaxe) | (1u << TypeShovel);
    constexpr uint16_t TypeAnyMiningWithHoe = TypeAnyTool | (1u << TypeHoe);
    constexpr uint16_t TypeAnyWeapon = (1u << TypeSword) | (1u << TypeAxe);
    /** Everything a book can be enchanted with. */
    constexpr uint16_t TypeEverything = 0xFFFF;

    /** One enchantment's entry. The four cost fields are the wiki's per-level range. */
    struct Def
    {
        Id id;
        const char *name;
        /** Selection weight: 10 common, 5 uncommon, 2 rare, 1 very rare. */
        uint8_t weight;
        uint8_t max_level;
        /** Minimum and maximum enchantability at level 1, and the per-level step. */
        int min1, max1, step;
        /** Bitmask of ItemType this can be placed on. */
        uint16_t types;
        /** Bitmask of Ids this cannot be combined with (compiled from the table). */
        uint32_t conflicts;
    };

    /** How many enchantments one item can carry, table or anvil. */
    constexpr int MaxPerItem = 4;
    /** Bookshelves past this many add nothing (vanilla's cap). */
    constexpr int MaxBookshelfPower = 15;
    /** Offers the table shows at once. */
    constexpr int OfferCount = 3;
    /** Vanilla refuses a survival anvil job past this many levels. */
    constexpr int TooExpensive = 40;

    const Def &definition(Id id);
    const char *name(Id id);
    /** The enchantment with this name, or IdCount. */
    Id byName(const char *name);
    int weight(Id id);
    int maxLevel(Id id);
    int minEnchantability(Id id, int level);
    int maxEnchantability(Id id, int level);
    bool appliesTo(Id id, ItemType type);
    bool conflicts(Id a, Id b);

    /** Vanilla's rarity price on the anvil: common 1, uncommon 2, rare 4, very rare 8. */
    int anvilRarityPrice(Id id);

    // ------------------------------------------------------------ the item
    /**
     * The enchantments on one item, packed one byte each: the id in the low five
     * bits, the level in the top three. That is the whole of an item's
     * enchantment state, which is what keeps it out of the block and in the slot.
     */
    struct Set
    {
        uint8_t entries[MaxPerItem] = {};
        uint8_t count = 0;

        /** The level of an enchantment on this item, or 0 when it has none. */
        int levelOf(Id id) const;
        bool has(Id id) const { return levelOf(id) > 0; }
        /** Adds an enchantment, keeping the deepest level when it is already there. */
        bool add(Id id, int level);
        void clear() { count = 0; }
        bool empty() const { return count == 0; }
    };

    uint8_t pack(Id id, int level);
    Id unpackId(uint8_t entry);
    int unpackLevel(uint8_t entry);

    /** The item type of an item id (the data byte of a BLOCK_ITEM stack). */
    ItemType typeOfItem(uint8_t item_id);
    /** The enchantability of an item: how much a table's offers gain from it. */
    int enchantability(uint8_t item_id);

    // ---------------------------------------------------------- the table
    /** A deterministic generator, so a host test can walk millions of rolls. */
    struct Rng
    {
        uint32_t state = 0x12345678u;
        void seed(uint32_t value) { state = value ? value : 0x9E3779B9u; }
        uint32_t next();
        /** Vanilla's nextInt(bound): 0..bound-1, and 0 when bound <= 0. */
        int nextInt(int bound);
        /** A float in [0,1), for the triangular wobble. */
        float nextFloat();
    };

    /** Bookshelf power: one per bookshelf, capped like vanilla. */
    constexpr int bookshelfPower(int bookshelves)
    {
        return bookshelves < 0 ? 0 : (bookshelves > MaxBookshelfPower ? MaxBookshelfPower : bookshelves);
    }

    /** The raw offer level, before the item's enchantability: vanilla's base formula. */
    int baseOfferLevel(Rng &rng, int power);
    /** Which of the three offers, scaled like vanilla (a third, two thirds, all). */
    int offerLevelForSlot(int base, int slot);

    /** One offer as the table shows it: the levels it costs and what it gives. */
    struct Offer
    {
        /** Levels the player must have (and spends). */
        int level = 0;
        Set set;
        /** False when the offer is below the player's level or has nothing to give. */
        bool available = false;
    };

    /**
     * Rolls the three offers for an item. `seed` names the roll, so the same
     * item at the same table offers the same three spells until the item is
     * taken out and put back (the caller seeds it from the world and the table).
     */
    void rollOffers(uint32_t seed, int power, int enchantability, ItemType type,
                    Offer offers[OfferCount]);

    /** Applies an offer to an item's set: adds what it rolled, skipping conflicts. */
    void applyOffer(const Offer &offer, Set &set);

    /**
     * Lapis lazuli the offer costs: one, two and three, as vanilla charges for the
     * three offers. This game does have a lapis item (item id 142), so the cost is
     * real rather than skipped.
     */
    constexpr int lapisForSlot(int slot) { return slot + 1; }

    // ---------------------------------------------------------- the anvil
    /** One item on the anvil: its wear, its enchantments and its prior work. */
    struct AnvilInput
    {
        bool present = false;
        Set set;
        int damage = 0;
        int max_damage = 0;
        /** How many times this item has already been through an anvil (vanilla's uses). */
        int prior_work = 0;
    };

    struct AnvilResult
    {
        bool possible = false;
        /** Levels the job costs (and the player must have). */
        int cost = 0;
        /** True when a survival player cannot afford it at any level. */
        bool too_expensive = false;
        Set set;
        int damage = 0;
        int prior_work = 0;
        /** True when the job combines two items rather than renaming or repairing one. */
        bool combined = false;
    };

    /**
     * Combines the left and right anvil slots.
     *
     * `renamed` is true when the player typed a new name (one level, and the
     * reason a nameless anvil job can still be worth doing). Vanilla's rules are
     * kept: two of the same item add their durability plus 12% of the item's
     * maximum, the same enchantment on both deepens by one level up to its own
     * maximum, different enchantments merge, everything costs its rarity price
     * per level, and each input's prior work doubles what that input contributes.
     */
    AnvilResult combine(const AnvilInput &left, const AnvilInput &right, bool renamed);

    /**
     * What one prior anvil job costs the next: vanilla doubles it, one level for
     * the first use, three for the second, seven for the third, and so on.
     */
    int anvilPriorPenalty(int uses);
    /** The material that repairs an item, as an item id, or NoRepairMaterial. */
    uint8_t repairMaterialFor(uint8_t item_id);
    constexpr uint8_t NoRepairMaterial = 0xFF;
    /** How much of the item's maximum durability one repair unit restores, in percent. */
    constexpr int RepairPercentPerUnit = 25;
    /** Levels each repair unit costs. */
    constexpr int RepairCostPerUnit = 1;
    /** The share of the maximum durability a combination job adds, in percent. */
    constexpr int CombineBonusPercent = 12;
    /** The wear left after feeding `units` repair materials to a damaged item. */
    int repairedDamage(int damage, int max_damage, int units);
    /** How many repair units a damaged item would take to be whole again. */
    int repairUnitsNeeded(int damage, int max_damage);

    // ------------------------------------------------------------ effects
    /**
     * What is being hit. Vanilla's two "one kind" damage enchantments name a kind
     * rather than a mob, so the mobs carry theirs: Smite is worth its bonus
     * against the undead and Bane of Arthropods against the bugs. This build's
     * mobs are all `Normal` (there are no zombies or spiders yet), so the two are
     * carried by the rules and worth nothing in play until one exists.
     */
    enum class TargetKind : uint8_t
    {
        Normal = 0,
        Undead,
        Arthropod
    };

    /**
     * The damage of one melee hit, in half-hearts: the weapon's own damage plus
     * what the weapon's own enchantments add against this kind of target.
     * Sharpness reaches everything, Smite and Bane only their own kind, and no
     * combination ever takes a hit below one point.
     */
    int meleeDamage(int base_damage, const Set &weapon, TargetKind target);

    /**
     * The five kinds of protection vanilla has, which decide which of the armour
     * enchantments applies to a hit: everything, fire, falling, explosions and
     * projectiles.
     */
    enum class ProtectionKind : uint8_t
    {
        FromAll = 0,
        FromFire,
        FromFall,
        FromBlast,
        FromProjectile
    };

    /** The enchantment that guards against one kind, or IdCount when none does. */
    Id protectionEnchantment(ProtectionKind kind);

    /**
     * Protection points one set of enchantments puts against one kind of damage,
     * counted the way vanilla counts them: the general Protection guards against
     * every kind, and the enchantment named for the kind is added on top of it.
     *
     * The caller sums this over the four worn pieces and runs it through the same
     * reduction as the armour's own points -- but as a *second* stage, because
     * vanilla caps each at 80% separately, and adding them into one total would
     * leave Protection worth nothing under a full diamond suit.
     */
    int protectionPointsFor(const Set &set, ProtectionKind kind);

    /**
     * The tool's mining speed as a percentage of a bare hand, which vanilla treats
     * as speed 1. Efficiency adds `level^2 + 1` to that speed, so level 5 is 27x a
     * bare hand -- the reason an Efficiency pickaxe goes through stone.
     */
    int efficiencyPercent(int level);
    /** Out of 100, how often an Unbreaking item survives a use. */
    int unbreakingKeepPercent(int level);
    /** Extra damage in half-hearts: Sharpness reaches every mob, Smite and Bane one kind. */
    int sharpnessHalfHearts(int level);
    int smiteHalfHearts(int level);
    int baneHalfHearts(int level);
    /** Armour points a protection adds against one kind of damage. */
    int protectionPoints(Id id, int level);
    /** Damage Thorns deals back to whatever hit the wearer, in half-hearts. */
    int thornsHalfHearts(int level);
    /** Ticks of burning Fire Aspect adds to a hit, or to a struck mob. */
    int fireAspectTicks(int level);
    /** How far Knockback throws a hit mob, in the engine's own knockback steps. */
    int knockbackSteps(int level);
    /** Extra rolls Looting adds to a mob's drop table. */
    int lootingExtraDrops(int level);
    /** Fortune's extra drops, as a percent chance of one more per level. */
    int fortuneExtraDropPercent(int level);
    /** Silk Touch: blocks drop themselves instead of their usual drop. */
    bool silkTouch(int level);
    /** Fall damage Feather Falling takes off, as a percentage. */
    int featherFallingPercentReduction(int level);
    /** Seconds of breath Respiration adds per level. */
    int respirationSeconds(int level);
    /** Extra bow damage as a percentage of an unenchanted arrow (100 = none). */
    int powerPercent(int level);
    int punchSteps(int level);
    /** Ticks a Flame bow sets its target alight for. */
    int flameTicks(int level);
    /** True when Infinity means one arrow is enough (the bow still needs one). */
    bool infinity(int level);
    /** Respiration's and Aqua Affinity's non-numeric halves. */
    bool aquaAffinity(int level);
}

#endif // ENCHANTING_H
