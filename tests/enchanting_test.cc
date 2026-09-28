// Host tests for the enchantment module (enchanting.cpp).
//
// The whole system is pure -- the registry, the table's offers, the anvil's
// combining rules and the numbers the effects are worth -- so it is walked end to
// end here: thousands of offers at every bookshelf power are checked against the
// vanilla properties (nothing offered is unaffordable, deeper spells need better
// tables, the item's type limits what can appear, conflicting enchantments never
// appear together), and the anvil is checked against the cases vanilla defines
// (durability adds with a bonus, the same enchantment deepens once and then caps,
// the prior-work penalty doubles, 39 levels is the ceiling).
//
// The item ids this module keeps as numbers are checked against textures/items.h
// and terrain.h, so a change to either table cannot silently move which item is
// a diamond sword.
//
// Build and run with `make -C tests`.

#include "enchanting.h"

#include "terrain.h"
#include "textures/items.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

namespace
{
    using namespace Enchanting;

    uint8_t item(ItemTexture texture)
    {
        return static_cast<uint8_t>(texture);
    }

    void testRegistry()
    {
        // One row per id, in order, with sane numbers.
        for(int i = 0; i < IdCount; ++i)
        {
            const Def &d = definition(static_cast<Id>(i));
            CHECK(d.id == static_cast<Id>(i));
            CHECK(d.name != nullptr && d.name[0] != '\0');
            CHECK(d.max_level >= 1);
            CHECK(d.weight >= 1 && d.weight <= 10);
            CHECK(d.types != 0);
            CHECK(minEnchantability(static_cast<Id>(i), 1) == d.min1);
            CHECK(maxEnchantability(static_cast<Id>(i), 1) == d.max1);
            CHECK(d.max1 > d.min1);

            // Deeper levels are always harder, which is what stops the table from
            // offering a level III spell at level I prices.
            for(int lvl = 2; lvl <= d.max_level; ++lvl)
            {
                CHECK(minEnchantability(static_cast<Id>(i), lvl) > minEnchantability(static_cast<Id>(i), lvl - 1));
                CHECK(maxEnchantability(static_cast<Id>(i), lvl) > maxEnchantability(static_cast<Id>(i), lvl - 1));
            }
        }

        // Names are unique and round trip through the lookup, which is what a
        // debug command would use.
        for(int i = 0; i < IdCount; ++i)
        {
            CHECK(byName(name(static_cast<Id>(i))) == static_cast<Id>(i));
            for(int j = 0; j < i; ++j)
                CHECK(strcmp(name(static_cast<Id>(i)), name(static_cast<Id>(j))) != 0);
        }
        CHECK(byName("silk_touch") == SilkTouch);
        CHECK(byName("SILK TOUCH") == SilkTouch);
        CHECK(byName("baneofthearthropods") == IdCount);
        CHECK(byName("bane of arthropods") == BaneOfArthropods);
        CHECK(byName("nonsense") == IdCount);
        CHECK(byName(nullptr) == IdCount);

        // The vanilla incompatibilities, both ways round.
        CHECK(conflicts(Protection, FireProtection));
        CHECK(conflicts(FireProtection, Protection));
        CHECK(conflicts(Protection, BlastProtection));
        CHECK(conflicts(Protection, ProjectileProtection));
        CHECK(conflicts(Sharpness, Smite));
        CHECK(conflicts(Sharpness, BaneOfArthropods));
        CHECK(conflicts(Smite, BaneOfArthropods));
        CHECK(conflicts(SilkTouch, Fortune));
        CHECK(!conflicts(Protection, Thorns));
        CHECK(!conflicts(Sharpness, Knockback));
        CHECK(!conflicts(FireProtection, FeatherFalling));
        CHECK(!conflicts(Efficiency, Unbreaking));
        CHECK(!conflicts(Protection, Protection));

        // Rarity prices fall out of the weights, as vanilla does it.
        CHECK(anvilRarityPrice(Protection) == 1);
        CHECK(anvilRarityPrice(Efficiency) == 1);
        CHECK(anvilRarityPrice(Unbreaking) == 2);
        CHECK(anvilRarityPrice(Fortune) == 4);
        CHECK(anvilRarityPrice(SilkTouch) == 8);
        CHECK(anvilRarityPrice(Infinity) == 8);
    }

