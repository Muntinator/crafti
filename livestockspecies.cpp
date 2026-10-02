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
			// Wolf: a vanilla wolf drops nothing at all, so its pelt is dropped as
			// leather, which is what the horse and the donkey do as well.
			{ 76, 108, 8, 100, 340, 14, 1,
			  { item(static_cast<uint8_t>(ItemTexture::LEATHER)), 0 },
			  { 1, 0 }, { 2, 0 } },
			// Mooshroom: the cow it is a variant of, beef and leather alike.
			{ 96, 205, 10, 100, 320, 10, 2,
			  { item(static_cast<uint8_t>(ItemTexture::RAW_BEEF)), item(static_cast<uint8_t>(ItemTexture::LEATHER)) },
			  { 1, 0 }, { 3, 2 } },
			// Donkey: leather, like the horse whose model it shares.
			{ 108, 235, 15, 110, 340, 12, 1,
			  { item(static_cast<uint8_t>(ItemTexture::LEATHER)), 0 },
			  { 1, 0 }, { 2, 0 } },
		};

		const char *const names[SpeciesCount] =
			{ "Cow", "Pig", "Sheep", "Chicken", "Horse", "Wolf", "Mooshroom", "Donkey" };

		// Percent weights per biome. Rows sum to <= 100, so a roll above the sum
		// spawns nothing (deserts and water stay mostly empty).
		// Order matches Species: cow, pig, sheep, chicken, horse, wolf, mooshroom,
		// donkey. Wolves only turn up where there is something to hunt, so they are
		// weighted towards the forests and never spawn on sand; mooshrooms are the
		// forest/grassland analogue of the cow they are a variant of.
		const uint8_t weights[BiomeCount][SpeciesCount] = {
			// Grassland
			{ 20, 15, 15, 20, 10, 10,  5,  5 },
			// Forest
			{ 10, 10, 20, 25, 10, 20,  5,  0 },
			// Desert
			{  0,  0, 10, 20,  0,  0,  0,  0 },
			// Shore
			{ 10, 10, 10, 50, 10,  5,  0,  0 },
			// Water
			{  0,  0,  0,  0,  0,  0,  0,  0 },
		};

		// --- the vanilla models, transcribed ---------------------------------
		//
		// The 1.17.1 client's box models. A part is one ModelRenderer (its rotation
		// point and fixed rotation) and each box is one addBox(), so a line here can
		// be read against the original class. The texture origins are the official
		// ones, which is what makes the skins under textures/entity/ line up.

		// ModelCow: ModelQuadruped(12) with the cow's head, body and horns.
		const Mob::MobBox cow_boxes[] = {
			{  0,  0,  8,  8,  6, -4, -4, -6 }, // head
			{ 22,  0,  1,  3,  1, -5, -5, -4 }, // left horn
			{ 22,  0,  1,  3,  1,  4, -5, -4 }, // right horn
			{ 18,  4, 12, 18, 10, -6, -10, -7 }, // body
			{ 52,  0,  4,  6,  1, -2,   2, -8 }, // udder
			{  0, 16,  4, 12,  4, -2,   0, -2 }, // leg
			{  0, 16,  4, 12,  4, -2,   0, -2 },
			{  0, 16,  4, 12,  4, -2,   0, -2 },
			{  0, 16,  4, 12,  4, -2,   0, -2 },
		};
		const Mob::MobPart cow_parts[] = {
			{  0,  4,  -8, 0,  0, 3, Mob::Pose::Head },
			{  0,  5,   2, 90, 3, 2, Mob::Pose::Static },
			{ -4, 12,   7, 0,  5, 1, Mob::Pose::LegFrontLeft },
			{  4, 12,   7, 0,  6, 1, Mob::Pose::LegFrontRight },
			{ -4, 12,  -6, 0,  7, 1, Mob::Pose::LegBackLeft },
			{  4, 12,  -6, 0,  8, 1, Mob::Pose::LegBackRight },
		};
		const Mob::MobModel cow_model = { 64, 32, 6, cow_parts, 9, cow_boxes };

		// ModelPig: ModelQuadruped(6) plus the snout.
		const Mob::MobBox pig_boxes[] = {
			{  0,  0,  8,  8, 8, -4,  -4, -8 }, // head
			{ 16, 16,  4,  3, 1, -2,   0, -9 }, // snout
			{ 28,  8, 10, 16, 8, -5, -10, -7 }, // body
			{  0, 16,  4,  6, 4, -2,   0, -2 }, // leg
			{  0, 16,  4,  6, 4, -2,   0, -2 },
			{  0, 16,  4,  6, 4, -2,   0, -2 },
			{  0, 16,  4,  6, 4, -2,   0, -2 },
		};
		const Mob::MobPart pig_parts[] = {
			{ 0, 12, -6, 0, 0, 2, Mob::Pose::Head },
			{ 0, 11,  2, 90, 2, 1, Mob::Pose::Static },
			{ -3, 18,  7, 0, 3, 1, Mob::Pose::LegFrontLeft },
			{  3, 18,  7, 0, 4, 1, Mob::Pose::LegFrontRight },
			{ -3, 18, -5, 0, 5, 1, Mob::Pose::LegBackLeft },
			{  3, 18, -5, 0, 6, 1, Mob::Pose::LegBackRight },
		};
		const Mob::MobModel pig_model = { 64, 32, 6, pig_parts, 7, pig_boxes };

		// ModelSheep1: the narrow wool body on ModelQuadruped(12).
		const Mob::MobBox sheep_boxes[] = {
			{  0,  0, 6,  6, 6, -3,  -4, -4 }, // head
			{ 28,  8, 8, 16, 6, -4, -10, -7 }, // body
			{  0, 16, 4,  6, 4, -2,   0, -2 }, // leg
			{  0, 16, 4,  6, 4, -2,   0, -2 },
			{  0, 16, 4,  6, 4, -2,   0, -2 },
			{  0, 16, 4,  6, 4, -2,   0, -2 },
		};
		const Mob::MobPart sheep_parts[] = {
			// ModelSheep1 keeps the base quadruped's pivots but gives the sheep
			// 6-unit legs instead of 12. Measured from the ground that leaves the
			// feet hanging, so all four pivots drop the extra 6 units and the whole
			// animal stands as one piece again.
			{ 0, 12, -8, 0, 0, 1, Mob::Pose::Head },
			{ 0, 11,  2, 90, 1, 1, Mob::Pose::Static },
			{ -3, 18,  7, 0, 2, 1, Mob::Pose::LegFrontLeft },
			{  3, 18,  7, 0, 3, 1, Mob::Pose::LegFrontRight },
			{ -3, 18, -5, 0, 4, 1, Mob::Pose::LegBackLeft },
			{  3, 18, -5, 0, 5, 1, Mob::Pose::LegBackRight },
		};
		const Mob::MobModel sheep_model = { 64, 32, 6, sheep_parts, 6, sheep_boxes };

		// ModelHorse (the 1.13+ one, 64x64). The neck, head, mane, ears and tail
		// share the 30 degrees the vanilla model gives them.
		const Mob::MobBox horse_boxes[] = {
			{  0, 32, 10, 10, 22, -5, -8, -11 }, // body
			{  0, 35,  4, 12,  7, -2, -11, -3 }, // neck
			{  0, 13,  6,  5,  7, -3,  -5,   0 }, // head
			{  0, 25,  4,  5,  5, -2,  -5,  -5 }, // muzzle
			{ 19, 16,  2,  3,  1, -1, -18,   3 }, // left ear
			{ 19, 16,  2,  3,  1, -2, -18,   3 }, // right ear
			{ 56, 36,  2, 16,  2, -1, -16,   4 }, // mane
			{ 42, 36,  3, 14,  4, -2,   0,  -2 }, // tail
			{ 48, 21,  4, 11,  4, -2,   0,  -2 }, // leg
			{ 48, 21,  4, 11,  4, -2,   0,  -2 },
			{ 48, 21,  4, 11,  4, -2,   0,  -2 },
			{ 48, 21,  4, 11,  4, -2,   0,  -2 },
		};
		const Mob::MobPart horse_parts[] = {
			{  0, 11,   9,  0,  0, 1, Mob::Pose::Static },
			{  0,  7,  -8, 30,  1, 1, Mob::Pose::Static },
			{  0, -4, -11, 30,  2, 2, Mob::Pose::Head },
			{  0,  7,  -8, 30,  4, 1, Mob::Pose::Head },
			{  0,  7,  -8, 30,  5, 1, Mob::Pose::Head },
			{  0,  7,  -8, 30,  6, 1, Mob::Pose::Static },
			{  0,  4,  11, 30,  7, 1, Mob::Pose::Static },
			{  3, 13,   9,  0,  8, 1, Mob::Pose::LegFrontLeft },
			{ -3, 13,   9,  0,  9, 1, Mob::Pose::LegFrontRight },
			{  3, 13,  -9,  0, 10, 1, Mob::Pose::LegBackLeft },
			{ -3, 13,  -9,  0, 11, 1, Mob::Pose::LegBackRight },
		};
		const Mob::MobModel horse_model = { 64, 64, 11, horse_parts, 12, horse_boxes };

		// ModelWolf (64x32). Vanilla puts the head half a unit off the integer grid
		// (y = 13.5) and the legs at x = -2.5 / 0.5, because the whole animal is
		// built half a unit to the left of the origin. The tables hold whole model
		// units, so the head drops the half and the legs move to -3 / 1, which is
		// still symmetric about the same x = -1 the vanilla offsets use.
		const Mob::MobBox wolf_boxes[] = {
			{  0,  0, 6, 6, 4, -3, -3, -2 }, // head
			{ 16, 14, 2, 2, 1, -2, -5,  0 }, // left ear
			{ 16, 14, 2, 2, 1,  0, -5,  0 }, // right ear
			{  0, 10, 3, 3, 4, -2,  0, -5 }, // muzzle
			{ 18, 14, 6, 9, 6, -4, -2, -3 }, // body
			{ 21,  0, 8, 6, 7, -4, -3, -3 }, // mane
			{  0, 18, 2, 8, 2, -1,  0, -1 }, // leg
			{  0, 18, 2, 8, 2, -1,  0, -1 },
			{  0, 18, 2, 8, 2, -1,  0, -1 },
			{  0, 18, 2, 8, 2, -1,  0, -1 },
			{  9, 18, 2, 8, 2, -1,  0, -1 }, // tail
		};
		const Mob::MobPart wolf_parts[] = {
			// The boxes are in the vanilla order, so their u/v origins are the ones
			// the official wolf.png paints them at.
			{ -1, 14,  -7, 0,  0, 4, Mob::Pose::Head },   // head, ears and muzzle
			{  0, 14,   2, 0,  4, 1, Mob::Pose::Static }, // body
			{ -1, 14,   2, 0,  5, 1, Mob::Pose::Static }, // mane
			{ -3, 16,  -4, 0,  6, 1, Mob::Pose::LegFrontLeft },
			{  1, 16,  -4, 0,  7, 1, Mob::Pose::LegFrontRight },
			{ -3, 16,   7, 0,  8, 1, Mob::Pose::LegBackLeft },
			{  1, 16,   7, 0,  9, 1, Mob::Pose::LegBackRight },
			{ -1, 12,   8, 0, 10, 1, Mob::Pose::Static }, // tail
		};
		const Mob::MobModel wolf_model = { 64, 32, 8, wolf_parts, 11, wolf_boxes };

		const Mob::MobModel no_model = { 0, 0, 0, nullptr, 0, nullptr };
	}

	const Mob::MobModel &model(Species species)
	{
		switch(species)
		{
		case Species::Cow: return cow_model;
		case Species::Pig: return pig_model;
		case Species::Sheep: return sheep_model;
		case Species::Horse: return horse_model;
		case Species::Wolf: return wolf_model;
		// A mooshroom is a cow and a donkey is a horse: both draw the boxes the
		// table already holds instead of a second copy of the same numbers.
		case Species::Mooshroom: return cow_model;
		case Species::Donkey: return horse_model;
		default: return no_model; // Chicken draws itself
		}
	}

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
