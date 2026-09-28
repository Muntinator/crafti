#include "livestockspecies.h"

#include "terrain.h"
#include "textures/items.h"

// The numeric codes in the pure header must stay pinned to the real ids.
static_assert(BLOCK_ITEM == 254, "Livestock::itemStack assumes BLOCK_ITEM == 254");
static_assert(BLOCK_GRASS == 20, "Surface mapping assumes BLOCK_GRASS == 20");
static_assert(BLOCK_SAND == 3, "Surface mapping assumes BLOCK_SAND == 3");
static_assert(BLOCK_STONE == 1, "Surface mapping assumes BLOCK_STONE == 1");
static_assert(BLOCK_WOOL_WHITE == 37, "Sheep drop assumes BLOCK_WOOL_WHITE == 37");

namespace Livestock
{
	namespace
	{
		constexpr uint16_t item(uint8_t id) { return itemStack(id); }

		const Stats stats_table[SpeciesCount] = {
			// Cow: beef and leather.
			{ 96, 205, 10, 100, 300, 10, 2,
			  { item(static_cast<uint8_t>(ItemTexture::RAW_BEEF)), item(static_cast<uint8_t>(ItemTexture::LEATHER)) },
			  { 1, 0 }, { 3, 2 } },
			// Pig: porkchops.
			{ 80, 165, 10, 80, 280, 12, 1,
			  { item(static_cast<uint8_t>(ItemTexture::RAW_PORKCHOP)), 0 },
			  { 1, 0 }, { 3, 0 } },
			// Sheep: wool.
			{ 90, 185, 8, 90, 320, 8, 1,
			  { blockStack(BLOCK_WOOL_WHITE), 0 },
			  { 1, 0 }, { 1, 0 } },
			// Chicken: raw chicken.
			{ 52, 84, 4, 100, 320, 8, 1,
			  { item(static_cast<uint8_t>(ItemTexture::RAW_CHICKEN)), 0 },
			  { 1, 0 }, { 1, 0 } },
			// Horse: leather.
			{ 115, 250, 15, 120, 340, 14, 1,
			  { item(static_cast<uint8_t>(ItemTexture::LEATHER)), 0 },
			  { 1, 0 }, { 2, 0 } },
		};

		const char *const names[SpeciesCount] = { "Cow", "Pig", "Sheep", "Chicken", "Horse" };

		// Percent weights per biome. Rows sum to <= 100, so a roll above the sum
		// spawns nothing (deserts and water stay mostly empty).
		const uint8_t weights[BiomeCount][SpeciesCount] = {
			// Grassland
			{ 25, 20, 20, 25, 10 },
			// Forest
			{ 15, 10, 30, 35, 10 },
			// Desert
			{  0,  0, 10, 20,  0 },
			// Shore
			{ 10, 10, 10, 60, 10 },
			// Water
			{  0,  0,  0,  0,  0 },
		};

		const QuadrupedModel quad_model = {
			64, 64,
			0, 0, 8, 8, 8,      // head
			0, 16, 12, 10, 18,  // body
			0, 48, 4, 12, 4,    // leg
			32, 0, 4, 4, 4,     // detail (ears/horns)
		};
	}

	const QuadrupedModel &quadrupedModel() { return quad_model; }

	const Stats &stats(Species species)
	{
		const unsigned int index = static_cast<unsigned int>(species);
		return stats_table[index < SpeciesCount ? index : 0];
	}

	const char *name(Species species)
	{
		const unsigned int index = static_cast<unsigned int>(species);
		return names[index < SpeciesCount ? index : 0];
	}

	unsigned int spawnWeight(Biome biome, Species species)
	{
		const unsigned int b = static_cast<unsigned int>(biome);
		const unsigned int s = static_cast<unsigned int>(species);
		if(b >= BiomeCount || s >= SpeciesCount)
			return 0;
		return weights[b][s];
	}

	Species pickSpecies(uint32_t roll, Biome biome)
	{
		if(static_cast<unsigned int>(biome) >= BiomeCount)
			return Species::Count;

		roll %= 100u;
		uint32_t accumulated = 0;
		for(unsigned int i = 0; i < SpeciesCount; ++i)
		{
			accumulated += weights[static_cast<unsigned int>(biome)][i];
			if(roll < accumulated)
				return static_cast<Species>(i);
		}
		return Species::Count;
	}

	Biome classify(bool near_water, bool has_trees, Surface surface)
	{
		if(surface == Surface::Water)
			return Biome::Water;
		if(near_water)
			return Biome::Shore;
		if(surface == Surface::Sand)
			return Biome::Desert;
		if(has_trees && surface == Surface::Grass)
			return Biome::Forest;
		// Grass, bare stone and anything unusual fall back to grassland weights.
		return Biome::Grassland;
	}

	unsigned int maxEntities()
	{
#ifdef _TINSPIRE
		// The CX pays for every entity twice (logic + geometry), so keep the
		// population small; the spawner respects this cap and despawns far mobs.
		return 10;
#else
		return 24;
#endif
	}

	unsigned int despawnDistanceBlocks()
	{
		return 56;
	}
}