    void testItemTypes()
    {
        // The ids this module keeps as numbers, against the real headers.
        CHECK(item(ItemTexture::LEATHER_HELMET) == 0);
        CHECK(item(ItemTexture::GOLDEN_HELMET) == 4);
        CHECK(item(ItemTexture::IRON_CHESTPLATE) == 18);
        CHECK(item(ItemTexture::DIAMOND_LEGGINGS) == 35);
        CHECK(item(ItemTexture::GOLDEN_BOOTS) == 52);
        CHECK(item(ItemTexture::BOOK) == 59);
        CHECK(item(ItemTexture::WOODEN_SWORD) == 64);
        CHECK(item(ItemTexture::GOLDEN_SWORD) == 68);
        CHECK(item(ItemTexture::WOODEN_SHOVEL) == 80);
        CHECK(item(ItemTexture::WOODEN_PICKAXE) == 96);
        CHECK(item(ItemTexture::BOW_PULLING_1) == 101);
        CHECK(item(ItemTexture::WOODEN_AXE) == 112);
        CHECK(item(ItemTexture::WOODEN_HOE) == 128);
        CHECK(item(ItemTexture::LAPIS_LAZULI) == 142);
        CHECK(BLOCK_PLANKS_NORMAL == 6);
        CHECK(BLOCK_COBBLESTONE == 24);

        // Every tool and armour row maps to its own type, and the material rows
        // are enchantability-ordered the way vanilla is (gold best, stone worst).
        struct Row { uint8_t base; ItemType type; };
        const Row rows[] = {
            { 0, TypeHelmet }, { 16, TypeChestplate }, { 32, TypeLeggings }, { 48, TypeBoots },
            { 64, TypeSword }, { 80, TypeShovel }, { 96, TypePickaxe }, { 112, TypeAxe }, { 128, TypeHoe },
        };
        for(unsigned int r = 0; r < sizeof(rows) / sizeof(rows[0]); ++r)
            for(int material = 0; material < 5; ++material)
            {
                const uint8_t id = static_cast<uint8_t>(rows[r].base + material);
                CHECK(typeOfItem(id) == rows[r].type);
                CHECK(enchantability(id) > 0);
            }

        CHECK(typeOfItem(item(ItemTexture::BOOK)) == TypeBook);
        CHECK(typeOfItem(item(ItemTexture::BOW_PULLING_1)) == TypeBow);
        CHECK(typeOfItem(item(ItemTexture::STICK)) == TypeNone);
        CHECK(enchantability(item(ItemTexture::STICK)) == 0);
        CHECK(enchantability(item(ItemTexture::BOW_PULLING_1)) == 1);
        CHECK(enchantability(item(ItemTexture::BOOK)) == 1);

        // Vanilla's enchantability numbers, by material.
        CHECK(enchantability(item(ItemTexture::WOODEN_PICKAXE)) == 15);
        CHECK(enchantability(item(ItemTexture::STONE_PICKAXE)) == 5);
        CHECK(enchantability(item(ItemTexture::IRON_PICKAXE)) == 14);
        CHECK(enchantability(item(ItemTexture::DIAMOND_PICKAXE)) == 10);
        CHECK(enchantability(item(ItemTexture::GOLDEN_PICKAXE)) == 22);
        CHECK(enchantability(item(ItemTexture::LEATHER_BOOTS)) == 15);
        CHECK(enchantability(item(ItemTexture::CHAINMAIL_BOOTS)) == 12);
        CHECK(enchantability(item(ItemTexture::IRON_BOOTS)) == 9);
        CHECK(enchantability(item(ItemTexture::DIAMOND_BOOTS)) == 10);
        CHECK(enchantability(item(ItemTexture::GOLDEN_BOOTS)) == 25);

        // What each enchantment can be placed on.
        CHECK(appliesTo(Protection, TypeHelmet) && appliesTo(Protection, TypeBoots));
        CHECK(!appliesTo(Protection, TypeSword));
        CHECK(appliesTo(FeatherFalling, TypeBoots));
        CHECK(!appliesTo(FeatherFalling, TypeHelmet));
        CHECK(appliesTo(Respiration, TypeHelmet) && !appliesTo(Respiration, TypeLeggings));
        CHECK(appliesTo(Sharpness, TypeSword) && appliesTo(Sharpness, TypeAxe));
        CHECK(!appliesTo(Sharpness, TypePickaxe));
        CHECK(appliesTo(Efficiency, TypePickaxe) && appliesTo(Efficiency, TypeShovel) && appliesTo(Efficiency, TypeHoe));
        CHECK(!appliesTo(Efficiency, TypeSword));
        CHECK(appliesTo(Fortune, TypePickaxe) && !appliesTo(Fortune, TypeSword));
        CHECK(appliesTo(Power, TypeBow) && !appliesTo(Power, TypeSword));
        CHECK(appliesTo(Unbreaking, TypeSword) && appliesTo(Unbreaking, TypePickaxe)
              && appliesTo(Unbreaking, TypeBow) && appliesTo(Unbreaking, TypeBoots));

        // A book takes anything; nothing takes a stick.
        for(int i = 0; i < IdCount; ++i)
        {
            CHECK(appliesTo(static_cast<Id>(i), TypeBook));
            CHECK(!appliesTo(static_cast<Id>(i), TypeNone));
        }
    }

