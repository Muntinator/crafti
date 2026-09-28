// Host tests for the procedural village generator.
//
// villagegen.cpp deliberately depends on nothing but terrain.h block ids, so
// the layout, the site search and the per-chunk emission can all be exercised
// on a host here. The tests pin down the properties the engine relies on:
//
//  - generation is a pure function of (seed, cell), so any chunk can be
//    generated at any time and still agree,
//  - emitChunk never writes outside the chunk it was asked for,
//  - villages in neighbouring cells do not overlap,
//  - the frequency setting really changes how many cells host a village.
//
// Build and run with `make -C tests`.

#include "villagegen.h"
#include "terrain.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

namespace
{
	// A tiny stand-in for the chunk a caller would be generating.
	struct Block
	{
		int x, y, z;
		uint16_t block;
	};

	struct Recorder
	{
		static const int MaxBlocks = 12000;
		Block blocks[MaxBlocks];
		int count = 0;
		int min_x = 0, max_x = 0, min_y = 0, max_y = 0, min_z = 0, max_z = 0;
		int out_of_bounds = 0;
		int chunk_x = 0, chunk_y = 0, chunk_z = 0;

		void reset(int cx, int cy, int cz)
		{
			count = 0;
			out_of_bounds = 0;
			chunk_x = cx;
			chunk_y = cy;
			chunk_z = cz;
			min_x = cx * Village::ChunkBlocks;
			max_x = min_x + Village::ChunkBlocks - 1;
			min_y = cy * Village::ChunkBlocks;
			max_y = min_y + Village::ChunkBlocks - 1;
			min_z = cz * Village::ChunkBlocks;
			max_z = min_z + Village::ChunkBlocks - 1;
		}

		void place(int x, int y, int z, uint16_t block)
		{
			if(x < min_x || x > max_x || y < min_y || y > max_y || z < min_z || z > max_z)
			{
				++out_of_bounds;
				return;
			}
			if(count < MaxBlocks)
				blocks[count++] = Block{x, y, z, block};
		}

		bool has(int x, int y, int z, uint16_t block) const
		{
			for(int i = 0; i < count; ++i)
				if(blocks[i].x == x && blocks[i].y == y && blocks[i].z == z && blocks[i].block == block)
					return true;
			return false;
		}

		bool hasAt(int x, int y, int z) const
		{
			for(int i = 0; i < count; ++i)
				if(blocks[i].x == x && blocks[i].y == y && blocks[i].z == z)
					return true;
			return false;
		}
	};

	Recorder recorder;

	void recordBlock(void *context, int x, int y, int z, uint16_t block)
	{
		static_cast<Recorder *>(context)->place(x, y, z, block);
	}

	int absI(int v) { return v < 0 ? -v : v; }

	int floorDivTest(int value, int divisor)
	{
		int quotient = value / divisor;
		if((value % divisor) != 0 && ((value < 0) != (divisor < 0)))
			--quotient;
		return quotient;
	}

	// Builds the plan for a cell exactly the way chunk.cpp does, using the flat
	// synthetic ground height so the site search always succeeds.
	int planForCell(unsigned int seed, int cell_x, int cell_z, int frequency, Village::Plan &plan, int ground_y = 20)
	{
		if(!Village::cellHasVillage(seed, cell_x, cell_z, frequency))
			return 0;
		for(int candidate = 0; candidate < Village::CandidateCount; ++candidate)
		{
			int ox, oz;
			Village::cellCandidate(seed, cell_x, cell_z, candidate, ox, oz);
			if(Village::planVillage(seed, cell_x, cell_z, candidate, ground_y, plan))
				return 1;
		}
		return 0;
	}

	// Finds a seed that really hosts a village in the given cell, so tests can
	// talk about a concrete plan instead of "whatever the seed gives".
	unsigned int seedWithVillageAtCell(int cell_x, int cell_z, int frequency)
	{
		for(unsigned int seed = 1; seed < 100000u; ++seed)
			if(Village::cellHasVillage(seed, cell_x, cell_z, frequency))
				return seed;
		return 0;
	}

