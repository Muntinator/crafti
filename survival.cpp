#include "survival.h"

#include "textures/items.h"

namespace Survival
{
	namespace
	{
		// A food that applies an effect carries it here rather than in a separate
		// table, so eating and its consequences cannot drift apart. The effects
		// follow vanilla (golden apple heals and shields, pufferfish poisons,
		// rotten flesh and raw chicken make you hungry, spider eye poisons).
		constexpr FoodEffect no_effect = { NoEffect, 0, 0 };
		constexpr FoodValue foods[] = {
			{ static_cast<uint8_t>(ItemTexture::APPLE), 4, 2.4f, "Apple", no_effect, no_effect },
			{ static_cast<uint8_t>(ItemTexture::GOLDEN_APPLE), 4, 9.6f, "Golden apple", { Regeneration, 5, 1 }, { Absorption, 120, 0 } },
			{ static_cast<uint8_t>(ItemTexture::DRIED_KELP), 1, 0.6f, "Dried kelp", no_effect, no_effect },
			{ static_cast<uint8_t>(ItemTexture::BREAD), 5, 6.0f, "Bread", no_effect, no_effect },
			{ static_cast<uint8_t>(ItemTexture::MUSHROOM_STEW), 6, 7.2f, "Mushroom stew", no_effect, no_effect },
			{ static_cast<uint8_t>(ItemTexture::RAW_PORKCHOP), 3, 1.8f, "Raw porkchop", no_effect, no_effect },
			{ static_cast<uint8_t>(ItemTexture::COOKED_PORKCHOP), 8, 12.8f, "Cooked porkchop", no_effect, no_effect },
			{ static_cast<uint8_t>(ItemTexture::RAW_COD), 2, 0.4f, "Raw cod", no_effect, no_effect },
			{ static_cast<uint8_t>(ItemTexture::COOKED_COD), 5, 6.0f, "Cooked cod", no_effect, no_effect },
			{ static_cast<uint8_t>(ItemTexture::RAW_SALMON), 2, 0.4f, "Raw salmon", no_effect, no_effect },
			{ static_cast<uint8_t>(ItemTexture::COOKIE), 2, 0.4f, "Cookie", no_effect, no_effect },
			{ static_cast<uint8_t>(ItemTexture::RAW_BEEF), 3, 1.8f, "Raw beef", no_effect, no_effect },
			{ static_cast<uint8_t>(ItemTexture::COOKED_BEEF), 8, 12.8f, "Steak", no_effect, no_effect },
			{ static_cast<uint8_t>(ItemTexture::BAKED_POTATO), 5, 6.0f, "Baked potato", no_effect, no_effect },
			{ static_cast<uint8_t>(ItemTexture::POTATO), 1, 0.6f, "Potato", no_effect, no_effect },
			{ static_cast<uint8_t>(ItemTexture::CARROT), 3, 3.6f, "Carrot", no_effect, no_effect },
			{ static_cast<uint8_t>(ItemTexture::RAW_CHICKEN), 2, 1.2f, "Raw chicken", { Hunger, 30, 0 }, no_effect },
			{ static_cast<uint8_t>(ItemTexture::COOKED_CHICKEN), 6, 7.2f, "Cooked chicken", no_effect, no_effect },
			{ static_cast<uint8_t>(ItemTexture::ROTTEN_FLESH), 4, 0.8f, "Rotten flesh", { Hunger, 30, 0 }, no_effect },
			{ static_cast<uint8_t>(ItemTexture::SPIDER_EYE), 2, 0.8f, "Spider eye", { Poison, 4, 0 }, no_effect },
			{ static_cast<uint8_t>(ItemTexture::PUFFERFISH), 1, 0.2f, "Pufferfish", { Poison, 60, 3 }, { Hunger, 15, 2 } }
		};
		constexpr int food_count = static_cast<int>(sizeof(foods) / sizeof(foods[0]));

		// The ids below are compared against the id the caller reads out of a held
		// stack, which must come from getITEMDATA() -- that keeps the full byte,
		// while getBLOCKDATA() would truncate the two foods past 127 (spider eye,
		// rotten flesh) into a different item. This walks the table so that two
		// foods sharing an id (one of which would then be unreachable) cannot be
		// added by accident.
		constexpr bool noClashWith(int index, int other)
		{
			return other >= food_count ? true
				: (foods[index].item != foods[other].item && noClashWith(index, other + 1));
		}
		constexpr bool foodIdsDistinct(int index)
		{
			return index >= food_count ? true
				: (noClashWith(index, index + 1) && foodIdsDistinct(index + 1));
		}
		static_assert(foodIdsDistinct(0), "each food must have its own item id");

