#ifndef LIVESTOCKSPECIES_H
#define LIVESTOCKSPECIES_H

#include <stdint.h>

#include "mobmodel.h"

/**
 * Pure, dependency-free definition of the passive livestock table.
 *
 * This header includes nothing from the game engine on purpose: the biome and
 * spawn rules and the per-species stats have to be unit-testable on a
 * development host (tests/livestock_test.cc) without pulling in nGL or the
 * world. livestockentity.cpp maps the numeric codes here onto the real block
 * and item ids, and static_asserts pin those ids so they cannot silently drift.
 */
namespace Livestock
{
	enum class Species : uint8_t
	{
		Cow = 0,
		Pig,
		Sheep,
		Chicken,
		Horse,
		Wolf,
		Mooshroom,
		Donkey,
		Count
	};

	/** Biome buckets derived from the generated terrain (no dedicated biome map). */
	enum class Biome : uint8_t
	{
		Grassland = 0,
		Forest,
		Desert,
		Shore,
		Water,
		Count
	};

	/** Terrain surface family, mapped from BLOCK ids by the entity code. */
	enum class Surface : uint8_t
	{
		Grass = 0,
		Sand,
		Stone,
		Water,
		Other,
		Count
	};

	constexpr unsigned int SpeciesCount = static_cast<unsigned int>(Species::Count);
	constexpr unsigned int BiomeCount = static_cast<unsigned int>(Biome::Count);

	/** Same encoding as getBLOCKWDATA(BLOCK_ITEM, data). */
	constexpr uint16_t itemStack(uint8_t item)
	{
		return static_cast<uint16_t>((static_cast<uint16_t>(item) << 8) | 254u);
	}

	constexpr uint16_t blockStack(uint8_t block)
	{
		return static_cast<uint16_t>(block);
	}

	struct Stats
	{
		int16_t width;          ///< collision/render width in nGL units (BLOCK_SIZE = 128)
		int16_t height;         ///< collision height in nGL units
		int16_t health;         ///< hit points
		uint8_t render_scale;   ///< percent scale of the shared model
		uint16_t idle_interval; ///< ticks between idle sounds
		uint8_t wander_speed;   ///< horizontal speed in nGL units per tick
		uint8_t drop_count;     ///< number of valid drop entries (1..2)
		uint16_t drop_stack[2]; ///< encoded BLOCK_WDATA per drop
		uint8_t drop_min[2];
		uint8_t drop_max[2];
	};

	/**
	 * The vanilla model for a species: the box model the 1.17.1 client draws,
	 * transcribed into the tables mobmodel.h describes. The skins under
	 * textures/entity/ are the official ones, so these UV origins are what line
	 * them up.
	 *
	 * Chicken is deliberately absent: its model carries a fixed body rotation and
	 * flapping wings, which the livestock renderer already draws in its own code.
	 *
	 * Mooshroom and Donkey are skin (and stats) variants in vanilla too -- both
	 * reuse a model another species already transcribes -- so they return the cow
	 * and the horse model rather than a second copy of the same boxes.
	 */
	const Mob::MobModel &model(Species species);

	const Stats &stats(Species species);
	const char *name(Species species);

	/** Percent chance of spawning the species in the biome (row sums to <= 100). */
	unsigned int spawnWeight(Biome biome, Species species);

	/** Deterministically maps a 0..99 roll to a species, or Count for "nothing". */
	Species pickSpecies(uint32_t roll, Biome biome);

	Biome classify(bool near_water, bool has_trees, Surface surface);

	/** Maximum number of live entities for the build (smaller on the calculator). */
	unsigned int maxEntities();

	/** Entities further than this many blocks from the player are despawned. */
	unsigned int despawnDistanceBlocks();
}

#endif // LIVESTOCKSPECIES_H