	// Visits every chunk that could contain part of the plan, in plan order.
	template <typename Fn>
	void forEachVillageChunk(const Village::Plan &plan, Fn fn)
	{
		const int span = Village::Radius + 4;
		const int cx0 = floorDivTest(plan.origin_x - span, Village::ChunkBlocks);
		const int cx1 = floorDivTest(plan.origin_x + span, Village::ChunkBlocks);
		const int cz0 = floorDivTest(plan.origin_z - span, Village::ChunkBlocks);
		const int cz1 = floorDivTest(plan.origin_z + span, Village::ChunkBlocks);
		const int cy0 = floorDivTest(plan.ground_y - Village::FoundationDepth, Village::ChunkBlocks);
		const int cy1 = floorDivTest(plan.ground_y + Village::MaxHeightAboveGround, Village::ChunkBlocks);

		for(int cy = cy0; cy <= cy1; ++cy)
			for(int cz = cz0; cz <= cz1; ++cz)
				for(int cx = cx0; cx <= cx1; ++cx)
					fn(cx, cy, cz);
	}

	// Emits a whole village chunk by chunk into the recorder and returns the
	// number of blocks emitted plus a cheap checksum over them.
	void emitWholeVillage(const Village::Plan &plan, int &blocks, int &checksum)
	{
		blocks = 0;
		checksum = 0;
		forEachVillageChunk(plan, [&plan, &blocks, &checksum](int cx, int cy, int cz) {
			recorder.reset(cx, cy, cz);
			Village::emitChunk(plan, cx, cy, cz, recordBlock, &recorder);
			blocks += recorder.count;
			for(int i = 0; i < recorder.count; ++i)
				checksum += recorder.blocks[i].block * 3 + i * 7;
		});
	}

	void testHashSeedIsStableAndMixed()
	{
		// Same input, same output; different input, different output.
		CHECK(Village::hashSeed(1234u, 3, -4, 0x4c41594fu) == Village::hashSeed(1234u, 3, -4, 0x4c41594fu));
		CHECK(Village::hashSeed(1234u, 3, -4, 0x4c41594fu) != Village::hashSeed(1235u, 3, -4, 0x4c41594fu));
		CHECK(Village::hashSeed(1234u, 3, -4, 0x4c41594fu) != Village::hashSeed(1234u, 4, -4, 0x4c41594fu));
		CHECK(Village::hashSeed(1234u, 3, -4, 0x4c41594fu) != Village::hashSeed(1234u, 3, -3, 0x4c41594fu));
		CHECK(Village::hashSeed(1234u, 3, -4, 0x4c41594fu) != Village::hashSeed(1234u, 3, -4, 0x4c415950u));

		// A weak hash would show up as a very clumpy distribution over cells.
		int buckets[8] = {};
		for(int i = 0; i < 800; ++i)
			++buckets[Village::hashSeed(99u, i, i * 3 + 1, 0x56494c47u) & 7u];
		for(int i = 0; i < 8; ++i)
			CHECK(buckets[i] > 50 && buckets[i] < 150);
	}

	void testFrequencyGating()
	{
		// Off must never place a village, whatever the seed.
		for(int i = 0; i < 200; ++i)
			CHECK(!Village::cellHasVillage(7u, i, -i, Village::FrequencyOff));

		// The three enabled frequencies are ordered by how often they fire, and
		// land near their advertised rate.
		const int cells = 4000;
		int hits[Village::FrequencyCount] = {};
		for(int f = 0; f < Village::FrequencyCount; ++f)
			for(int i = 0; i < cells; ++i)
				if(Village::cellHasVillage(4242u, i % 100, i / 100 * (f + 1), f))
					++hits[f];

		CHECK(hits[Village::FrequencyOff] == 0);
		CHECK(hits[Village::FrequencyRare] < hits[Village::FrequencyNormal]);
		CHECK(hits[Village::FrequencyNormal] < hits[Village::FrequencyCommon]);

		CHECK(hits[Village::FrequencyRare] > cells * 20 / 100 && hits[Village::FrequencyRare] < cells * 30 / 100);
		CHECK(hits[Village::FrequencyNormal] > cells * 55 / 100 && hits[Village::FrequencyNormal] < cells * 65 / 100);
		CHECK(hits[Village::FrequencyCommon] > cells * 85 / 100 && hits[Village::FrequencyCommon] < cells * 95 / 100);

		// Out of range frequencies fall back to Normal and never crash.
		CHECK(!Village::cellHasVillage(1u, 0, 0, -5));
		CHECK(!Village::cellHasVillage(1u, 0, 0, Village::FrequencyCount));
	}

