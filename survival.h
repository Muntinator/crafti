#ifndef SURVIVAL_H
#define SURVIVAL_H

#include <stdint.h>

#include "terrain.h"

/**
 * Survival rules: hunger, health regeneration, environmental damage, status
 * effects and experience.
 *
 * This is deliberately a pure rules module: it holds no engine state, touches no
 * hardware and includes nothing from nGL, so every threshold and curve can be
 * unit tested on a development host (tests/survival_test.cc). The caller owns
 * the mutable state (the player's hunger, air, fire ticks, effect list and XP)
 * and drives it through the helpers here.
 *
 * Numbers follow Minecraft where that keeps the game recognisable, and are
 * scaled down where the CX cannot afford the vanilla rates.
 */
namespace Survival
{
	// ---------------------------------------------------------------- hunger

	/** Hunger points, 0..20. One HUD drumstick is two points. */
	constexpr int MaxHunger = 20;
	/** Saturation can never exceed the current hunger. */
	constexpr float MaxSaturation = 10.0f;
	/** Exhaustion needed to consume one hunger (or saturation) point. */
	constexpr float ExhaustionPerPoint = 4.0f;

	/** Hit points at full health, and the HUD heart count (two points a heart). */
	constexpr int MaxHealth = 20;
	constexpr int HeartsPerHealthBar = MaxHealth / 2;

	/** A status effect a food applies on top of its hunger. */
	struct FoodEffect
	{
		uint8_t effect;
		uint8_t seconds;
		uint8_t amplifier; ///< 0 = level I
	};

	/** `effect` value meaning "this food applies no effect here". */
	constexpr uint8_t NoEffect = 0xFF;

	struct FoodValue
	{
		uint8_t item;      ///< BLOCK_ITEM metadata (ItemTexture id, must be < 128)
		uint8_t hunger;    ///< hunger points restored
		float saturation;  ///< saturation restored
		const char *name;
		FoodEffect effect; ///< { NoEffect, 0, 0 } when there is none
		FoodEffect extra;  ///< second effect, or { NoEffect, 0, 0 }
	};

	/** The food for an item id, or nullptr when it is not edible. */
	const FoodValue *foodValue(uint8_t item);
	bool isFood(uint8_t item);

	struct HungerState
	{
		int hunger = MaxHunger;
		float saturation = 0.0f;
		float exhaustion = 0.0f;
		/** Counts down to the next regeneration or starvation decision. */
		int timer = 0;
	};

	void resetHunger(HungerState &state);
	/** Applies a food, clamped to the caps. */
	void eat(HungerState &state, const FoodValue &food);
	/** Adds exhaustion; returns true when a hunger point was consumed. */
	bool addExhaustion(HungerState &state, float amount);
	/** True while the player is too full to eat. */
	bool canEat(const HungerState &state);

	// Exhaustion added by actions.
	constexpr float ExhaustionPerBlockWalked = 0.01f;
	constexpr float ExhaustionPerBlockSprinted = 0.10f;
	constexpr float ExhaustionPerJump = 0.05f;
	constexpr float ExhaustionPerAttack = 0.10f;
	constexpr float ExhaustionPerMinedBlock = 0.005f;

	/**
	 * Health regenerates when well fed. Returns the tick interval between one
	 * health point of regeneration, or 0 when health does not regenerate now.
	 */
	int regenerationIntervalTicks(const HungerState &state);
	/** Ticks between starvation damage events, or 0 when not starving. */
	int starvationIntervalTicks(const HungerState &state);
	constexpr int StarvationDamage = 1;
	/** Starvation never takes the player below this many hearts (1 = 1 hp). */
	constexpr int StarvationHealthFloor = 1;

	// ------------------------------------------------------------- damage

	enum class Damage : uint8_t
	{
		Fall = 0,
		Drown,
		Fire,
		Lava,
		Starve,
		Suffocate,
		Poison,
		Wither,
		Cactus,
		Void,
		Mob,
		Explosion,
		Magic,
		/**
		 * A lightning strike. Vanilla's lightning_bolt bypasses armour, and this one
		 * keeps that: it is the weather hitting you, not a blade. The strike also
		 * sets its victim alight, which is the caller's job (worldweathersnow.cpp).
		 */
		Lightning,
		Count
	};

	/** Damage per event for sources with a fixed amount. */
	int sourceDamage(Damage source);
	/** True for sources that cannot be reduced by armour or Resistance. */
	bool ignoresResistance(Damage source);
	/** True for sources that fire resistance blocks. */
	bool blockedByFireResistance(Damage source);

	/** Fall damage: no damage up to 3 blocks, then one point per block. */
	constexpr int SafeFallBlocks = 3;
	int fallDamage(int fall_blocks);

	/** Maximum breath, in ticks (15 s), and the tick interval of drown damage. */
	constexpr int MaxAir = 300;
	constexpr int DrownDamageInterval = 20;
	constexpr int DrownDamage = 2;
	/** Air restored per tick when the head is out of water. */
	constexpr int AirRecoveryPerTick = 4;

	/**
	 * Respiration (the helmet's enchantment) gives a chance of not spending a breath
	 * at all, so a diver with Respiration III keeps three breaths out of four and
	 * stays under water four times as long. `roll` is the caller's own random
	 * number; this rule only decides whether that roll saves the breath.
	 */
	bool respirationSavesBreath(uint8_t level, uint32_t roll);

	/** Suffocation (standing inside a block): interval and damage. */
	constexpr int SuffocateInterval = 10;
	constexpr int SuffocateDamage = 1;

	/** Burning: damage interval, damage, and how long lava sets you alight. */
	constexpr int FireDamageInterval = 20;
	constexpr int FireDamage = 1;
	constexpr int FireTicksFromLava = 300;
	constexpr int FireTicksFromFireBlock = 160;