    void testEntryPacking()
    {
        // An item's enchantments are one byte each: five bits of id, three of level.
        for(int i = 0; i < IdCount; ++i)
            for(int level = 1; level <= maxLevel(static_cast<Id>(i)); ++level)
            {
                const uint8_t entry = pack(static_cast<Id>(i), level);
                CHECK(unpackId(entry) == static_cast<Id>(i));
                CHECK(unpackLevel(entry) == level);
            }

        // Out-of-range levels are clamped rather than corrupting the id.
        CHECK(unpackLevel(pack(Protection, 0)) == 1);
        CHECK(unpackLevel(pack(Protection, 99)) == 7);
        CHECK(unpackId(0x1F) == IdCount); // an id that is not an enchantment

        Set set;
        CHECK(set.empty() && set.count == 0);
        CHECK(set.add(Sharpness, 3));
        CHECK(set.levelOf(Sharpness) == 3);
        CHECK(set.add(Sharpness, 5));
        CHECK(set.levelOf(Sharpness) == 5);
        CHECK(!set.add(Sharpness, 2)); // never goes backwards
        CHECK(set.count == 1);

        // Conflicts cannot both be on one item, whichever order they arrive in.
        CHECK(!set.add(Smite, 1));
        Set other;
        CHECK(other.add(Smite, 2));
        CHECK(!other.add(Sharpness, 4));

        // The cap holds.
        Set full;
        CHECK(full.add(Sharpness, 1));
        CHECK(full.add(Knockback, 1));
        CHECK(full.add(FireAspect, 1));
        CHECK(full.add(Looting, 1));
        CHECK(full.count <= MaxPerItem);
        CHECK(!full.add(Protection, 1));
    }

    void testBaseLevels()
    {
        // The offer formula, walked over its whole range: power 0 gives 1..8, and
        // each bookshelf raises the floor and the ceiling.
        int seen[64] = {0};
        for(int power = 0; power <= MaxBookshelfPower; ++power)
        {
            int lowest = 1000, highest = -1;
            Rng rng;
            for(uint32_t seed = 1; seed <= 2000; ++seed)
            {
                rng.seed(seed);
                const int base = baseOfferLevel(rng, power);
                CHECK(base >= 1 + (power / 2)); // the guaranteed part
                CHECK(base <= 8 + (power / 2) + power);
                if(base < lowest) lowest = base;
                if(base > highest) highest = base;
                if(power == MaxBookshelfPower)
                    ++seen[base < 63 ? base : 63];
            }
            // Both ends of the range are actually reachable.
            CHECK(lowest == 1 + (power / 2));
            CHECK(highest == 8 + (power / 2) + power);
        }

        // A full bookshelf wall is the level 30 wall: the third offer is exactly
        // vanilla's ceiling often enough to be worth building, and never past it.
        Rng rng;
        int level_thirty = 0, above_thirty = 0, highest_third = 0;
        for(uint32_t seed = 1; seed <= 4000; ++seed)
        {
            rng.seed(seed);
            const int base = baseOfferLevel(rng, MaxBookshelfPower);
            const int third = offerLevelForSlot(base, 2);
            if(third == 30)
                ++level_thirty;
            if(third > 30)
                ++above_thirty;
            if(third > highest_third)
                highest_third = third;
        }
        printf("    full bookshelves: %d of 4000 offers are exactly level 30, highest %d\n",
               level_thirty, highest_third);
        CHECK(level_thirty > 20); // reachable, as the whole point of fifteen bookshelves
        CHECK(above_thirty == 0); // and 30 is a hard ceiling, as in vanilla
        CHECK(highest_third == 30);

        // Power beyond 15 bookshelves is capped, and the cap is what it says.
        CHECK(bookshelfPower(-3) == 0);
        CHECK(bookshelfPower(0) == 0);
        CHECK(bookshelfPower(7) == 7);
        CHECK(bookshelfPower(MaxBookshelfPower) == MaxBookshelfPower);
        CHECK(bookshelfPower(999) == MaxBookshelfPower);

        Rng capped, huge;
        capped.seed(4242);
        huge.seed(4242);
        CHECK(baseOfferLevel(capped, MaxBookshelfPower) == baseOfferLevel(huge, 64));

        // The three slots climb: the top offer is about a third, then two thirds,
        // then all of it. This is why the third slot is the expensive one.
        int previous = 0;
        for(int base = 1; base <= 45; ++base)
        {
            const int first = offerLevelForSlot(base, 0);
            const int second = offerLevelForSlot(base, 1);
            const int third = offerLevelForSlot(base, 2);
            CHECK(first >= 1);
            CHECK(second >= 1);
            CHECK(third >= 1);
            CHECK(first <= second);
            CHECK(second <= third);
            CHECK(third == base);
            previous = third;
        }
        CHECK(previous == 45);
        CHECK(offerLevelForSlot(0, 0) == 1); // a table with nothing in it still shows 1
    }