	void testPlanIsDeterministic()
	{
		const unsigned int seed = seedWithVillageAtCell(2, -3, Village::FrequencyCommon);
		CHECK(seed != 0);
		if(seed == 0)
			return;

		Village::Plan a, b;
		CHECK(planForCell(seed, 2, -3, Village::FrequencyCommon, a));
		CHECK(planForCell(seed, 2, -3, Village::FrequencyCommon, b));
		CHECK(a.valid == b.valid);
		CHECK(a.origin_x == b.origin_x);
		CHECK(a.origin_z == b.origin_z);
		CHECK(a.ground_y == b.ground_y);
		CHECK(a.seed == b.seed);

		// Two seeds with a village in the same cell must lay it out differently.
		unsigned int other_seed = 0;
		for(unsigned int s = seed + 1; s < 100000u && other_seed == 0; ++s)
			if(Village::cellHasVillage(s, 2, -3, Village::FrequencyCommon))
				other_seed = s;
		CHECK(other_seed != 0);
		if(other_seed != 0)
		{
			Village::Plan other;
			CHECK(planForCell(other_seed, 2, -3, Village::FrequencyCommon, other));
			CHECK(other.seed != a.seed);
		}
	}

	void testCandidateStaysInsideCell()
	{
		for(int index = 0; index < Village::CandidateCount; ++index)
		{
			int ox, oz;
			Village::cellCandidate(9u, 1, 1, index, ox, oz);
			CHECK(ox >= 1 * Village::CellBlocks + Village::CellBlocks / 2 - Village::CellJitter);
			CHECK(ox <= 1 * Village::CellBlocks + Village::CellBlocks / 2 + Village::CellJitter);
			CHECK(oz >= 1 * Village::CellBlocks + Village::CellBlocks / 2 - Village::CellJitter);
			CHECK(oz <= 1 * Village::CellBlocks + Village::CellBlocks / 2 + Village::CellJitter);
		}
	}

	void testNeighbouringVillagesDoNotOverlap()
	{
		// A village spans 2*Radius + overhang, one cell is CellBlocks wide, and
		// the origin jitter is bounded, so two occupied neighbours must keep a
		// clear gap. This is the property that keeps terrain from being
		// overwritten by two overlapping villages.
		const unsigned int seed = 31337u;
		for(int cz = -2; cz <= 2; ++cz)
			for(int cx = -2; cx <= 2; ++cx)
			{
				Village::Plan here;
				if(!planForCell(seed, cx, cz, Village::FrequencyCommon, here))
					continue;
				Village::Plan right;
				if(planForCell(seed, cx + 1, cz, Village::FrequencyCommon, right))
				{
					const int dx = right.origin_x - here.origin_x;
					CHECK(dx > 2 * Village::Radius);
				}
				Village::Plan below;
				if(planForCell(seed, cx, cz + 1, Village::FrequencyCommon, below))
				{
					const int dz = below.origin_z - here.origin_z;
					CHECK(dz > 2 * Village::Radius);
				}
				// Same-cell candidates can be close, but a cell only ever
				// hosts one village, so that is not a problem.
				(void)cx;
				(void)cz;
			}
	}