	/** Contact damage from cactus and the void. */
	constexpr int CactusInterval = 10;
	constexpr int VoidDamage = 4;

	/** Damage of a direct lightning strike (vanilla: 5, five half-hearts). */
	constexpr int LightningDamage = 5;

	// ------------------------------------------------------ status effects

	enum Effect : uint8_t
	{
		Speed = 0,
		Slowness,
		Haste,
		MiningFatigue,
		Strength,
		Weakness,
		JumpBoost,
		Regeneration,
		Resistance,
		FireResistance,
		WaterBreathing,
		NightVision,
		Hunger,
		Poison,
		Wither,
		Absorption,
		Saturation,
		InstantHealth,
		InstantDamage,
		EffectCount
	};

	/**
	 * Vanilla effect ids, kept for the ones this build implements so that an
	 * effect written into a save file keeps the meaning it has elsewhere.
	 */
	enum VanillaEffectId
	{
		VanillaSpeed = 1,
		VanillaSlowness = 2,
		VanillaHaste = 3,
		VanillaMiningFatigue = 4,
		VanillaStrength = 5,
		VanillaJumpBoost = 8,
		VanillaRegeneration = 10,
		VanillaResistance = 11,
		VanillaFireResistance = 12,
		VanillaWaterBreathing = 13,
		VanillaNightVision = 16,
		VanillaHunger = 17,
		VanillaWeakness = 18,
		VanillaPoison = 19,
		VanillaWither = 20,
		VanillaAbsorption = 22,
		VanillaSaturation = 23
	};

	/** Simulation ticks in one second, used to convert seconds to durations. */
	constexpr int TicksPerSecond = 20;
	constexpr uint16_t secondsToTicks(int seconds)
	{
		return static_cast<uint16_t>(seconds > 0 ? seconds * TicksPerSecond : 0);
	}

	constexpr int MaxActiveEffects = 8;

	struct EffectInstance
	{
		uint8_t effect = 0;    ///< Effect
		uint16_t duration = 0; ///< remaining ticks
		uint8_t amplifier = 0; ///< 0 = level I
	};

	/**
	 * Translates between this build's Effect and the ids a save file (or any
	 * other tool) uses. `effectFromVanillaId` returns EffectCount for an id this
	 * build does not implement, and `effectToVanillaId` returns 0 for an effect
	 * that has no vanilla id.
	 */
	uint8_t effectFromVanillaId(int vanilla_id);
	int effectToVanillaId(uint8_t effect);

	const char *effectName(uint8_t effect);
	bool effectIsGood(uint8_t effect);
	/** Instant effect: applies once, then is not stored. */
	bool effectIsInstant(uint8_t effect);
	/** Duration in ticks for a level, or 0xFFFF for "infinite" (no decay). */
	uint16_t durationForLevel(int amplifier, int base_seconds);

	/** Ticks between damage events, or 0 when this effect does no damage. */
	int effectDamageInterval(uint8_t effect);
	int effectDamageAmount(uint8_t effect, uint8_t amplifier);
	/** True for effects that must not take the last heart (poison). */
	bool stopsAtOneHeart(uint8_t effect);

	/** Regeneration effect: tick interval between healing one point. */
	int regenerationIntervalForEffect(uint8_t amplifier);

	// Multipliers. All take the amplifier (0 = level I) and return a plain
	// factor so the caller can apply it to whatever it is computing.

	/** Movement speed factor (Speed 1.2 + 20% per level, Slowness 0.85 - 15%). */
	float movementMultiplier(uint8_t effect, uint8_t amplifier);
	/** Mining speed factor (Haste +20% per level up to +100%, MiningFatigue -). */
	float miningMultiplier(uint8_t effect, uint8_t amplifier);
	/** Attack damage bonus as a flat multiplier (Strength +1.3 per level). */
	float attackMultiplier(uint8_t effect, uint8_t amplifier);
	/** Incoming damage factor (Resistance -20% per level, capped at -80%). */
	float resistanceMultiplier(uint8_t amplifier);
	/** Jump velocity factor (JumpBoost +1 per level). */
	float jumpMultiplier(uint8_t amplifier);
	/** Extra absorption health granted by one Absorption level (4 per level). */
	int absorptionHealth(uint8_t amplifier);
	/** Hunger drain factor (Hunger +30% per level). */
	float hungerDrainMultiplier(uint8_t amplifier);

	/** Night vision lifts the sky light floor so caves stay readable. */
	int lightFloorFor(uint8_t effect);
	constexpr int NightVisionLightFloor = 150;

	// ------------------------------------------------------------------ xp

	struct XpRange
	{
		int min;
		int max;
	};

	/** Experience dropped by a block when mined; {0, 0} for none. */
	XpRange xpBlockRange(uint8_t block);
	/** Deterministic choice inside a range from a caller-supplied roll (0..99). */
	int xpRoll(const XpRange &range, uint32_t roll);

	enum class XpMob : uint8_t
	{
		Passive = 0,
		Hostile,
		Boss
	};
	int xpFromMob(XpMob mob);
	constexpr int XpFromSmelting = 1;
	constexpr int XpPerOreMined = 0; ///< ores use their own range instead

	/** Experience needed to go from `level` to `level + 1`. */
	int xpForNextLevel(int level);
	/** Total experience accumulated when standing at `level`. */
	int totalXpForLevel(int level);
	/** Highest level reached with `total_xp` accumulated experience. */
	int levelFromTotalXp(int total_xp);
	/**
	 * Progress toward the next level as 0..1. `out_level` receives the level and
	 * `out_remaining` the experience still needed for the next one.
	 */
	float levelProgress(int total_xp, int &out_level, int &out_remaining);
}

#endif // SURVIVAL_H