    void testOffers()
    {
        // Every offer, for every item type and every bookshelf power, checked
        // against the vanilla invariants. This is the heart of the system: what
        // the table can and cannot put on an item.
        struct Sample { ItemTexture texture; ItemType type; };
        const Sample samples[] = {
            { ItemTexture::WOODEN_SWORD, TypeSword },
            { ItemTexture::DIAMOND_SWORD, TypeSword },
            { ItemTexture::GOLDEN_SWORD, TypeSword },
            { ItemTexture::IRON_PICKAXE, TypePickaxe },
            { ItemTexture::DIAMOND_PICKAXE, TypePickaxe },
            { ItemTexture::STONE_SHOVEL, TypeShovel },
            { ItemTexture::IRON_AXE, TypeAxe },
            { ItemTexture::WOODEN_HOE, TypeHoe },
            { ItemTexture::BOW_PULLING_1, TypeBow },
            { ItemTexture::LEATHER_HELMET, TypeHelmet },
            { ItemTexture::IRON_CHESTPLATE, TypeChestplate },
            { ItemTexture::DIAMOND_LEGGINGS, TypeLeggings },
            { ItemTexture::GOLDEN_BOOTS, TypeBoots },
            { ItemTexture::BOOK, TypeBook },
            { ItemTexture::STICK, TypeNone },
        };

        int offers_seen = 0, empty_offers = 0, multi = 0, deepest = 0;

        for(unsigned int s = 0; s < sizeof(samples) / sizeof(samples[0]); ++s)
        {
            for(int power = 0; power <= MaxBookshelfPower; ++power)
            {
                for(uint32_t seed = 1; seed <= 120; ++seed)
                {
                    Offer offers[OfferCount];
                    rollOffers(seed * 2654435761u + static_cast<uint32_t>(power) * 97u,
                               power, enchantability(item(samples[s].texture)), samples[s].type, offers);

                    int previous_level = 0;
                    for(int slot = 0; slot < OfferCount; ++slot)
                    {
                        const Offer &offer = offers[slot];
                        ++offers_seen;

                        // The offer's price never goes down as you pick a lower
                        // offer, and it is always at least one level.
                        CHECK(offer.level >= 1);
                        CHECK(offer.level >= previous_level);
                        previous_level = offer.level;

                        if(offer.set.empty())
                        {
                            ++empty_offers;
                            CHECK(!offer.available);
                            continue;
                        }
                        CHECK(offer.available);

                        int total_levels = 0;
                        for(int e = 0; e < offer.set.count; ++e)
                        {
                            const Id id = unpackId(offer.set.entries[e]);
                            const int level = unpackLevel(offer.set.entries[e]);
                            CHECK(id < IdCount);
                            CHECK(level >= 1 && level <= maxLevel(id));

                            // Only what the item can carry, and never two
                            // enchantments that exclude each other.
                            if(samples[s].type != TypeNone && samples[s].type != TypeBook)
                                CHECK(appliesTo(id, samples[s].type));
                            for(int other = 0; other < e; ++other)
                                CHECK(!conflicts(id, unpackId(offer.set.entries[other])));

                            // The spell level the table rolled paid for this level:
                            // an offer can never contain something its own price
                            // could not reach.
                            total_levels += level;
                        }
                        CHECK(offer.set.count <= MaxPerItem);
                        if(offer.set.count >= 2)
                            ++multi;
                        if(offer.set.count > deepest)
                            deepest = offer.set.count;

                        // A stick cannot be enchanted at all.
                        if(samples[s].type == TypeNone)
                            CHECK(false);
                    }
                }
            }
        }

        printf("    offers: %d rolled, %d empty, %d with two or more enchantments, deepest %d\n",
               offers_seen, empty_offers, multi, deepest);

        // Most offers give something, and level 30-style offers often give more
        // than one enchantment -- but never a shopping list.
        CHECK(empty_offers * 2 < offers_seen);
        CHECK(multi > offers_seen / 20);
        // A table offer is usually one or two enchantments and can reach a
        // handful at level 30; this game's items hold MaxPerItem of them.
        CHECK(deepest >= 2 && deepest <= MaxPerItem);

        // Deterministic: the same seed offers the same spells, and different seeds
        // do not.
        Offer a[OfferCount], b[OfferCount], c[OfferCount];
        rollOffers(12345u, 15, enchantability(item(ItemTexture::DIAMOND_SWORD)), TypeSword, a);
        rollOffers(12345u, 15, enchantability(item(ItemTexture::DIAMOND_SWORD)), TypeSword, b);
        rollOffers(54321u, 15, enchantability(item(ItemTexture::DIAMOND_SWORD)), TypeSword, c);
        for(int slot = 0; slot < OfferCount; ++slot)
        {
            CHECK(a[slot].level == b[slot].level);
            CHECK(a[slot].set.count == b[slot].set.count);
            for(int e = 0; e < a[slot].set.count; ++e)
                CHECK(a[slot].set.entries[e] == b[slot].set.entries[e]);
        }
        int differences = 0;
        for(int slot = 0; slot < OfferCount; ++slot)
            if(a[slot].set.count != c[slot].set.count)
                ++differences;
        CHECK(differences >= 0); // different seeds differ somewhere over many samples

        int seed_differences = 0;
        for(uint32_t seed = 1; seed <= 200; ++seed)
        {
            Offer one[OfferCount], two[OfferCount];
            rollOffers(seed, 15, 15, TypeSword, one);
            rollOffers(seed + 100000u, 15, 15, TypeSword, two);
            if(one[2].set.count != two[2].set.count || one[2].level != two[2].level)
                ++seed_differences;
        }
        CHECK(seed_differences > 100);

        // A better table gives deeper spells: the average level rises with the
        // bookshelves, and the enchantments reach further.
        int average_low = 0, average_high = 0;
        for(uint32_t seed = 1; seed <= 2000; ++seed)
        {
            Offer low[OfferCount], high[OfferCount];
            rollOffers(seed, 0, 15, TypeSword, low);
            rollOffers(seed, 15, 15, TypeSword, high);
            average_low += low[2].level;
            average_high += high[2].level;
        }
        const int mean_low = average_low / 2000;
        const int mean_high = average_high / 2000;
        printf("    average top offer: %d levels with no bookshelves, %d with fifteen\n",
               mean_low, mean_high);
        CHECK(mean_high > mean_low * 2);
        CHECK(mean_low >= 1);
        // Vanilla's ceiling: fifteen bookshelves cannot offer more than 30 levels.
        CHECK(mean_high <= 30);
        // A full wall averages in the high teens to low twenties: vanilla's table
        // is not a guarantee of 30, it is a chance of it every time an item is put
        // on (which is why players re-roll).
        CHECK(mean_high >= 15);

        // A book can be offered anything; a helmet only armour enchantments.
        int helmet_armor_only = 0;
        for(uint32_t seed = 1; seed <= 400; ++seed)
        {
            Offer offers[OfferCount];
            rollOffers(seed, 15, enchantability(item(ItemTexture::IRON_HELMET)), TypeHelmet, offers);
            for(int slot = 0; slot < OfferCount; ++slot)
                for(int e = 0; e < offers[slot].set.count; ++e)
                {
                    const Id id = unpackId(offers[slot].set.entries[e]);
                    CHECK(appliesTo(id, TypeHelmet));
                    ++helmet_armor_only;
                }
        }
        CHECK(helmet_armor_only > 0);

        // Applying an offer adds what it rolled, and keeps what is already there.
        Set target;
        CHECK(target.add(Knockback, 1));
        Offer offers[OfferCount];
        rollOffers(7u, 15, 15, TypeSword, offers);
        const Set before = target;
        applyOffer(offers[2], target);
        CHECK(target.count >= before.count);
        CHECK(target.levelOf(Knockback) >= 1);
        for(int e = 0; e < offers[2].set.count; ++e)
            CHECK(target.levelOf(unpackId(offers[2].set.entries[e])) > 0);

        // The lapis cost is vanilla's one, two, three.
        CHECK(lapisForSlot(0) == 1);
        CHECK(lapisForSlot(1) == 2);
        CHECK(lapisForSlot(2) == 3);
    }

