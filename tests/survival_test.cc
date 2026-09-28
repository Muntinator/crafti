// Host tests for the survival rules: hunger and saturation, regeneration and
// starvation, damage sources, status-effect tables and multipliers, and the
// experience curve.
//
// The module is pure rules with no engine state, so everything here is exact
// arithmetic rather than a simulation.
//
// Build and run with `make -C tests`.

#include "survival.h"
#include "textures/items.h"

#include <stdio.h>
#include <stdint.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

using namespace Survival;

static bool near(float a, float b) { float d = a - b; return d < 0.001f && d > -0.001f; }

// ---------------------------------------------------------------- hunger

static void test_food_table()
{
    // Every entry must be edible, named, and small enough for getBLOCKDATA()'s
    // 7-bit item field, or the player would eat a different item than the one
    // the HUD is showing.
    const uint8_t samples[] = { static_cast<uint8_t>(ItemTexture::APPLE),
                                static_cast<uint8_t>(ItemTexture::COOKED_BEEF),
                                static_cast<uint8_t>(ItemTexture::BREAD),
                                static_cast<uint8_t>(ItemTexture::COOKED_CHICKEN) };
    for(unsigned int i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
    {
        const FoodValue *f = foodValue(samples[i]);
        CHECK(f != nullptr);
        CHECK(isFood(samples[i]));
        if(f != nullptr)
        {
            CHECK(f->item == samples[i]);
            CHECK(f->hunger > 0 && f->hunger <= MaxHunger);
            CHECK(f->saturation >= 0.0f);
            CHECK(f->name != nullptr && f->name[0] != '\0');
            CHECK(f->item < 128);
        }
    }

    // A cooked food is always at least as good as its raw form.
    CHECK(foodValue(static_cast<uint8_t>(ItemTexture::COOKED_BEEF))->hunger >
          foodValue(static_cast<uint8_t>(ItemTexture::RAW_BEEF))->hunger);
    CHECK(foodValue(static_cast<uint8_t>(ItemTexture::COOKED_COD))->hunger >
          foodValue(static_cast<uint8_t>(ItemTexture::RAW_COD))->hunger);
    CHECK(foodValue(static_cast<uint8_t>(ItemTexture::COOKED_CHICKEN))->hunger >
          foodValue(static_cast<uint8_t>(ItemTexture::RAW_CHICKEN))->hunger);
    CHECK(foodValue(static_cast<uint8_t>(ItemTexture::COOKED_PORKCHOP))->hunger >
          foodValue(static_cast<uint8_t>(ItemTexture::RAW_PORKCHOP))->hunger);

    // Tools and blocks are not food, and an unknown id must not wrap into the
    // table. Low block ids are checked on purpose: blocks share the id space
    // with items, so a stray food entry would make the player eat stone.
    CHECK(foodValue(static_cast<uint8_t>(ItemTexture::COAL)) == nullptr);
    CHECK(foodValue(static_cast<uint8_t>(ItemTexture::IRON_HELMET)) == nullptr);
    CHECK(foodValue(static_cast<uint8_t>(ItemTexture::STICK)) == nullptr);
    CHECK(foodValue(static_cast<uint8_t>(BLOCK_STONE)) == nullptr);
    CHECK(!isFood(static_cast<uint8_t>(BLOCK_DIRT)));
    CHECK(foodValue(255) == nullptr);
}

static void test_hunger_state()
{
    HungerState s;
    resetHunger(s);
    CHECK(s.hunger == MaxHunger);
    CHECK(near(s.saturation, 0.0f));
    CHECK(s.exhaustion == 0.0f);
    CHECK(s.timer == 0);

    // A full player refuses food when there is still saturation to burn, and
    // accepts it once hunger has dropped.
    HungerState full;
    resetHunger(full);
    full.saturation = 5.0f;
    CHECK(!canEat(full));
    full.saturation = 0.0f;
    CHECK(canEat(full));
    full.hunger = 19;
    CHECK(canEat(full));

    // Eating clamps hunger and caps saturation at the current hunger, which is
    // vanilla's rule and stops a starving player banking saturation.
    HungerState e;
    resetHunger(e);
    e.hunger = 5;
    eat(e, *foodValue(static_cast<uint8_t>(ItemTexture::COOKED_BEEF)));
    CHECK(e.hunger == 13);
    CHECK(near(e.saturation, MaxSaturation)); // capped at the old hunger, not 12.8

    HungerState over;
    resetHunger(over);
    over.hunger = 19;
    over.saturation = 9.0f;
    eat(over, *foodValue(static_cast<uint8_t>(ItemTexture::GOLDEN_APPLE)));
    CHECK(over.hunger == MaxHunger);
    CHECK(over.saturation <= MaxSaturation);

    // A rotten-ish food with no saturation still restores hunger.
    HungerState plain;
    resetHunger(plain);
    plain.hunger = 10;
    eat(plain, *foodValue(static_cast<uint8_t>(ItemTexture::POTATO)));
    CHECK(plain.hunger == 11);
}

static void test_exhaustion()
{
    HungerState s;
    resetHunger(s);
    s.saturation = 5.0f;

    // Fractional exhaustion accumulates instead of being lost per call.
    CHECK(!addExhaustion(s, 1.0f));
    CHECK(!addExhaustion(s, 1.0f));
    CHECK(near(s.exhaustion, 2.0f));
    CHECK(!addExhaustion(s, 2.0f)); // reaches the threshold, burns saturation only
    CHECK(near(s.saturation, 4.0f));
    CHECK(near(s.exhaustion, 0.0f));
    CHECK(s.hunger == MaxHunger);

    // Burn through the saturation, then hunger starts draining and the returned
    // flag reports it (that is what drives the HUD flash and the sound).
    // Saturation is 4.0 here, so 16 points of exhaustion spend it; the next 8
    // points must take exactly two hunger points.
    bool drained = false;
    for(int i = 0; i < 24; ++i)
        drained = addExhaustion(s, 1.0f) || drained;
    CHECK(near(s.saturation, 0.0f));
    CHECK(s.hunger == MaxHunger - 2);
    CHECK(drained);

    // At zero hunger a point of exhaustion cannot be paid, so it is swallowed.
    HungerState empty;
    resetHunger(empty);
    empty.hunger = 0;
    CHECK(!addExhaustion(empty, 100.0f));
    CHECK(empty.hunger == 0);

    // Negative or zero amounts never create energy.
    HungerState n;
    resetHunger(n);
    CHECK(!addExhaustion(n, 0.0f));
    CHECK(!addExhaustion(n, -5.0f));
    CHECK(n.exhaustion == 0.0f);
    CHECK(n.hunger == MaxHunger);
}

static void test_regeneration_and_starvation()
{
    // Vanilla: 18+ hunger regenerates, quickly with saturation and slowly
    // without. Test both branches explicitly.
    HungerState fed;
    resetHunger(fed);
    fed.hunger = 20;
    fed.saturation = 0.0f;
    CHECK(regenerationIntervalTicks(fed) == 80);
    fed.saturation = 1.0f;
    CHECK(regenerationIntervalTicks(fed) == 10);

    fed.hunger = 17;
    fed.saturation = 5.0f;
    CHECK(regenerationIntervalTicks(fed) == 0);
    fed.hunger = 18;
    CHECK(regenerationIntervalTicks(fed) == 10);

    // Starvation only bites at zero hunger, and it must never kill outright.
    HungerState starve;
    resetHunger(starve);
    starve.hunger = 1;
    CHECK(starvationIntervalTicks(starve) == 0);
    starve.hunger = 0;
    CHECK(starvationIntervalTicks(starve) == 80);
    CHECK(StarvationHealthFloor >= 1);
}

// ---------------------------------------------------------------- damage

static void test_damage_sources()
{
    CHECK(fallDamage(0) == 0);
    CHECK(fallDamage(SafeFallBlocks) == 0);
    CHECK(fallDamage(SafeFallBlocks + 1) == 1);
    CHECK(fallDamage(10) == 10 - SafeFallBlocks);
    CHECK(fallDamage(-4) == 0);
    // Fall damage is monotonic, so a longer drop is never safer.
    for(int i = 0; i < 40; ++i)
        CHECK(fallDamage(i + 1) >= fallDamage(i));

    CHECK(sourceDamage(Damage::Drown) == DrownDamage);
    CHECK(sourceDamage(Damage::Fire) == FireDamage);
    CHECK(sourceDamage(Damage::Starve) == StarvationDamage);
    CHECK(sourceDamage(Damage::Suffocate) == SuffocateDamage);
    CHECK(sourceDamage(Damage::Void) == VoidDamage);
    CHECK(sourceDamage(Damage::Lava) > 0);
    CHECK(sourceDamage(Damage::Cactus) > 0);
    // Only direct sources may report zero damage (they are handled by the mob
    // and explosion code, which knows the amount).
    CHECK(sourceDamage(Damage::Fall) == 0);
    CHECK(sourceDamage(Damage::Mob) == 0);

    CHECK(ignoresResistance(Damage::Starve));
    CHECK(ignoresResistance(Damage::Drown));
    CHECK(ignoresResistance(Damage::Void));
    CHECK(ignoresResistance(Damage::Poison));
    CHECK(ignoresResistance(Damage::Wither));
    CHECK(ignoresResistance(Damage::Magic));
    CHECK(!ignoresResistance(Damage::Fall));
    CHECK(!ignoresResistance(Damage::Mob));

    CHECK(blockedByFireResistance(Damage::Fire));
    CHECK(blockedByFireResistance(Damage::Lava));
    CHECK(!blockedByFireResistance(Damage::Drown));

    // Air and fire budgets must stay inside the tick range the caller stores.
    CHECK(MaxAir > 0 && MaxAir <= 0xFFFF);
    CHECK(AirRecoveryPerTick > 0);
    CHECK(FireTicksFromLava >= FireTicksFromFireBlock);
    CHECK(DrownDamageInterval > 0);
    CHECK(FireDamageInterval > 0);
    CHECK(CactusInterval > 0);
}

// ------------------------------------------------------ status effects

static void test_effect_tables()
{
    for(int e = 0; e < EffectCount; ++e)
    {
        const char *name = effectName(static_cast<uint8_t>(e));
        CHECK(name != nullptr && name[0] != '\0');
    }
    // Distinct names, so the HUD and the debug screen are unambiguous.
    for(int i = 0; i < EffectCount; ++i)
        for(int j = i + 1; j < EffectCount; ++j)
            CHECK(effectName(static_cast<uint8_t>(i)) != effectName(static_cast<uint8_t>(j)));

    CHECK(effectName(EffectCount) != nullptr); // out of range must be safe
    CHECK(effectName(255) != nullptr);
    CHECK(!effectIsGood(255));

    CHECK(effectIsGood(Speed));
    CHECK(effectIsGood(Regeneration));
    CHECK(effectIsGood(NightVision));
    CHECK(!effectIsGood(Poison));
    CHECK(!effectIsGood(Wither));
    CHECK(!effectIsGood(Hunger));
    CHECK(!effectIsGood(Slowness));

    CHECK(effectIsInstant(InstantHealth));
    CHECK(effectIsInstant(InstantDamage));
    CHECK(!effectIsInstant(Regeneration));
    CHECK(!effectIsInstant(255));

    // Effects that damage the player must report an interval, and vice versa.
    for(int e = 0; e < EffectCount; ++e)
    {
        const int interval = effectDamageInterval(static_cast<uint8_t>(e));
        const int amount = effectDamageAmount(static_cast<uint8_t>(e), 0);
        CHECK((interval > 0) == (amount > 0));
    }
    CHECK(effectDamageAmount(Poison, 0) == 1);
    CHECK(effectDamageAmount(Poison, 3) == 1); // level scales duration, not damage
    CHECK(effectDamageAmount(Wither, 2) == 1);
    CHECK(stopsAtOneHeart(Poison));
    CHECK(!stopsAtOneHeart(Wither));
}

static void test_effect_durations_and_mappings()
{
    CHECK(durationForLevel(0, 30) == 30 * TicksPerSecond);
    CHECK(durationForLevel(1, 30) == 60 * TicksPerSecond);
    CHECK(durationForLevel(-3, 30) == 30 * TicksPerSecond); // negative clamps
    CHECK(durationForLevel(0, 0) == 0);
    CHECK(durationForLevel(0, -10) == 0);
    // Extremely long durations saturate rather than wrapping.
    CHECK(durationForLevel(9, 3600) == 0xFFFF);

    // Regeneration gets faster with the level and then flattens at the floor.
    int previous = regenerationIntervalForEffect(0);
    CHECK(previous > 0);
    for(int level = 1; level < 8; ++level)
    {
        const int now = regenerationIntervalForEffect(static_cast<uint8_t>(level));
        CHECK(now <= previous);
        CHECK(now >= 10);
        previous = now;
    }
    CHECK(regenerationIntervalForEffect(0) > regenerationIntervalForEffect(3));

    // Vanilla ids are a real round trip, and unknown ids are rejected instead of
    // silently decoding as effect 0.
    CHECK(effectFromVanillaId(VanillaPoison) == Poison);
    CHECK(effectFromVanillaId(VanillaNightVision) == NightVision);
    CHECK(effectFromVanillaId(VanillaSpeed) == Speed);
    CHECK(effectFromVanillaId(1234) == EffectCount);
    CHECK(effectFromVanillaId(0) == EffectCount);
    CHECK(effectToVanillaId(EffectCount) == 0);

    for(int e = 0; e < EffectCount; ++e)
    {
        const int vanilla = effectToVanillaId(static_cast<uint8_t>(e));
        if(vanilla != 0)
            CHECK(effectFromVanillaId(vanilla) == e);
    }
    // Nausea and blindness have no implementation here, so the ids vanilla uses
    // for them must not be claimed by something else.
    CHECK(effectFromVanillaId(9) == EffectCount);  // nausea
    CHECK(effectFromVanillaId(15) == EffectCount); // blindness
    CHECK(effectFromVanillaId(14) == EffectCount); // invisibility
}

static void test_multipliers()
{
    // Speed strengthens with the level and is capped so the fixed-point player
    // controller cannot outrun chunk loading.
    CHECK(near(movementMultiplier(Speed, 0), 1.2f));
    CHECK(movementMultiplier(Speed, 1) > movementMultiplier(Speed, 0));
    CHECK(movementMultiplier(Speed, 9) <= 2.0f);
    for(int level = 0; level < 12; ++level)
        CHECK(movementMultiplier(Speed, static_cast<uint8_t>(level)) <= 2.0f);

    // Slowness slows and never reaches zero speed.
    CHECK(movementMultiplier(Slowness, 0) < 1.0f);
    CHECK(movementMultiplier(Slowness, 3) < movementMultiplier(Slowness, 0));
    for(int level = 0; level < 12; ++level)
        CHECK(movementMultiplier(Slowness, static_cast<uint8_t>(level)) > 0.0f);
    CHECK(near(movementMultiplier(Poison, 3), 1.0f)); // unrelated effect
    CHECK(near(miningMultiplier(Poison, 3), 1.0f));
    CHECK(near(attackMultiplier(Poison, 3), 1.0f));

    CHECK(near(miningMultiplier(Haste, 0), 1.2f));
    CHECK(miningMultiplier(Haste, 9) <= 2.0f);
    CHECK(miningMultiplier(MiningFatigue, 0) < 1.0f);
    CHECK(miningMultiplier(MiningFatigue, 3) < miningMultiplier(MiningFatigue, 1));
    for(int level = 0; level < 12; ++level)
        CHECK(miningMultiplier(MiningFatigue, static_cast<uint8_t>(level)) > 0.0f);

    CHECK(attackMultiplier(Strength, 0) > 1.0f);
    CHECK(attackMultiplier(Strength, 2) > attackMultiplier(Strength, 0));
    CHECK(attackMultiplier(Weakness, 0) < 1.0f);
    CHECK(attackMultiplier(Weakness, 9) > 0.0f);

    // Resistance caps at 80% reduction, so it can never make the player immune.
    CHECK(near(resistanceMultiplier(0), 0.8f));
    CHECK(resistanceMultiplier(4) < resistanceMultiplier(0));
    for(int level = 0; level < 12; ++level)
        CHECK(resistanceMultiplier(static_cast<uint8_t>(level)) >= 0.2f);

    CHECK(jumpMultiplier(0) > 1.0f);
    CHECK(jumpMultiplier(1) > jumpMultiplier(0));

    CHECK(absorptionHealth(0) == 4);
    CHECK(absorptionHealth(2) == 12);

    CHECK(hungerDrainMultiplier(0) > 1.0f);
    CHECK(hungerDrainMultiplier(2) > hungerDrainMultiplier(0));

    CHECK(lightFloorFor(NightVision) == NightVisionLightFloor);
    // The floor is handed to the renderer as a 0..255 sky light level, so it has
    // to be a usable level rather than a scaled or negative one.
    CHECK(NightVisionLightFloor > 0 && NightVisionLightFloor <= 255);
    CHECK(lightFloorFor(Poison) == 0);
    CHECK(lightFloorFor(255) == 0);
}

// ------------------------------------------------------------------ xp

static void test_experience()
{
    CHECK(xpBlockRange(BLOCK_COAL_ORE).max > 0);
    CHECK(xpBlockRange(BLOCK_DIAMOND_ORE).max > xpBlockRange(BLOCK_COAL_ORE).max);
    CHECK(xpBlockRange(BLOCK_REDSTONE_ORE).max > 0);
    CHECK(xpBlockRange(BLOCK_GOLD_ORE).max > 0);
    // Iron, stone, dirt and air give nothing.
    CHECK(xpBlockRange(BLOCK_IRON_ORE).max == 0);
    CHECK(xpBlockRange(BLOCK_STONE).max == 0);
    CHECK(xpBlockRange(BLOCK_DIRT).max == 0);

    const XpRange r = xpBlockRange(BLOCK_DIAMOND_ORE);
    CHECK(xpRoll(r, 0) == r.min);
    CHECK(xpRoll(r, 1) == r.min + 1);
    CHECK(xpRoll(r, 1000000) >= r.min);
    CHECK(xpRoll(r, 1000000) <= r.max);
    CHECK(xpRoll(XpRange{ 0, 0 }, 7) == 0);
    CHECK(xpRoll(XpRange{ 4, 2 }, 7) == 4); // inverted range must not underflow

    CHECK(xpFromMob(XpMob::Hostile) > 0);
    CHECK(xpFromMob(XpMob::Passive) > 0);
    CHECK(xpFromMob(XpMob::Boss) > xpFromMob(XpMob::Hostile));

    // Vanilla's level curve.
    CHECK(xpForNextLevel(0) == 7);
    CHECK(xpForNextLevel(1) == 9);
    CHECK(xpForNextLevel(15) == 37);
    CHECK(xpForNextLevel(16) == 42);
    CHECK(xpForNextLevel(30) == 112);
    CHECK(xpForNextLevel(31) == 121);
    CHECK(xpForNextLevel(-5) == 7);

    CHECK(totalXpForLevel(0) == 0);
    CHECK(totalXpForLevel(1) == 7);
    CHECK(totalXpForLevel(16) == 16 * 16 + 6 * 16);
    CHECK(totalXpForLevel(-3) == 0);

    // The two functions describe the same curve: the gap between consecutive
    // totals is exactly the cost of the next level, and the level of a total is
    // the level it was built from. Any slip in the piecewise constants fails.
    for(int level = 0; level < 60; ++level)
    {
        const int gap = totalXpForLevel(level + 1) - totalXpForLevel(level);
        CHECK(gap == xpForNextLevel(level));
        CHECK(levelFromTotalXp(totalXpForLevel(level)) == level);
        CHECK(levelFromTotalXp(totalXpForLevel(level + 1) - 1) == level);
    }

    // Monotonic, and safe on nonsense input.
    for(int xp = 0; xp < 4000; xp += 37)
        CHECK(levelFromTotalXp(xp + 37) >= levelFromTotalXp(xp));
    CHECK(levelFromTotalXp(0) == 0);
    CHECK(levelFromTotalXp(-100) == 0);

    // Progress is consistent with the curve the caller draws.
    for(int level = 0; level < 40; ++level)
    {
        const int total = totalXpForLevel(level);
        int out_level = -1;
        int remaining = -1;
        const float progress = levelProgress(total, out_level, remaining);
        CHECK(out_level == level);
        CHECK(near(progress, 0.0f));
        CHECK(remaining == xpForNextLevel(level));

        const float half = levelProgress(total + xpForNextLevel(level) / 2, out_level, remaining);
        CHECK(half > 0.0f && half <= 1.0f);

        // One point short of the next level is as close to full as the bar gets.
        const float end = levelProgress(total + xpForNextLevel(level) - 1, out_level, remaining);
        CHECK(end > 0.5f && end <= 1.0f);
        CHECK(remaining == 1);
    }

    int out_level = -1;
    int remaining = -1;
    CHECK(near(levelProgress(0, out_level, remaining), 0.0f));
    CHECK(out_level == 0);
    CHECK(remaining == 7);
}

// Respiration (the helmet's enchantment) does not make the air bar bigger; it
// makes some breaths free, so a diver stays down longer without the HUD showing
// more bubbles. The rule is the level over the level plus one.
static void test_respiration_breath()
{
    // Without the enchantment no breath is ever saved.
    for(uint32_t roll = 0; roll < 20; ++roll)
        CHECK(!respirationSavesBreath(0, roll));

    // With it, the share of saved breaths is level/(level+1): count a full cycle
    // of rolls per level and check the exact share.
    for(uint8_t level = 1; level <= 4; ++level)
    {
        const uint32_t span = static_cast<uint32_t>(level) + 1u;
        uint32_t saved = 0;
        for(uint32_t roll = 0; roll < span * 50u; ++roll)
            if(respirationSavesBreath(level, roll))
                ++saved;
        CHECK(saved == (span - 1u) * 50u);
    }

    // A deeper level never saves fewer breaths than a shallower one.
    for(uint32_t roll = 0; roll < 100; ++roll)
        CHECK(!(respirationSavesBreath(1, roll) && !respirationSavesBreath(3, roll)));
}

int main()
{
    test_food_table();
    test_hunger_state();
    test_exhaustion();
    test_regeneration_and_starvation();
    test_damage_sources();
    test_effect_tables();
    test_effect_durations_and_mappings();
    test_multipliers();
    test_experience();
    test_respiration_breath();

    printf("survival_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
