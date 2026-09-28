#ifndef VILLAGEGEN_H
#define VILLAGEGEN_H

#include <stdint.h>

/**
 * Deterministic, dependency-free procedural village generation.
 *
 * The world is divided into a grid of square cells of CellBlocks. A cell
 * deterministically either hosts one village or nothing, and a village's
 * origin, ground level and full layout are all a pure function of the world
 * seed and the cell coordinates. That makes generation:
 *
 *  - deterministic: the same seed always produces the same villages,
 *  - order independent: chunks can stream in any order,
 *  - progressive: every chunk emits only the village blocks that fall inside
 *    itself, so no cross-chunk bookkeeping or pending block changes are needed,
 *  - save-friendly: the blocks live in the normal chunk data.
 *
 * This header deliberately includes nothing from the engine so the layout and
 * emission can be unit-tested on a host (tests/villagegen_test.cc). The caller
 * supplies the terrain heights it sampled, and terrain.h block ids are passed
 * in as the numeric codes documented in villagegen.cpp.
 */
namespace Village
{
	enum Frequency
	{
		FrequencyOff = 0,
		FrequencyRare,
		FrequencyNormal,
		FrequencyCommon,
		FrequencyCount
	};

	/** Grid spacing of village cells, in blocks. */
	constexpr int CellBlocks = 48;
	/** Blocks per chunk edge; must match Chunk::SIZE (chunk.cpp static_asserts). */
	constexpr int ChunkBlocks = 8;
	/** Maximum offset of a village centre from its cell centre, in blocks. */
	constexpr int CellJitter = 6;
	/** How many candidate origins are tried inside a cell before giving up. */
	constexpr int CandidateCount = 4;
	/** Village half-extent in blocks (a village spans 2*Radius + roof overhang). */
	constexpr int Radius = 16;
	/** Highest structure height above the ground block (roof ridge). */
	constexpr int MaxHeightAboveGround = 5;
	/** Deepest foundation block below the ground (covers the flatness tolerance). */
	constexpr int FoundationDepth = 3;

	/** Reject a site whose sampled terrain spreads wider than this. */
	constexpr int FlatnessTolerance = 3;
	/** Ground must be above sea level (12) so villages never sit on beaches. */
	constexpr int MinGroundY = 14;
	/** Leave headroom for roofs under the world ceiling. */
	constexpr int MaxGroundY = 33;

	struct Plan
	{
		bool valid = false;
		int cell_x = 0, cell_z = 0;
		int origin_x = 0, origin_z = 0; ///< world block coords of the village centre
		int ground_y = 0;               ///< block Y of the terrain surface at the centre
		uint32_t seed = 0;              ///< layout seed derived from the world seed
	};

	/** Global generation frequency (Off/Rare/Normal/Common), set from settings. */
	void setFrequency(int frequency);
	int frequency();

	/** Deterministic mixture of the world seed and a cell coordinate. */
	uint32_t hashSeed(uint32_t world_seed, int cell_x, int cell_z, uint32_t salt);

	/** True when this cell hosts a village at the given frequency. */
	bool cellHasVillage(uint32_t world_seed, int cell_x, int cell_z, int frequency);

	/** Origin of candidate `index` (0..CandidateCount-1) inside a cell. */
	void cellCandidate(uint32_t world_seed, int cell_x, int cell_z, int index, int &out_x, int &out_z);

	/** Builds the plan for a validated candidate. Always succeeds. */
	bool planVillage(uint32_t world_seed, int cell_x, int cell_z, int candidate, int ground_y, Plan &out);

	/** Receives one placed block, in world block coordinates. */
	typedef void (*SetBlockFn)(void *context, int world_x, int world_y, int world_z, uint16_t block);

	/**
	 * Emits every village block that falls inside the given chunk only, so each
	 * chunk writes its own slice exactly once.
	 */
	void emitChunk(const Plan &plan, int chunk_x, int chunk_y, int chunk_z, SetBlockFn set, void *context);

	/**
	 * Plans accepted while chunks were generated. Villagers need to know where
	 * the real villages are (not just where a cell would like one), and only the
	 * chunk generator knows which candidates passed the site checks, so it
	 * publishes the accepted plans here.
	 */
	constexpr int MaxRegisteredPlans = 24;

	/** Records an accepted plan; idempotent per cell. */
	void registerPlan(const Plan &plan);
	/** Forgets every registered plan (new world / new seed). */
	void clearRegisteredPlans();
	/** Copies up to `max_out` plans whose centre lies within `radius_blocks`. */
	int registeredPlansNear(int world_x, int world_z, int radius_blocks, Plan *out, int max_out);
	int registeredPlanCount();

	/** Number of houses the plan contains (for tests and villager budgeting). */
	int houseCount(const Plan &plan);

	// Structure centres, in world block coordinates. Villagers use these to walk
	// to a real workplace or house instead of a made-up offset, and the host
	// tests check them against the blocks the plan actually emits.
	/** Centre of house `index` in the layout's house order; false if out of range. */
	bool houseCentre(const Plan &plan, int index, int &out_x, int &out_z);
	/** Centre of farm 0 or 1 (the water trench); false if out of range. */
	bool farmCentre(const Plan &plan, int index, int &out_x, int &out_z);
	/** Centre of the shed. */
	void shedCentre(const Plan &plan, int &out_x, int &out_z);

	/** Number of villagers a village of this plan should keep alive. */
	int villagerBudget(const Plan &plan);

	/** Deterministic profession (0..2) of villager `index` in this village. */
	int villagerProfession(const Plan &plan, int index);
}

#endif // VILLAGEGEN_H