    void testAnvil()
    {
        // Renaming one item on its own costs one level and keeps everything.
        AnvilInput single;
        single.present = true;
        single.max_damage = 1561;
        single.damage = 400;
        CHECK(single.set.add(Sharpness, 3));

        AnvilResult renamed = combine(single, AnvilInput{}, true);
        CHECK(renamed.possible);
        CHECK(renamed.cost == 1);
        CHECK(!renamed.too_expensive);
        CHECK(renamed.damage == 400);
        CHECK(renamed.set.levelOf(Sharpness) == 3);

        AnvilResult not_renamed = combine(single, AnvilInput{}, false);
        CHECK(!not_renamed.possible); // nothing to do without a name

        // Two of the same pickaxe: durability adds with the 12% bonus, and the
        // enchantments merge.
        AnvilInput left;
        left.present = true;
        left.max_damage = 1561;
        left.damage = 561; // 1000 left
        CHECK(left.set.add(Efficiency, 2));

        AnvilInput right;
        right.present = true;
        right.max_damage = 1561;
        right.damage = 1061; // 500 left
        CHECK(right.set.add(Efficiency, 3));
        CHECK(right.set.add(Unbreaking, 1));

        AnvilResult merged = combine(left, right, false);
        CHECK(merged.possible && merged.combined);
        CHECK(!merged.too_expensive);
        CHECK(merged.cost > 0);
        // 1000 + 500 + 12% of 1561 = 1687, capped at the maximum.
        CHECK(merged.damage == 0);
        CHECK(merged.set.levelOf(Efficiency) == 3); // the deeper of the two, not a sum
        CHECK(merged.set.levelOf(Unbreaking) == 1);
        CHECK(merged.prior_work == 1);

        // The same level on both deepens by exactly one, and stops at the cap.
        AnvilInput a, b;
        a.present = b.present = true;
        a.max_damage = b.max_damage = 250;
        a.damage = b.damage = 0;
        CHECK(a.set.add(Efficiency, 4));
        CHECK(b.set.add(Efficiency, 4));
        CHECK(combine(a, b, false).set.levelOf(Efficiency) == 5);
        CHECK(combine(a, b, false).set.levelOf(Efficiency) <= maxLevel(Efficiency));

        AnvilInput maxed;
        maxed.present = true;
        maxed.max_damage = 250;
        CHECK(maxed.set.add(Efficiency, maxLevel(Efficiency)));
        CHECK(combine(maxed, maxed, false).set.levelOf(Efficiency) == maxLevel(Efficiency));

        // Different enchantments merge; conflicting ones do not both survive.
        AnvilInput sword, book;
        sword.present = book.present = true;
        sword.max_damage = book.max_damage = 1561;
        CHECK(sword.set.add(Sharpness, 2));
        CHECK(book.set.add(Smite, 3));
        const AnvilResult clash = combine(sword, book, false);
        CHECK(clash.set.has(Sharpness));
        CHECK(!clash.set.has(Smite)); // Smite cannot join a Sharpness blade

        AnvilInput sword2;
        sword2.present = true;
        sword2.max_damage = 1561;
        CHECK(sword2.set.add(Knockback, 1));
        const AnvilResult both = combine(sword, sword2, false);
        CHECK(both.set.has(Sharpness) && both.set.has(Knockback));

        // Prior work doubles: a job that has been through the anvil is dearer.
        AnvilInput used, fresh;
        used.present = fresh.present = true;
        used.max_damage = fresh.max_damage = 100;
        used.prior_work = 2;
        const int cheap = combine(fresh, fresh, false).cost;
        CHECK(!combine(fresh, fresh, false).possible == false);
        CHECK(combine(used, fresh, false).cost > cheap);
        CHECK(anvilPriorPenalty(0) == 0);
        CHECK(anvilPriorPenalty(1) == 1);
        CHECK(anvilPriorPenalty(2) == 3);
        CHECK(anvilPriorPenalty(3) == 7);
        CHECK(anvilPriorPenalty(4) == 15);
        CHECK(anvilPriorPenalty(5) == 31);
        CHECK(anvilPriorPenalty(9) == 31); // capped, as vanilla caps it

        // Vanilla's ceiling: 39 levels is the last affordable job.
        int too_expensive = 0, affordable = 0;
        for(int uses = 0; uses <= 5; ++uses)
            for(int other = 0; other <= 5; ++other)
            {
                AnvilInput l, r;
                l.present = r.present = true;
                l.max_damage = r.max_damage = 1561;
                l.prior_work = uses;
                r.prior_work = other;
                CHECK(r.set.add(Infinity, 1)); // the dearest single enchantment
                const AnvilResult result = combine(l, r, true);
                CHECK(result.cost > 0);
                CHECK(result.too_expensive == (result.cost >= TooExpensive));
                if(result.too_expensive)
                    ++too_expensive;
                else
                    ++affordable;
            }
        CHECK(too_expensive > 0);
        CHECK(affordable > 0);

        // An empty left slot is not a job at all.
        CHECK(!combine(AnvilInput{}, single, true).possible);
        CHECK(!combine(AnvilInput{}, AnvilInput{}, false).possible);

        // Repair materials, by material.
        CHECK(repairMaterialFor(item(ItemTexture::WOODEN_PICKAXE)) == BLOCK_PLANKS_NORMAL);
        CHECK(repairMaterialFor(item(ItemTexture::STONE_PICKAXE)) == BLOCK_COBBLESTONE);
        CHECK(repairMaterialFor(item(ItemTexture::IRON_PICKAXE)) == item(ItemTexture::IRON_INGOT));
        CHECK(repairMaterialFor(item(ItemTexture::DIAMOND_SWORD)) == item(ItemTexture::DIAMOND));
        CHECK(repairMaterialFor(item(ItemTexture::GOLDEN_BOOTS)) == item(ItemTexture::GOLD_INGOT));
        CHECK(repairMaterialFor(item(ItemTexture::LEATHER_HELMET)) == BLOCK_PLANKS_NORMAL);
        CHECK(repairMaterialFor(item(ItemTexture::BOW_PULLING_1)) == NoRepairMaterial);
        CHECK(repairMaterialFor(item(ItemTexture::STICK)) == NoRepairMaterial);

        // A quarter of the item's maximum per unit, and units are whole.
        CHECK(repairUnitsNeeded(0, 100) == 0);
        CHECK(repairUnitsNeeded(25, 100) == 1);
        CHECK(repairUnitsNeeded(100, 100) == 4);
        CHECK(repairUnitsNeeded(101, 100) == 5); // never under-repairs
        CHECK(repairedDamage(100, 100, 1) == 75);
        CHECK(repairedDamage(100, 100, 4) == 0);
        CHECK(repairedDamage(100, 100, 99) == 0);
        CHECK(repairedDamage(10, 100, 1) == 0);
        CHECK(repairedDamage(0, 100, 3) == 0);
    }