	void testEmitStaysInsideChunk()
	{
		const unsigned int seed = seedWithVillageAtCell(0, 0, Village::FrequencyCommon);
		CHECK(seed != 0);
		Village::Plan plan;
		CHECK(planForCell(seed, 0, 0, Village::FrequencyCommon, plan, 20));

		// Sweep every chunk the village could touch and make sure each write
		// lands in the chunk that was asked for.
		int total_blocks = 0;
		forEachVillageChunk(plan, [&plan, &total_blocks](int cx, int cy, int cz) {
			recorder.reset(cx, cy, cz);
			Village::emitChunk(plan, cx, cy, cz, recordBlock, &recorder);
			CHECK(recorder.out_of_bounds == 0);
			total_blocks += recorder.count;
		});
		CHECK(total_blocks > 300);

		// A chunk far away must receive nothing at all.
		recorder.reset(160, 2, 160);
		Village::emitChunk(plan, 160, 2, 160, recordBlock, &recorder);
		CHECK(recorder.count == 0);

		// Emission into a chunk above the roof must also produce nothing.
		recorder.reset(0, 12, 0);
		Village::emitChunk(plan, 0, 12, 0, recordBlock, &recorder);
		CHECK(recorder.count == 0);
	}

	// Collects a whole village by emitting it chunk by chunk, the way streaming
	// does, and returns the number of blocks plus the highest/lowest Y used.
	void testEmitIsOrderIndependent()
	{
		const unsigned int seed = seedWithVillageAtCell(1, 1, Village::FrequencyCommon);
		CHECK(seed != 0);
		Village::Plan plan;
		CHECK(planForCell(seed, 1, 1, Village::FrequencyCommon, plan, 20));

		int forward_blocks = 0, forward_sum = 0;
		emitWholeVillage(plan, forward_blocks, forward_sum);

		// The same chunks in reverse order must write exactly the same blocks.
		std::vector<int> coords;
		forEachVillageChunk(plan, [&coords](int cx, int cy, int cz) {
			coords.push_back(cx);
			coords.push_back(cy);
			coords.push_back(cz);
		});

		int backward_blocks = 0, backward_sum = 0;
		for(size_t i = coords.size(); i >= 3; i -= 3)
		{
			recorder.reset(coords[i - 3], coords[i - 2], coords[i - 1]);
			Village::emitChunk(plan, coords[i - 3], coords[i - 2], coords[i - 1], recordBlock, &recorder);
			backward_blocks += recorder.count;
			for(int b = 0; b < recorder.count; ++b)
				backward_sum += recorder.blocks[b].block * 3 + b * 7;
		}

		CHECK(forward_blocks == backward_blocks);
		CHECK(forward_sum == backward_sum);
	}

	void testStructuresArePresent()
	{
		const unsigned int seed = seedWithVillageAtCell(0, 0, Village::FrequencyCommon);
		CHECK(seed != 0);
		Village::Plan plan;
		CHECK(planForCell(seed, 0, 0, Village::FrequencyCommon, plan, 20));

		const int gy = plan.ground_y;
		int cobble = 0, planks = 0, dirt = 0, water = 0, wood = 0, wheat = 0,
			doors = 0, glass = 0, pumpkins = 0, flowers = 0, glowstone = 0, walls = 0;
		int highest = -1000, lowest = 1000;
		forEachVillageChunk(plan, [&](int cx, int cy, int cz) {
			recorder.reset(cx, cy, cz);
			Village::emitChunk(plan, cx, cy, cz, recordBlock, &recorder);
			for(int i = 0; i < recorder.count; ++i)
			{
				const uint8_t b = getBLOCK(recorder.blocks[i].block);
				if(b == BLOCK_COBBLESTONE) ++cobble;
				else if(b == BLOCK_PLANKS_NORMAL || b == BLOCK_PLANKS_DARK || b == BLOCK_PLANKS_BRIGHT) ++planks;
				else if(b == BLOCK_DIRT) ++dirt;
				else if(b == BLOCK_WATER_FAST) ++water;
				else if(b == BLOCK_WOOD) ++wood;
				else if(b == BLOCK_WHEAT) ++wheat;
				else if(b == BLOCK_DOOR) ++doors;
				else if(b == BLOCK_GLASS) ++glass;
				else if(b == BLOCK_PUMPKIN) ++pumpkins;
				else if(b == BLOCK_FLOWER) ++flowers;
				else if(b == BLOCK_GLOWSTONE) ++glowstone;
				else if(b == BLOCK_WALL) ++walls;

				// Nothing may be left floating above the roof or dug below the
				// foundation: the whole village is bounded by the plan.
				CHECK(recorder.blocks[i].y <= gy + Village::MaxHeightAboveGround);
				CHECK(recorder.blocks[i].y >= gy - Village::FoundationDepth);
				if(recorder.blocks[i].y > highest) highest = recorder.blocks[i].y;
				if(recorder.blocks[i].y < lowest) lowest = recorder.blocks[i].y;
			}
		});

		// A well, a plaza, a shed and 4..7 houses all contribute.
		CHECK(cobble > 60);
		CHECK(planks > 200);
		CHECK(dirt > 40);      // roads
		CHECK(water >= 1);     // well centre + farm trench
		CHECK(wood >= 8);      // corner posts, well posts, lamp posts
		CHECK(wheat >= 8);     // farms
		CHECK(doors >= 8);     // both halves of every door
		CHECK(glass >= 4);     // one per house
		CHECK(pumpkins == 9);  // the decoration patch
		CHECK(flowers > 0);
		CHECK(glowstone == 4); // four lamp posts
		CHECK(walls > 0);      // farms, boundary walls, shed

		CHECK(highest == gy + Village::MaxHeightAboveGround);
		CHECK(lowest == gy - Village::FoundationDepth);
	}

