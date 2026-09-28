#include "villagegen.h"

#include "terrain.h"

namespace Village
{
	namespace
	{
		int g_frequency = FrequencyNormal;

		int absI(int v) { return v < 0 ? -v : v; }

		struct Rng
		{
			uint32_t state;
			explicit Rng(uint32_t s) : state(s ? s : 0x9e3779b9u) {}
			uint32_t next() { state = state * 1664525u + 1013904223u; return state; }
			int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo + 1)); }
			bool bit() { return (next() & 1u) != 0; }
		};

		/** Blocks of the village that fall inside the chunk currently generating. */
		struct Target
		{
			int x0, y0, z0, x1, y1, z1;
			SetBlockFn set;
			void *context;

			void place(int x, int y, int z, uint16_t block)
			{
				if(x < x0 || x > x1 || y < y0 || y > y1 || z < z0 || z > z1)
					return;
				set(context, x, y, z, block);
			}

			bool overlaps(int ax, int ay, int az, int bx, int by, int bz) const
			{
				return ax <= x1 && bx >= x0 && ay <= y1 && by >= y0 && az <= z1 && bz >= z0;
			}
		};

		void fillBox(Target &t, int ax, int ay, int az, int bx, int by, int bz, uint16_t block)
		{
			if(!t.overlaps(ax, ay, az, bx, by, bz))
				return;
			for(int y = ay; y <= by; ++y)
				for(int z = az; z <= bz; ++z)
					for(int x = ax; x <= bx; ++x)
						t.place(x, y, z, block);
		}

		/**
		 * Per-quadrant layout, in quadrant-local coordinates (u,v >= 0, measured
		 * outwards from the village centre). The four slots never overlap:
		 *
		 *   house1  u,v in [ 5, 11]  (7x7, or [6,10] when it is a 5x5)
		 *   house2  u,v in [11, 15]  (5x5)
		 *   farm    u in [ 6, 10], v in [11, 15]
		 *   shed    u in [11, 15], v in [ 6, 10]
		 */
		enum Slot { SlotHouse1 = 0, SlotHouse2, SlotFarm, SlotShed, SlotCount };

		void slotCentre(int slot, int &u, int &v)
		{
			switch(slot)
			{
			case SlotHouse1: u = 8;  v = 8;  break;
			case SlotHouse2: u = 13; v = 13; break;
			case SlotFarm:   u = 8;  v = 13; break;
			default:         u = 13; v = 8;  break;
			}
		}

		struct Layout
		{
			uint16_t plank;
			int house_q[8];
			int house_slot[8];
			int house_half[8];
			int house_count;
			int farm_q[2];
			int shed_q;
			int decor_q;
		};

		Layout computeLayout(const Plan &p)
		{
			Rng rng(p.seed);
			Layout l = {};
			const int material = rng.range(0, 2);
			l.plank = static_cast<uint16_t>(material == 0 ? BLOCK_PLANKS_NORMAL
				: (material == 1 ? BLOCK_PLANKS_DARK : BLOCK_PLANKS_BRIGHT));

			int order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
			for(int i = 7; i > 0; --i)
			{
				const int j = rng.range(0, i);
				const int tmp = order[i];
				order[i] = order[j];
				order[j] = tmp;
			}

			l.house_count = rng.range(4, 7);
			for(int i = 0; i < l.house_count; ++i)
			{
				const int index = order[i];
				l.house_q[i] = index / 2;
				l.house_slot[i] = index % 2; // SlotHouse1 or SlotHouse2
				l.house_half[i] = (l.house_slot[i] == SlotHouse1 && rng.bit()) ? 3 : 2;
			}

			int q_order[4] = { 0, 1, 2, 3 };
			for(int i = 3; i > 0; --i)
			{
				const int j = rng.range(0, i);
				const int tmp = q_order[i];
				q_order[i] = q_order[j];
				q_order[j] = tmp;
			}
			l.farm_q[0] = q_order[0];
			l.farm_q[1] = q_order[1];
			l.shed_q = q_order[2];
			l.decor_q = q_order[3];
			return l;
		}

		inline int qSign(int q, bool x) { return (q & (x ? 1 : 2)) ? 1 : -1; }
		inline int qx(const Plan &p, int q, int u) { return p.origin_x + qSign(q, true) * u; }
		inline int qz(const Plan &p, int q, int v) { return p.origin_z + qSign(q, false) * v; }

		void buildRoads(const Plan &p, Target &t)
		{
			const int gy = p.ground_y;
			for(int i = -Radius; i <= Radius; ++i)
				for(int d = -1; d <= 0; ++d)
				{
					t.place(p.origin_x + i, gy, p.origin_z + d, BLOCK_DIRT);
					t.place(p.origin_x + d, gy, p.origin_z + i, BLOCK_DIRT);
				}
		}

		void buildPlaza(const Plan &p, Target &t)
		{
			const int gy = p.ground_y;
			fillBox(t, p.origin_x - 4, gy, p.origin_z - 4, p.origin_x + 4, gy, p.origin_z + 4, BLOCK_COBBLESTONE);
		}

		void buildWell(const Plan &p, Target &t, uint16_t plank)
		{
			const int gy = p.ground_y;
			const int ox = p.origin_x;
			const int oz = p.origin_z;

			for(int x = -1; x <= 1; ++x)
				for(int z = -1; z <= 1; ++z)
					t.place(ox + x, gy, oz + z, BLOCK_COBBLESTONE);
			t.place(ox, gy, oz, getBLOCKWDATA(BLOCK_WATER_FAST, 0));

			for(int y = gy + 1; y <= gy + 2; ++y)
			{
				t.place(ox - 1, y, oz - 1, BLOCK_WOOD);
				t.place(ox + 1, y, oz - 1, BLOCK_WOOD);
				t.place(ox - 1, y, oz + 1, BLOCK_WOOD);
				t.place(ox + 1, y, oz + 1, BLOCK_WOOD);
			}
			fillBox(t, ox - 1, gy + 3, oz - 1, ox + 1, gy + 3, oz + 1, plank);
		}

		void buildLampPosts(const Plan &p, Target &t)
		{
			const int gy = p.ground_y;
			static const int corners[4][2] = { { 3, 3 }, { 3, -3 }, { -3, 3 }, { -3, -3 } };
			for(int i = 0; i < 4; ++i)
			{
				const int x = p.origin_x + corners[i][0];
				const int z = p.origin_z + corners[i][1];
				for(int y = gy + 1; y <= gy + 3; ++y)
					t.place(x, y, z, BLOCK_WOOD);
				t.place(x, gy + 4, z, BLOCK_GLOWSTONE);
			}
		}

		void buildFences(const Plan &p, Target &t)
		{
			const int gy = p.ground_y;
			const int oz = p.origin_z;
			// Low walls on the east and west edges, broken up so they never seal the village.
			for(int i = -Radius; i <= Radius; ++i)
			{
				if(absI(i) <= 2 || (i % 6) == 0)
					continue;
				t.place(p.origin_x + Radius, gy + 1, oz + i, BLOCK_WALL);
				t.place(p.origin_x - Radius, gy + 1, oz + i, BLOCK_WALL);
			}
		}

		void buildHouse(const Plan &p, Target &t, int q, int half, uint16_t plank)
		{
			const int gy = p.ground_y;
			int u, v;
			slotCentre(SlotHouse1, u, v);
			const int cx = qx(p, q, u);
			const int cz = qz(p, q, v);
			const int x0 = cx - half, x1 = cx + half;
			const int z0 = cz - half, z1 = cz + half;

			fillBox(t, x0, gy - FoundationDepth, z0, x1, gy - 1, z1, BLOCK_COBBLESTONE);
			fillBox(t, x0, gy, z0, x1, gy, z1, plank);

			for(int y = gy + 1; y <= gy + 3; ++y)
			{
				for(int x = x0; x <= x1; ++x)
				{
					t.place(x, y, z0, plank);
					t.place(x, y, z1, plank);
				}
				for(int z = z0; z <= z1; ++z)
				{
					t.place(x0, y, z, plank);
					t.place(x1, y, z, plank);
				}
			}
			for(int y = gy + 1; y <= gy + 3; ++y)
			{
				t.place(x0, y, z0, BLOCK_WOOD);
				t.place(x1, y, z0, BLOCK_WOOD);
				t.place(x0, y, z1, BLOCK_WOOD);
				t.place(x1, y, z1, BLOCK_WOOD);
			}

			t.place(cx, gy + 2, z0, BLOCK_GLASS);
			t.place(cx, gy + 2, z1, BLOCK_GLASS);
			t.place(x0, gy + 2, cz, BLOCK_GLASS);
			t.place(x1, gy + 2, cz, BLOCK_GLASS);

			// Door on the wall facing the village centre along z.
			const int outward = qz(p, q, 1) > p.origin_z ? 1 : -1;
			const int door_z = outward > 0 ? z0 : z1;
			const int side = outward > 0 ? BLOCK_FRONT : BLOCK_BACK;
			t.place(cx, gy + 1, door_z, getBLOCKWDATA(BLOCK_DOOR, static_cast<uint8_t>(side)));
			t.place(cx, gy + 2, door_z, getBLOCKWDATA(BLOCK_DOOR, static_cast<uint8_t>(side | 8)));

			fillBox(t, x0 - 1, gy + 4, z0 - 1, x1 + 1, gy + 4, z1 + 1, plank);
			fillBox(t, x0, gy + 5, z0, x1, gy + 5, z1, plank);

			if(half >= 3)
			{
				t.place(cx - 1, gy + 1, cz + 1, BLOCK_CRAFTING_TABLE);
				t.place(cx + 1, gy + 1, cz + 1, BLOCK_FURNACE);
			}
		}

		void buildSmallHouse(const Plan &p, Target &t, int q, uint16_t plank)
		{
			const int gy = p.ground_y;
			int u, v;
			slotCentre(SlotHouse2, u, v);
			const int cx = qx(p, q, u);
			const int cz = qz(p, q, v);
			const int half = 2;
			const int x0 = cx - half, x1 = cx + half;
			const int z0 = cz - half, z1 = cz + half;

			fillBox(t, x0, gy - FoundationDepth, z0, x1, gy - 1, z1, BLOCK_COBBLESTONE);
			fillBox(t, x0, gy, z0, x1, gy, z1, plank);
			for(int y = gy + 1; y <= gy + 3; ++y)
			{
				for(int x = x0; x <= x1; ++x)
				{
					t.place(x, y, z0, plank);
					t.place(x, y, z1, plank);
				}
				for(int z = z0; z <= z1; ++z)
				{
					t.place(x0, y, z, plank);
					t.place(x1, y, z, plank);
				}
			}
			t.place(x0, gy + 2, cz, BLOCK_GLASS);
			t.place(x1, gy + 2, cz, BLOCK_GLASS);

			const int outward = qz(p, q, 1) > p.origin_z ? 1 : -1;
			const int door_z = outward > 0 ? z0 : z1;
			const int side = outward > 0 ? BLOCK_FRONT : BLOCK_BACK;
			t.place(cx, gy + 1, door_z, getBLOCKWDATA(BLOCK_DOOR, static_cast<uint8_t>(side)));
			t.place(cx, gy + 2, door_z, getBLOCKWDATA(BLOCK_DOOR, static_cast<uint8_t>(side | 8)));

			fillBox(t, x0 - 1, gy + 4, z0 - 1, x1 + 1, gy + 4, z1 + 1, plank);
		}

		void buildFarm(const Plan &p, Target &t, int q)
		{
			const int gy = p.ground_y;
			int u, v;
			slotCentre(SlotFarm, u, v);
			const int cx = qx(p, q, u);
			const int cz = qz(p, q, v);
			const int x0 = cx - 2, x1 = cx + 2;
			const int z0 = cz - 2, z1 = cz + 2;

			for(int x = x0; x <= x1; ++x)
			{
				t.place(x, gy + 1, z0, BLOCK_WALL);
				t.place(x, gy + 1, z1, BLOCK_WALL);
			}
			for(int z = z0 + 1; z <= z1 - 1; ++z)
			{
				t.place(x0, gy + 1, z, BLOCK_WALL);
				t.place(x1, gy + 1, z, BLOCK_WALL);
			}

			for(int x = x0 + 1; x <= x1 - 1; ++x)
				for(int z = z0 + 1; z <= z1 - 1; ++z)
				{
					if(x == cx)
						t.place(x, gy, z, getBLOCKWDATA(BLOCK_WATER_FAST, 0));
					else
					{
						t.place(x, gy, z, BLOCK_DIRT);
						t.place(x, gy + 1, z, getBLOCKWDATA(BLOCK_WHEAT, static_cast<uint8_t>(((x + z) & 1) ? 7 : 5)));
					}
				}
		}

		void buildShed(const Plan &p, Target &t, int q, uint16_t plank)
		{
			const int gy = p.ground_y;
			int u, v;
			slotCentre(SlotShed, u, v);
			const int cx = qx(p, q, u);
			const int cz = qz(p, q, v);
			const int x0 = cx - 2, x1 = cx + 2;
			const int z0 = cz - 2, z1 = cz + 2;

			fillBox(t, x0, gy, z0, x1, gy, z1, BLOCK_COBBLESTONE);

			// The opening (and the doorway gap) faces the village centre along x.
			const bool open_low_u = qSign(q, true) > 0;
			for(int y = gy + 1; y <= gy + 2; ++y)
			{
				for(int x = x0; x <= x1; ++x)
				{
					t.place(x, y, z0, plank);
					t.place(x, y, z1, plank);
				}
				for(int z = z0 + 1; z <= z1 - 1; ++z)
				{
					if(!(open_low_u && z == cz))
						t.place(x0, y, z, plank);
					if(!(!open_low_u && z == cz))
						t.place(x1, y, z, plank);
				}
			}
			fillBox(t, x0, gy + 3, z0, x1, gy + 3, z1, plank);

			const int back_x = open_low_u ? x1 : x0;
			t.place(back_x, gy + 1, cz - 1, BLOCK_CRAFTING_TABLE);
			t.place(back_x, gy + 1, cz, BLOCK_BOOKSHELF);
			t.place(back_x, gy + 1, cz + 1, BLOCK_FURNACE);
		}

		void buildDecorations(const Plan &p, Target &t, int q)
		{
			const int gy = p.ground_y;

			// Pumpkin patch in the quadrant that has no farm or shed.
			int u, v;
			slotCentre(SlotShed, u, v);
			const int pcx = qx(p, q, u);
			const int pcz = qz(p, q, v);
			for(int x = -1; x <= 1; ++x)
				for(int z = -1; z <= 1; ++z)
					t.place(pcx + x, gy + 1, pcz + z, BLOCK_PUMPKIN);

			// Flowers and bushes along the free strip between the plaza and the houses.
			for(int i = -Radius + 4; i <= Radius - 4; i += 4)
			{
				if(absI(i) <= 4)
					continue;
				t.place(p.origin_x + 3, gy + 1, p.origin_z + i, getBLOCKWDATA(BLOCK_FLOWER, 0));
				t.place(p.origin_x - 3, gy + 1, p.origin_z + i, getBLOCKWDATA(BLOCK_FLOWER, 1));
				t.place(p.origin_x + i, gy + 1, p.origin_z + 3, getBLOCKWDATA(BLOCK_FLOWER, 0));
				t.place(p.origin_x + i, gy + 1, p.origin_z - 3, getBLOCKWDATA(BLOCK_FLOWER, 1));
			}
		}

		void buildVillage(const Plan &p, Target &t)
		{
			const Layout l = computeLayout(p);
			buildRoads(p, t);
			buildPlaza(p, t);
			buildWell(p, t, l.plank);
			buildLampPosts(p, t);
			buildFences(p, t);

			for(int i = 0; i < l.house_count; ++i)
			{
				if(l.house_slot[i] == SlotHouse1)
					buildHouse(p, t, l.house_q[i], l.house_half[i], l.plank);
				else
					buildSmallHouse(p, t, l.house_q[i], l.plank);
			}

			buildFarm(p, t, l.farm_q[0]);
			buildFarm(p, t, l.farm_q[1]);
			buildShed(p, t, l.shed_q, l.plank);
			buildDecorations(p, t, l.decor_q);
		}
	}

	Plan g_registered[MaxRegisteredPlans];
	int g_registered_count = 0;
	int g_registered_cursor = 0;

	void registerPlan(const Plan &plan)
	{
		if(!plan.valid)
			return;

		for(int i = 0; i < g_registered_count; ++i)
			if(g_registered[i].cell_x == plan.cell_x && g_registered[i].cell_z == plan.cell_z)
			{
				g_registered[i] = plan;
				return;
			}

		if(g_registered_count < MaxRegisteredPlans)
			g_registered[g_registered_count++] = plan;
		else
		{
			// Round-robin replacement keeps the most recently visited areas.
			g_registered[g_registered_cursor] = plan;
			g_registered_cursor = (g_registered_cursor + 1) % MaxRegisteredPlans;
		}
	}

	void clearRegisteredPlans()
	{
		g_registered_count = 0;
		g_registered_cursor = 0;
	}

	int registeredPlansNear(int world_x, int world_z, int radius_blocks, Plan *out, int max_out)
	{
		if(out == nullptr || max_out <= 0)
			return 0;

		const int radius = radius_blocks < 0 ? 0 : radius_blocks;
		int found = 0;
		for(int i = 0; i < g_registered_count && found < max_out; ++i)
		{
			const Plan &plan = g_registered[i];
			if(absI(plan.origin_x - world_x) <= radius && absI(plan.origin_z - world_z) <= radius)
				out[found++] = plan;
		}
		return found;
	}

	int registeredPlanCount() { return g_registered_count; }

	void setFrequency(int frequency)
	{
		if(frequency < 0 || frequency >= FrequencyCount)
			frequency = FrequencyNormal;
		g_frequency = frequency;
	}

	int frequency() { return g_frequency; }

	uint32_t hashSeed(uint32_t world_seed, int cell_x, int cell_z, uint32_t salt)
	{
		uint32_t h = world_seed
			^ (static_cast<uint32_t>(cell_x) * 73856093u)
			^ (static_cast<uint32_t>(cell_z) * 19349663u)
			^ salt;
		h ^= h >> 16;
		h *= 0x7feb352du;
		h ^= h >> 15;
		h *= 0x846ca68bu;
		h ^= h >> 16;
		return h;
	}

	bool cellHasVillage(uint32_t world_seed, int cell_x, int cell_z, int freq)
	{
		if(freq <= FrequencyOff || freq >= FrequencyCount)
			return false;

		static const int chance[FrequencyCount] = { 0, 25, 60, 90 };
		const uint32_t h = hashSeed(world_seed, cell_x, cell_z, 0x56494c47u); // "VILG"
		return static_cast<int>(h % 100u) < chance[freq];
	}

	void cellCandidate(uint32_t world_seed, int cell_x, int cell_z, int index, int &out_x, int &out_z)
	{
		const uint32_t h = hashSeed(world_seed, cell_x, cell_z,
			0x43414e44u ^ (static_cast<uint32_t>(index) * 2654435761u));
		const int jx = static_cast<int>(h % (2 * CellJitter + 1)) - CellJitter;
		const int jz = static_cast<int>((h >> 8) % (2 * CellJitter + 1)) - CellJitter;
		out_x = cell_x * CellBlocks + CellBlocks / 2 + jx;
		out_z = cell_z * CellBlocks + CellBlocks / 2 + jz;
	}

	bool planVillage(uint32_t world_seed, int cell_x, int cell_z, int candidate, int ground_y, Plan &out)
	{
		int ox, oz;
		cellCandidate(world_seed, cell_x, cell_z, candidate, ox, oz);
		out.valid = true;
		out.cell_x = cell_x;
		out.cell_z = cell_z;
		out.origin_x = ox;
		out.origin_z = oz;
		out.ground_y = ground_y;
		out.seed = hashSeed(world_seed, cell_x, cell_z, 0x4c41594fu); // "LAYO"
		return true;
	}

	int houseCount(const Plan &plan) { return computeLayout(plan).house_count; }

	bool houseCentre(const Plan &plan, int index, int &out_x, int &out_z)
	{
		if(!plan.valid)
			return false;

		const Layout l = computeLayout(plan);
		if(index < 0 || index >= l.house_count)
			return false;

		// Big and small houses live in different slots; computeLayout is a pure
		// function of the plan seed, so this always matches what was emitted.
		int u, v;
		slotCentre(l.house_slot[index] == SlotHouse1 ? SlotHouse1 : SlotHouse2, u, v);
		out_x = qx(plan, l.house_q[index], u);
		out_z = qz(plan, l.house_q[index], v);
		return true;
	}

	bool farmCentre(const Plan &plan, int index, int &out_x, int &out_z)
	{
		if(!plan.valid || index < 0 || index > 1)
			return false;

		int u, v;
		slotCentre(SlotFarm, u, v);
		out_x = qx(plan, computeLayout(plan).farm_q[index], u);
		out_z = qz(plan, computeLayout(plan).farm_q[index], v);
		return true;
	}

	void shedCentre(const Plan &plan, int &out_x, int &out_z)
	{
		if(!plan.valid)
		{
			out_x = plan.origin_x;
			out_z = plan.origin_z;
			return;
		}

		int u, v;
		slotCentre(SlotShed, u, v);
		const int q = computeLayout(plan).shed_q;
		out_x = qx(plan, q, u);
		out_z = qz(plan, q, v);
	}

	int villagerBudget(const Plan &plan)
	{
		const int budget = 1 + computeLayout(plan).house_count / 2;
		return budget > 4 ? 4 : budget;
	}

	int villagerProfession(const Plan &plan, int index)
	{
		const uint32_t h = hashSeed(plan.seed, index, 0, 0x50524f46u); // "PROF"
		return static_cast<int>(h % 3u);
	}

	void emitChunk(const Plan &plan, int chunk_x, int chunk_y, int chunk_z, SetBlockFn set, void *context)
	{
		if(!plan.valid || set == nullptr)
			return;

		Target t;
		t.x0 = chunk_x * ChunkBlocks;
		t.y0 = chunk_y * ChunkBlocks;
		t.z0 = chunk_z * ChunkBlocks;
		t.x1 = t.x0 + ChunkBlocks - 1;
		t.y1 = t.y0 + ChunkBlocks - 1;
		t.z1 = t.z0 + ChunkBlocks - 1;
		t.set = set;
		t.context = context;

		const int reach = Radius + 2;
		if(!t.overlaps(plan.origin_x - reach, plan.ground_y - FoundationDepth, plan.origin_z - reach,
		               plan.origin_x + reach, plan.ground_y + MaxHeightAboveGround, plan.origin_z + reach))
			return;

		buildVillage(plan, t);
	}
}