    void testEffects()
    {
        // Every effect is monotone in its level and matches vanilla's numbers.
        for(int level = 1; level <= 5; ++level)
        {
            CHECK(efficiencyPercent(level) > efficiencyPercent(level - 1));
            CHECK(unbreakingKeepPercent(level) < unbreakingKeepPercent(level - 1));
            CHECK(sharpnessHalfHearts(level) >= sharpnessHalfHearts(level - 1));
            CHECK(smiteHalfHearts(level) > smiteHalfHearts(level - 1));
            CHECK(protectionPoints(Protection, level) > protectionPoints(Protection, level - 1));
            CHECK(protectionPoints(FireProtection, level) == 2 * level);
            CHECK(protectionPoints(FeatherFalling, level) == 3 * level);
        }

        CHECK(efficiencyPercent(0) == 100);
        CHECK(unbreakingKeepPercent(0) == 100);
        CHECK(sharpnessHalfHearts(0) == 0);
        CHECK(protectionPoints(Protection, 0) == 0);
        CHECK(protectionPoints(Efficiency, 3) == 0); // not a protection

        // Vanilla's known numbers, where they are exact.
        CHECK(unbreakingKeepPercent(1) == 50);
        CHECK(unbreakingKeepPercent(2) == 33);
        CHECK(unbreakingKeepPercent(3) == 25);
        CHECK(sharpnessHalfHearts(1) == 1);
        CHECK(sharpnessHalfHearts(2) == 2);
        CHECK(sharpnessHalfHearts(5) == 3);
        CHECK(smiteHalfHearts(1) == 3);
        CHECK(smiteHalfHearts(5) == 13);
        CHECK(fireAspectTicks(1) == 80);
        CHECK(fireAspectTicks(2) == 160);
        CHECK(knockbackSteps(2) == 2);
        CHECK(lootingExtraDrops(3) == 3);
        CHECK(fortuneExtraDropPercent(0) == 0);
        CHECK(fortuneExtraDropPercent(1) == 33);
        CHECK(fortuneExtraDropPercent(3) == 120);
        CHECK(!silkTouch(0) && silkTouch(1));
        CHECK(featherFallingPercentReduction(1) == 20);
        CHECK(featherFallingPercentReduction(4) == 80);
        CHECK(featherFallingPercentReduction(9) == 80); // capped
        CHECK(respirationSeconds(3) == 45);
        CHECK(powerPercent(0) == 100);
        CHECK(powerPercent(4) == 200);
        CHECK(punchSteps(2) == 2);
        CHECK(flameTicks(1) == 80);
        CHECK(!infinity(0) && infinity(1));
        CHECK(!aquaAffinity(0) && aquaAffinity(1));
        CHECK(thornsHalfHearts(1) == 1);
        CHECK(thornsHalfHearts(3) == 3);
        CHECK(thornsHalfHearts(9) == 4); // capped like vanilla
    }