	// Collects a whole village into one block list so a test can ask "is there a
	// plank here?" the same way the world would after streaming those chunks in.
	std::vector<Block> collected;

	void collectBlock(void *, int x, int y, int z, uint16_t block)
	{
		collected.push_back(Block{ x, y, z, block });
	}

	void collectVillage(const Village::Plan &plan)
	{
		collected.clear();
		forEachVillageChunk(plan, [&plan](int cx, int cy, int cz) {
			Village::emitChunk(plan, cx, cy, cz, collectBlock, nullptr);
		});
	}

	bool isPlank(uint16_t block)
	{
		const uint8_t b = getBLOCK(block);
		return b == BLOCK_PLANKS_NORMAL || b == BLOCK_PLANKS_DARK || b == BLOCK_PLANKS_BRIGHT;
	}

	bool collectedHasPlank(int x, int y, int z)
	{
		for(const Block &b : collected)
			if(b.x == x && b.y == y && b.z == z && isPlank(b.block))
				return true;
		return false;
	}

	bool collectedHasBlock(int x, int y, int z, uint8_t wanted)
	{
		for(const Block &b : collected)
			if(b.x == x && b.y == y && b.z == z && getBLOCK(b.block) == wanted)
				return true;
		return false;
	}

	void testStructureCentresMatchEmittedBlocks()
	{
		const unsigned int seed = seedWithVillageAtCell(0, 0, Village::FrequencyCommon);
		CHECK(seed != 0);
		Village::Plan plan;
		CHECK(planForCell(seed, 0, 0, Village::FrequencyCommon, plan, 20));

		const int gy = plan.ground_y;
		const int ox = plan.origin_x;
		const int oz = plan.origin_z;
		collectVillage(plan);
		CHECK(!collected.empty());

		// Every house centre must sit on that house's plank floor, inside the village.
		const int houses = Village::houseCount(plan);
		CHECK(houses >= 4 && houses <= 7);
		int house_x[8], house_z[8];
		for(int i = 0; i < houses; ++i)
		{
			CHECK(Village::houseCentre(plan, i, house_x[i], house_z[i]));
			CHECK(absI(house_x[i] - ox) <= Village::Radius);
			CHECK(absI(house_z[i] - oz) <= Village::Radius);
			CHECK(collectedHasPlank(house_x[i], gy, house_z[i]));
			// Determinism: a second lookup must agree with the first.
			int again_x = 0, again_z = 0;
			CHECK(Village::houseCentre(plan, i, again_x, again_z));
			CHECK(again_x == house_x[i] && again_z == house_z[i]);
			// And no two houses share a centre.
			for(int j = 0; j < i; ++j)
				CHECK(house_x[i] != house_x[j] || house_z[i] != house_z[j]);
		}
		CHECK(!Village::houseCentre(plan, houses, house_x[0], house_z[0]));
		CHECK(!Village::houseCentre(plan, -1, house_x[0], house_z[0]));

		// Farm centres are the water trench in the middle of each field.
		for(int i = 0; i < 2; ++i)
		{
			int fx = 0, fz = 0;
			CHECK(Village::farmCentre(plan, i, fx, fz));
			CHECK(absI(fx - ox) <= Village::Radius);
			CHECK(absI(fz - oz) <= Village::Radius);
			CHECK(collectedHasBlock(fx, gy, fz, BLOCK_WATER_FAST));
		}
		CHECK(!Village::farmCentre(plan, 2, house_x[0], house_z[0]));
		CHECK(!Village::farmCentre(plan, -1, house_x[0], house_z[0]));

		// The shed centre is on its cobblestone floor.
		int sx = 0, sz = 0;
		Village::shedCentre(plan, sx, sz);
		CHECK(absI(sx - ox) <= Village::Radius);
		CHECK(absI(sz - oz) <= Village::Radius);
		CHECK(collectedHasBlock(sx, gy, sz, BLOCK_COBBLESTONE));
		int sx2 = 0, sz2 = 0;
		Village::shedCentre(plan, sx2, sz2);
		CHECK(sx2 == sx && sz2 == sz);

		// None of the workplaces may be the well at the village centre, or the
		// villagers would all pile onto it.
		CHECK(sx != ox || sz != oz);
		for(int i = 0; i < houses; ++i)
			CHECK(house_x[i] != ox || house_z[i] != oz);
	}