		// An effect id that is not one of ours would make the caller index its
		// effect table out of bounds, so the food table is checked here instead.
		constexpr bool effectIdOk(uint8_t effect)
		{
			return effect == NoEffect || effect < EffectCount;
		}
		constexpr bool foodEffectsFit(int index)
		{
			return index >= food_count ? true
				: (effectIdOk(foods[index].effect.effect)
				   && effectIdOk(foods[index].extra.effect)
				   && foodEffectsFit(index + 1));
		}
		static_assert(foodEffectsFit(0), "food effects must be real effects");

		const char *const effect_names[EffectCount] = {
			"Speed",
			"Slowness",
			"Haste",
			"Mining fatigue",
			"Strength",
			"Weakness",
			"Jump boost",
			"Regeneration",
			"Resistance",
			"Fire resistance",
			"Water breathing",
			"Night vision",
			"Hunger",
			"Poison",
			"Wither",
			"Absorption",
			"Saturation",
			"Instant health",
			"Instant damage"
		};

		// Only the effects this build actually implements have a vanilla id, so a
		// save file round-trips exactly and an unknown id is rejected rather than
		// silently becoming effect 0.
		struct VanillaEffectMapping
		{
			uint8_t effect;
			int vanilla;
		};
		constexpr VanillaEffectMapping vanilla_map[] = {
			{ Speed, VanillaSpeed },
			{ Slowness, VanillaSlowness },
			{ Haste, VanillaHaste },
			{ MiningFatigue, VanillaMiningFatigue },
			{ Strength, VanillaStrength },
			{ Weakness, VanillaWeakness },
			{ JumpBoost, VanillaJumpBoost },
			{ Regeneration, VanillaRegeneration },
			{ Resistance, VanillaResistance },
			{ FireResistance, VanillaFireResistance },
			{ WaterBreathing, VanillaWaterBreathing },
			{ NightVision, VanillaNightVision },
			{ Hunger, VanillaHunger },
			{ Poison, VanillaPoison },
			{ Wither, VanillaWither },
			{ Absorption, VanillaAbsorption },
			{ Saturation, VanillaSaturation }
		};
		constexpr int vanilla_map_count = static_cast<int>(sizeof(vanilla_map) / sizeof(vanilla_map[0]));

		const bool effect_is_good[EffectCount] = {
			true,  // Speed
			false, // Slowness
			true,  // Haste
			false, // Mining fatigue
			true,  // Strength
			false, // Weakness
			true,  // Jump boost
			true,  // Regeneration
			true,  // Resistance
			true,  // Fire resistance
			true,  // Water breathing
			true,  // Night vision
			false, // Hunger
			false, // Poison
			false, // Wither
			true,  // Absorption
			true,  // Saturation
			true,  // Instant health
			false  // Instant damage
		};

		float clampf(float value, float low, float high)
		{
			if(value < low)
				return low;
			if(value > high)
				return high;
			return value;
		}

		/** Level I is the amplifier 0 entry, so the multiplier is per level. */
		int levels(uint8_t amplifier) { return static_cast<int>(amplifier) + 1; }
	}

	// ---------------------------------------------------------------- hunger

	const FoodValue *foodValue(uint8_t item)
	{
		for(int i = 0; i < food_count; ++i)
			if(foods[i].item == item)
				return &foods[i];
		return nullptr;
	}

	bool isFood(uint8_t item) { return foodValue(item) != nullptr; }

	void resetHunger(HungerState &state)
	{
		state.hunger = MaxHunger;
		state.saturation = 0.0f;
		state.exhaustion = 0.0f;
		state.timer = 0;
	}

	void eat(HungerState &state, const FoodValue &food)
	{
		state.hunger += food.hunger;
		if(state.hunger > MaxHunger)
			state.hunger = MaxHunger;

		state.saturation += food.saturation;
		const float cap = state.hunger < static_cast<int>(MaxSaturation) ? static_cast<float>(state.hunger) : MaxSaturation;
		if(state.saturation > cap)
			state.saturation = cap;
	}

	bool canEat(const HungerState &state)
	{
		// Edible unless both hunger and saturation are full, which is vanilla's
		// rule; eating below full is always allowed here.
		if(state.hunger >= MaxHunger && state.saturation > 0.0f)
			return false;
		return true;
	}