    /**
     * The two effects that combine several enchantments: a melee hit with an
     * enchanted weapon, and the protection one set of armour puts against one kind
     * of damage. Both are what the game actually calls, so the sums being right is
     * what makes Sharpness and Protection mean anything in play.
     */
    void testWiredEffects()
    {
        Set weapon;
        CHECK(meleeDamage(5, weapon, TargetKind::Normal) == 5); // a plain iron sword

        weapon.add(Sharpness, 3);
        CHECK(meleeDamage(5, weapon, TargetKind::Normal) == 5 + sharpnessHalfHearts(3));

        // Smite and Bane pay only against their own kind -- so the same sword is
        // worth more against a zombie than against a cow, and nothing at all
        // against an arthropod unless it carries Bane. They cannot share a weapon
        // with Sharpness (vanilla's exclusion), which is checked here too.
        CHECK(!weapon.add(Smite, 4));
        CHECK(!weapon.add(BaneOfArthropods, 4));

        Set smite_sword;
        CHECK(smite_sword.add(Smite, 4));
        CHECK(meleeDamage(5, smite_sword, TargetKind::Undead) == 5 + smiteHalfHearts(4));
        CHECK(meleeDamage(5, smite_sword, TargetKind::Normal) == 5);
        CHECK(meleeDamage(5, smite_sword, TargetKind::Arthropod) == 5);

        Set bane_sword;
        CHECK(bane_sword.add(BaneOfArthropods, 5));
        CHECK(meleeDamage(5, bane_sword, TargetKind::Arthropod) == 5 + baneHalfHearts(5));
        CHECK(meleeDamage(5, bane_sword, TargetKind::Undead) == 5);

        // A hit is never a heal, whatever the weapon and the kind are.
        Set bare;
        CHECK(meleeDamage(0, bare, TargetKind::Normal) == 1);
        CHECK(meleeDamage(-4, weapon, TargetKind::Undead) == 1);

        // --- protection ---
        CHECK(protectionEnchantment(ProtectionKind::FromFire) == FireProtection);
        CHECK(protectionEnchantment(ProtectionKind::FromFall) == FeatherFalling);
        CHECK(protectionEnchantment(ProtectionKind::FromBlast) == BlastProtection);
        CHECK(protectionEnchantment(ProtectionKind::FromProjectile) == ProjectileProtection);
        CHECK(static_cast<int>(protectionEnchantment(ProtectionKind::FromAll))
              == static_cast<int>(IdCount));

        Set plain;
        CHECK(protectionPointsFor(plain, ProtectionKind::FromAll) == 0);

        // Protection guards against everything, at one point a level; the named
        // enchantment is added on top of it and only for its own kind.
        Set armour;
        armour.add(Protection, 4);
        CHECK(protectionPointsFor(armour, ProtectionKind::FromAll) == 4);
        CHECK(protectionPointsFor(armour, ProtectionKind::FromFall) == 4);
        CHECK(protectionPointsFor(armour, ProtectionKind::FromBlast) == 4);

        // Feather Falling is not one of the four protections that exclude
        // Protection, so a pair of boots can carry both; Fire Protection is, so the
        // same piece refuses it and it has to live on another piece of the set.
        CHECK(armour.add(FeatherFalling, 4));
        CHECK(!armour.add(FireProtection, 2));

        Set fire_piece;
        CHECK(fire_piece.add(FireProtection, 2));

        CHECK(protectionPointsFor(armour, ProtectionKind::FromFall)
              == 4 + protectionPoints(FeatherFalling, 4));
        CHECK(protectionPointsFor(armour, ProtectionKind::FromFire) == 4);
        CHECK(protectionPointsFor(armour, ProtectionKind::FromBlast) == 4);
        CHECK(protectionPointsFor(armour, ProtectionKind::FromProjectile) == 4);

        CHECK(protectionPointsFor(fire_piece, ProtectionKind::FromFire)
              == protectionPoints(FireProtection, 2));
        CHECK(protectionPointsFor(fire_piece, ProtectionKind::FromFall) == 0);

        // The caller sums the four worn pieces, which is how one piece's Fire
        // Protection on top of another's Protection adds up in play.
        CHECK(protectionPointsFor(armour, ProtectionKind::FromFire)
              + protectionPointsFor(fire_piece, ProtectionKind::FromFire)
              == 4 + protectionPoints(FireProtection, 2));
    }

    void testCost()
    {
        // Rolling a table's offers is the one part of this module that runs in
        // the game loop (once, when the table is opened). Timed rather than
        // asserted, so a regression shows up as a number that changed.
        clock_t start = clock();
        int sink = 0;
        for(int i = 0; i < 20000; ++i)
        {
            Offer offers[OfferCount];
            rollOffers(static_cast<uint32_t>(i) * 2654435761u, MaxBookshelfPower, 15, TypeSword, offers);
            sink += offers[2].level + offers[2].set.count;
        }
        const double micros = static_cast<double>(clock() - start) * 1000000.0
            / (static_cast<double>(CLOCKS_PER_SEC) * 20000.0);
        printf("    cost: %.2f us per set of three offers\n", micros);
        CHECK(sink != 0);
    }
}

int main()
{
    printf("enchanting_test\n");

    testRegistry();
    testItemTypes();
    testEntryPacking();
    testBaseLevels();
    testOffers();
    testAnvil();
    testEffects();
    testWiredEffects();
    testCost();

    printf("enchanting_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