	void testHousesAndVillagers()
	{
		bool seen[3] = { false, false, false };
		int villages = 0;
		for(unsigned int seed = 1; seed <= 40; ++seed)
		{
			Village::Plan plan;
			if(!planForCell(seed, 0, 0, Village::FrequencyCommon, plan))
				continue;
			++villages;

			const int houses = Village::houseCount(plan);
			CHECK(houses >= 4 && houses <= 7);

			const int budget = Village::villagerBudget(plan);
			CHECK(budget >= 1 && budget <= 4);
			// The budget must rise with the house count, but never run away.
			CHECK(budget == (1 + houses / 2 > 4 ? 4 : 1 + houses / 2));

			for(int i = 0; i < 12; ++i)
			{
				const int profession = Village::villagerProfession(plan, i);
				CHECK(profession >= 0 && profession <= 2);
				CHECK(profession == Village::villagerProfession(plan, i));
				seen[profession] = true;
			}
		}
		CHECK(villages > 10);
		// Across many villages every profession shows up somewhere.
		CHECK(seen[0] && seen[1] && seen[2]);
	}

	void testDifferentSeedsDiffer()
	{
		// Two seeds must not produce the same village at the same cell, or the
		// world seed would not really control the layout.
		int differing = 0;
		for(unsigned int seed = 1; seed <= 20; ++seed)
		{
			Village::Plan a, b;
			if(!planForCell(seed, 0, 0, Village::FrequencyCommon, a))
				continue;
			if(!planForCell(seed + 1000u, 0, 0, Village::FrequencyCommon, b))
				continue;
			if(a.origin_x != b.origin_x || a.origin_z != b.origin_z || a.seed != b.seed)
				++differing;
		}
		CHECK(differing >= 15);
	}

	void testFrequencyAccessors()
	{
		Village::setFrequency(Village::FrequencyRare);
		CHECK(Village::frequency() == Village::FrequencyRare);
		Village::setFrequency(Village::FrequencyCommon);
		CHECK(Village::frequency() == Village::FrequencyCommon);

		// Out of range values fall back to Normal instead of being stored.
		Village::setFrequency(99);
		CHECK(Village::frequency() == Village::FrequencyNormal);
		Village::setFrequency(-3);
		CHECK(Village::frequency() == Village::FrequencyNormal);
	}