	bool addExhaustion(HungerState &state, float amount)
	{
		if(amount <= 0.0f)
			return false;

		state.exhaustion += amount;
		if(state.exhaustion < ExhaustionPerPoint)
			return false;

		state.exhaustion -= ExhaustionPerPoint;
		if(state.saturation > 0.0f)
		{
			state.saturation -= 1.0f;
			if(state.saturation < 0.0f)
				state.saturation = 0.0f;
			return false;
		}

		if(state.hunger > 0)
		{
			--state.hunger;
			return true;
		}
		return false;
	}

	int regenerationIntervalTicks(const HungerState &state)
	{
		if(state.hunger < 18)
			return 0;
		// With saturation to burn it heals fast, without it slowly.
		return state.saturation > 0.0f ? 10 : 80;
	}

	int starvationIntervalTicks(const HungerState &state)
	{
		return state.hunger <= 0 ? 80 : 0;
	}

	// ------------------------------------------------------------- damage

	int sourceDamage(Damage source)
	{
		switch(source)
		{
		case Damage::Drown: return DrownDamage;
		case Damage::Fire: return FireDamage;
		case Damage::Lava: return 4;
		case Damage::Starve: return StarvationDamage;
		case Damage::Suffocate: return SuffocateDamage;
		case Damage::Cactus: return 1;
		case Damage::Void: return VoidDamage;
		case Damage::Poison: return 1;
		case Damage::Wither: return 1;
		default: return 0;
		}
	}

	bool ignoresResistance(Damage source)
	{
		// Vanilla: starving, drowning, the void, poison and magic bypass armour.
		return source == Damage::Starve || source == Damage::Drown || source == Damage::Void
			|| source == Damage::Poison || source == Damage::Wither || source == Damage::Magic;
	}

	bool blockedByFireResistance(Damage source)
	{
		return source == Damage::Fire || source == Damage::Lava;
	}

	int fallDamage(int fall_blocks)
	{
		if(fall_blocks <= SafeFallBlocks)
			return 0;
		return fall_blocks - SafeFallBlocks;
	}

	// ------------------------------------------------------ status effects

	uint8_t effectFromVanillaId(int vanilla_id)
	{
		for(int i = 0; i < vanilla_map_count; ++i)
			if(vanilla_map[i].vanilla == vanilla_id)
				return vanilla_map[i].effect;
		return EffectCount;
	}

	int effectToVanillaId(uint8_t effect)
	{
		for(int i = 0; i < vanilla_map_count; ++i)
			if(vanilla_map[i].effect == effect)
				return vanilla_map[i].vanilla;
		return 0;
	}

	const char *effectName(uint8_t effect)
	{
		return effect < EffectCount ? effect_names[effect] : "Unknown";
	}

	bool effectIsGood(uint8_t effect)
	{
		return effect < EffectCount ? effect_is_good[effect] : false;
	}

	bool effectIsInstant(uint8_t effect)
	{
		return effect == InstantHealth || effect == InstantDamage;
	}

	uint16_t durationForLevel(int amplifier, int base_seconds)
	{
		// Vanilla halves nothing; it roughly doubles the duration per level for
		// instant-ish effects and keeps it flat otherwise. Keeping it flat except
		// for a modest bonus keeps the numbers predictable.
		if(amplifier < 0)
			amplifier = 0;
		const int seconds = base_seconds * (amplifier + 1);
		if(seconds <= 0)
			return 0;
		const int ticks = seconds * TicksPerSecond;
		return ticks > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(ticks);
	}

	int effectDamageInterval(uint8_t effect)
	{
		switch(effect)
		{
		case Poison: return 25;
		case Wither: return 40;
		default: return 0;
		}
	}

	int effectDamageAmount(uint8_t effect, uint8_t /*amplifier*/)
	{
		// Both poison and wither do a flat point per damage tick; the level scales
		// how long they last (and how often, for regeneration), not the amount.
		switch(effect)
		{
		case Poison:
		case Wither:
			return 1;
		default:
			return 0;
		}
	}

	bool stopsAtOneHeart(uint8_t effect)
	{
		// Vanilla poison can never kill, so it must not be allowed to take the
		// last heart; wither is not capped.
		return effect == Poison;
	}

	int regenerationIntervalForEffect(uint8_t amplifier)
	{
		// 50 ticks at level I, halving per level, floored at 10.
		int interval = 50 >> (amplifier > 3 ? 3 : amplifier);
		if(interval < 10)
			interval = 10;
		return interval;
	}

	float movementMultiplier(uint8_t effect, uint8_t amplifier)
	{
		const int level = levels(amplifier);
		switch(effect)
		{
		case Speed:
		{
			const float factor = 1.0f + 0.2f * static_cast<float>(level);
			return factor > 2.0f ? 2.0f : factor;
		}
		case Slowness:
		{
			const float factor = 1.0f - 0.15f * static_cast<float>(level);
			return factor < 0.15f ? 0.15f : factor;
		}
		default:
			return 1.0f;
		}
	}