	void testPlanRegistry()
	{
		Village::clearRegisteredPlans();
		CHECK(Village::registeredPlanCount() == 0);

		// Nothing registered yet, so nothing can be found.
		Village::Plan found[4];
		CHECK(Village::registeredPlansNear(0, 0, 100, found, 4) == 0);

		Village::Plan a;
		const unsigned int seed = seedWithVillageAtCell(0, 0, Village::FrequencyCommon);
		CHECK(seed != 0);
		CHECK(planForCell(seed, 0, 0, Village::FrequencyCommon, a, 20));

		// An invalid plan must never enter the registry.
		Village::Plan invalid;
		Village::registerPlan(invalid);
		CHECK(Village::registeredPlanCount() == 0);

		Village::registerPlan(a);
		CHECK(Village::registeredPlanCount() == 1);
		CHECK(Village::registeredPlansNear(a.origin_x, a.origin_z, 4, found, 4) == 1);
		CHECK(found[0].origin_x == a.origin_x && found[0].origin_z == a.origin_z);

		// Far away is filtered out, and a null/short output buffer is tolerated.
		CHECK(Village::registeredPlansNear(a.origin_x + 10000, a.origin_z, 8, found, 4) == 0);
		CHECK(Village::registeredPlansNear(a.origin_x, a.origin_z, 8, nullptr, 4) == 0);
		CHECK(Village::registeredPlansNear(a.origin_x, a.origin_z, 8, found, 0) == 0);

		// Registering the same cell again replaces rather than duplicates.
		Village::Plan a2 = a;
		a2.ground_y = a.ground_y + 1;
		Village::registerPlan(a2);
		CHECK(Village::registeredPlanCount() == 1);
		CHECK(Village::registeredPlansNear(a.origin_x, a.origin_z, 4, found, 4) == 1);
		CHECK(found[0].ground_y == a2.ground_y);

		// The registry is bounded; filling it past the cap keeps the newest.
		for(int i = 1; i <= Village::MaxRegisteredPlans + 4; ++i)
		{
			Village::Plan p = a;
			p.cell_x = i;
			p.origin_x = a.origin_x + i * Village::CellBlocks;
			Village::registerPlan(p);
		}
		CHECK(Village::registeredPlanCount() == Village::MaxRegisteredPlans);
		const int newest_cell = Village::MaxRegisteredPlans + 4;
		CHECK(Village::registeredPlansNear(a.origin_x + newest_cell * Village::CellBlocks, a.origin_z,
		                                   4, found, 4) == 1);

		Village::clearRegisteredPlans();
		CHECK(Village::registeredPlanCount() == 0);
		CHECK(Village::registeredPlansNear(a.origin_x, a.origin_z, 100, found, 4) == 0);
	}

	void testInvalidPlansAreIgnored()
	{
		Village::Plan plan;
		CHECK(!plan.valid);
		recorder.reset(0, 0, 0);
		Village::emitChunk(plan, 0, 0, 0, recordBlock, &recorder);
		CHECK(recorder.count == 0);

		// A null callback must be tolerated as well.
		if(planForCell(1u, 0, 0, Village::FrequencyCommon, plan))
			Village::emitChunk(plan, 0, 0, 0, nullptr, &recorder);
		CHECK(recorder.count == 0);
	}
}

int main()
{
	printf("villagegen_test\n");

	testHashSeedIsStableAndMixed();
	testFrequencyGating();
	testPlanIsDeterministic();
	testCandidateStaysInsideCell();
	testNeighbouringVillagesDoNotOverlap();
	testEmitStaysInsideChunk();
	testEmitIsOrderIndependent();
	testStructuresArePresent();
	testStructureCentresMatchEmittedBlocks();
	testHousesAndVillagers();
	testDifferentSeedsDiffer();
	testFrequencyAccessors();
	testPlanRegistry();
	testInvalidPlansAreIgnored();

	printf("%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