	float miningMultiplier(uint8_t effect, uint8_t amplifier)
	{
		const int level = levels(amplifier);
		switch(effect)
		{
		case Haste:
		{
			const float factor = 1.0f + 0.2f * static_cast<float>(level);
			return factor > 2.0f ? 2.0f : factor;
		}
		case MiningFatigue:
		{
			const float factor = 1.0f / (1.0f + 0.3f * static_cast<float>(level));
			return factor < 0.1f ? 0.1f : factor;
		}
		default:
			return 1.0f;
		}
	}

	float attackMultiplier(uint8_t effect, uint8_t amplifier)
	{
		const int level = levels(amplifier);
		switch(effect)
		{
		case Strength:
			return 1.0f + 0.3f * static_cast<float>(level);
		case Weakness:
		{
			const float factor = 1.0f - 0.2f * static_cast<float>(level);
			return factor < 0.1f ? 0.1f : factor;
		}
		default:
			return 1.0f;
		}
	}

	float resistanceMultiplier(uint8_t amplifier)
	{
		const float factor = 1.0f - 0.2f * static_cast<float>(levels(amplifier));
		return factor < 0.2f ? 0.2f : factor;
	}

	float jumpMultiplier(uint8_t amplifier)
	{
		return 1.0f + 0.5f * static_cast<float>(levels(amplifier));
	}

	int absorptionHealth(uint8_t amplifier)
	{
		return 4 * levels(amplifier);
	}

	float hungerDrainMultiplier(uint8_t amplifier)
	{
		return 1.0f + 0.3f * static_cast<float>(levels(amplifier));
	}

	int lightFloorFor(uint8_t effect)
	{
		return effect == NightVision ? NightVisionLightFloor : 0;
	}

	// ------------------------------------------------------------------ xp

	XpRange xpBlockRange(uint8_t block)
	{
		switch(block)
		{
		case BLOCK_COAL_ORE: return XpRange{ 0, 2 };
		case BLOCK_REDSTONE_ORE: return XpRange{ 1, 5 };
		case BLOCK_GOLD_ORE: return XpRange{ 0, 2 };
		case BLOCK_DIAMOND_ORE: return XpRange{ 3, 7 };
		// Iron ore, stone and everything else give no experience.
		default: return XpRange{ 0, 0 };
		}
	}

	int xpRoll(const XpRange &range, uint32_t roll)
	{
		if(range.max <= range.min)
			return range.min;
		const uint32_t span = static_cast<uint32_t>(range.max - range.min + 1);
		return range.min + static_cast<int>(roll % span);
	}

	int xpFromMob(XpMob mob)
	{
		switch(mob)
		{
		case XpMob::Hostile: return 5;
		case XpMob::Boss: return 50;
		default: return 2; // passive animals
		}
	}

	int xpForNextLevel(int level)
	{
		if(level < 0)
			level = 0;
		if(level >= 30)
			return 112 + (level - 30) * 9;
		if(level >= 15)
			return 37 + (level - 15) * 5;
		return 7 + level * 2;
	}

	int totalXpForLevel(int level)
	{
		if(level <= 0)
			return 0;
		if(level <= 16)
			return level * level + 6 * level;
		if(level <= 31)
			return static_cast<int>(2.5f * static_cast<float>(level) * static_cast<float>(level)
				- 40.5f * static_cast<float>(level) + 360.0f);
		return static_cast<int>(4.5f * static_cast<float>(level) * static_cast<float>(level)
			- 162.5f * static_cast<float>(level) + 2220.0f);
	}

	int levelFromTotalXp(int total_xp)
	{
		if(total_xp <= 0)
			return 0;

		int level = 0;
		// The curve grows quadratically, so this walk stays short even for a
		// long-lived world; the cap keeps a corrupt save from looping forever.
		while(level < 200 && total_xp >= totalXpForLevel(level + 1))
			++level;
		return level;
	}

	float levelProgress(int total_xp, int &out_level, int &out_remaining)
	{
		const int level = levelFromTotalXp(total_xp);
		const int base = totalXpForLevel(level);
		const int needed = xpForNextLevel(level);

		out_level = level;
		const int into = total_xp - base;
		out_remaining = needed - into;
		if(out_remaining < 0)
			out_remaining = 0;

		return needed > 0 ? clampf(static_cast<float>(into) / static_cast<float>(needed), 0.0f, 1.0f) : 0.0f;
	}
}
