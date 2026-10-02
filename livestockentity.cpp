#include "livestockentity.h"

#include <cmath>
#include <cstdlib>

#include "audio_manager.h"
#include "fastmath.h"
#include "gl.h"
#include "mobmodel.h"
#include "enchanting.h"
#include "grounddrops.h"
#include "terrain.h"
#include "textures/items.h"
#include "world.h"
#include "worldtask.h"

#include "textures/chicken.h"
#include "textures/cow.h"
#include "textures/donkey.h"
#include "textures/horse.h"
#include "textures/mooshroom.h"
#include "textures/pig.h"
#include "textures/sheep.h"
#include "textures/wolf.h"

std::vector<LivestockEntity> livestock_entities;

namespace
{
	constexpr unsigned int SpeciesCount = Livestock::SpeciesCount;

	/** Ticks a baby takes to grow up (~2 minutes at the usual tick rate). */
	constexpr int BabyTicks = 2400;
	/** Ticks an adult stays "in love" after being fed. */
	constexpr int LoveTicks = 600;
	/** Cooldown after breeding so the same pair does not repeat instantly. */
	constexpr int BreedCooldownTicks = -900;
	/** Ticks a hurt animal keeps running away. */
	constexpr int FleeTicks = 90;

	/// Spawn cadence: quick while the world is still filling up, slower once it is stocked.
	constexpr unsigned int SpawnFillInterval = 30;
	constexpr unsigned int SpawnSteadyInterval = 90;
	unsigned int spawn_timer = 0;

	/** Baby scale as a percentage of the adult model. */
	constexpr int BabyScalePercent = 60;

	inline GLFix absFix(GLFix v) { return v < GLFix(0) ? -v : v; }

	/** Chebyshev distance to the player in blocks, for sound attenuation and culling. */
	int blocksToPlayer(GLFix ax, GLFix az)
	{
		const int bx = absFix(ax - world_task.x).toInteger<int>() / BLOCK_SIZE;
		const int bz = absFix(az - world_task.z).toInteger<int>() / BLOCK_SIZE;
		return bx > bz ? bx : bz;
	}

	/** Compass angle (degrees) of a horizontal vector, for facing a direction. */
	GLFix angleFromVector(GLFix dx, GLFix dz)
	{
		const float a = std::atan2(static_cast<float>(dx), static_cast<float>(dz)) * 57.29578f;
		GLFix result(a);
		result.normaliseAngle();
		return result;
	}

	GameAudio::MobKind audioKind(Livestock::Species species)
	{
		switch(species)
		{
		case Livestock::Species::Cow: return GameAudio::MobCow;
		case Livestock::Species::Pig: return GameAudio::MobPig;
		case Livestock::Species::Sheep: return GameAudio::MobSheep;
		case Livestock::Species::Horse: return GameAudio::MobHorse;
		case Livestock::Species::Wolf: return GameAudio::MobWolf;
		// A mooshroom lows like the cow and a donkey brays with the horse set.
		case Livestock::Species::Mooshroom: return GameAudio::MobCow;
		case Livestock::Species::Donkey: return GameAudio::MobHorse;
		default: return GameAudio::MobChicken;
		}
	}

	const TEXTURE *textureFor(Livestock::Species species)
	{
		switch(species)
		{
		case Livestock::Species::Cow: return &cow_tex;
		case Livestock::Species::Pig: return &pig_tex;
		case Livestock::Species::Sheep: return &sheep_tex;
		case Livestock::Species::Horse: return &horse_tex;
		case Livestock::Species::Wolf: return &wolf_tex;
		case Livestock::Species::Mooshroom: return &mooshroom_tex;
		case Livestock::Species::Donkey: return &donkey_tex;
		default: return &chicken_tex;
		}
	}

	Livestock::Surface surfaceFromBlock(BLOCK block)
	{
		switch(block)
		{
		case BLOCK_GRASS: return Livestock::Surface::Grass;
		case BLOCK_SAND: return Livestock::Surface::Sand;
		case BLOCK_DIRT: return Livestock::Surface::Grass;
		case BLOCK_STONE:
		case BLOCK_COBBLESTONE:
		case BLOCK_BEDROCK: return Livestock::Surface::Stone;
		case BLOCK_WATER:
		case BLOCK_WATER_FAST: return Livestock::Surface::Water;
		default: return Livestock::Surface::Other;
		}
	}

	// --- rendering helpers (the chicken still draws itself) ------------------

	/** One vanilla model box; the unwrap and the emit live in mobmodel.cpp. */
	void drawQuadBox(
		GLFix bx, GLFix by, GLFix bz,
		GLFix bw, GLFix bh, GLFix bd,
		int u0, int v0, int wp, int hp, int dp,
		bool mirror = false)
	{
		Mob::drawBox(bx, by, bz, bw, bh, bd, u0, v0, wp, hp, dp, mirror);
	}

	bool isBreedingFood(uint8_t item)
	{
		return item == static_cast<uint8_t>(ItemTexture::WHEAT_SEEDS)
			|| item == static_cast<uint8_t>(ItemTexture::APPLE);
	}

	/** Scans a column for the topmost solid surface; false when there is none. */
	bool sampleSurface(int block_x, int block_z, int &surface_y, BLOCK &surface_block, bool &near_water)
	{
		const int top = World::HEIGHT * Chunk::SIZE;
		bool water_seen = false;
		for(int y = top - 1; y >= 0; --y)
		{
			const BLOCK block = getBLOCK(world.getBlock(block_x, y, block_z));
			if(block == BLOCK_AIR)
				continue;
			if(block == BLOCK_WATER || block == BLOCK_WATER_FAST)
			{
				water_seen = true;
				continue;
			}

			surface_y = y;
			surface_block = block;
			near_water = water_seen;
			// Adjacent columns count as shore too, so beaches read as shoreline.
			if(!near_water)
			{
				static const int offsets[4][2] = { { 2, 0 }, { -2, 0 }, { 0, 2 }, { 0, -2 } };
				for(unsigned int i = 0; i < 4; ++i)
				{
					const BLOCK side = getBLOCK(world.getBlock(block_x + offsets[i][0], y + 1, block_z + offsets[i][1]));
					if(side == BLOCK_WATER || side == BLOCK_WATER_FAST)
					{
						near_water = true;
						break;
					}
				}
			}
			return true;
		}
		return false;
	}

	/** Random spawn point near the player; false when no suitable ground is found. */
	bool findSpawnPoint(int &out_x, int &out_y, int &out_z, Livestock::Biome &out_biome)
	{
		const int px = positionToBlock(world_task.x.toInteger<int>());
		const int pz = positionToBlock(world_task.z.toInteger<int>());

		// Stay inside the loaded chunk radius (~field_of_view chunks on the CX).
		for(int attempt = 0; attempt < 6; ++attempt)
		{
			const int angle = rand() % 360;
			const int distance = 8 + rand() % 9; // 8..16 blocks
			const GLFix dir_x = fast_sin(GLFix(angle));
			const GLFix dir_z = fast_cos(GLFix(angle));
			const int bx = px + (dir_x * distance).toInteger<int>();
			const int bz = pz + (dir_z * distance).toInteger<int>();

			int surface_y = 0;
			BLOCK surface_block = BLOCK_AIR;
			bool near_water = false;
			if(!sampleSurface(bx, bz, surface_y, surface_block, near_water))
				continue;

			const BLOCK above = getBLOCK(world.getBlock(bx, surface_y + 1, bz));
			if(above != BLOCK_AIR)
				continue;

			const int top = World::HEIGHT * Chunk::SIZE;
			if(surface_y + 2 >= top)
				continue;

			const Livestock::Surface surface = surfaceFromBlock(surface_block);
			// Only real terrain surfaces spawn animals; this also rejects
			// unloaded columns, which read back as stone.
			if(surface != Livestock::Surface::Grass && surface != Livestock::Surface::Sand)
				continue;

			const bool has_trees = world.noiseGenerator().noise(GLFix(bx) / GLFix(8), GLFix(bz) / GLFix(8), 25) < GLFix(0.3f);
			out_biome = Livestock::classify(near_water, has_trees, surface);
			if(out_biome == Livestock::Biome::Water)
				continue;

			out_x = bx;
			out_y = surface_y + 1;
			out_z = bz;
			return true;
		}
		return false;
	}

	bool spawnOne()
	{
		if(livestock_entities.size() >= Livestock::maxEntities())
			return false;

		int bx = 0, by = 0, bz = 0;
		Livestock::Biome biome = Livestock::Biome::Grassland;
		if(!findSpawnPoint(bx, by, bz, biome))
			return false;

		const Livestock::Species species = Livestock::pickSpecies(static_cast<uint32_t>(rand() % 100), biome);
		if(species == Livestock::Species::Count)
			return false;

		livestock_entities.emplace_back(
			species,
			GLFix(bx * BLOCK_SIZE) + GLFix(BLOCK_SIZE / 2),
			GLFix(by * BLOCK_SIZE) + GLFix::minStep(),
			GLFix(bz * BLOCK_SIZE) + GLFix(BLOCK_SIZE / 2));
		return true;
	}

	constexpr unsigned int PendingBabies = 4;
}

LivestockEntity::LivestockEntity()
	: x(0), y(GLFix(World::HEIGHT * Chunk::SIZE) * BLOCK_SIZE), z(0),
	  vx(0), vy(0), vz(0), yaw(0), walk_timer(0), swing_intensity(0),
	  health(1), hurt_time(0), hurt_resistant(0), death_time(0), dir_timer(0),
	  love_timer(0), flee_timer(0), age(0), ticks_alive(0), fire_ticks(0), killing_looting(0),
	  species(static_cast<uint8_t>(Livestock::Species::Cow)),
	  on_ground(false), loot_spawned(false)
{
	const Livestock::Stats &st = Livestock::stats(kind());
	const GLFix w = GLFix(st.width);
	health = st.health;
	aabb = { x - w / 2, y, z - w / 2, x + w / 2, y + GLFix(st.height), z + w / 2 };
}

LivestockEntity::LivestockEntity(Livestock::Species kind_, GLFix px, GLFix py, GLFix pz, bool baby)
	: x(px), y(py), z(pz),
	  vx(0), vy(0), vz(0), yaw(GLFix(rand() % 360)), walk_timer(0), swing_intensity(0),
	  health(1), hurt_time(0), hurt_resistant(0), death_time(0), dir_timer(rand() % 60),
	  love_timer(0), flee_timer(0), age(baby ? -BabyTicks / 2 : 0), ticks_alive(0), fire_ticks(0), killing_looting(0),
	  species(static_cast<uint8_t>(kind_)),
	  on_ground(false), loot_spawned(false)
{
	const Livestock::Stats &st = Livestock::stats(kind_);
	const GLFix w = GLFix(st.width);
	health = st.health;
	aabb = { x - w / 2, y, z - w / 2, x + w / 2, y + GLFix(st.height), z + w / 2 };
}

void LivestockEntity::applyMeleeDamage(int amount, GLFix attacker_yaw, int knockback_steps,
                                      int looting, int set_fire_ticks)
{
	if(health <= 0 || hurt_resistant > 0)
		return;

	health -= amount;
	hurt_time = 10;
	hurt_resistant = 10;
	flee_timer = FleeTicks;

	// Fire Aspect sets the animal alight for as long as the weapon says; a burn
	// already going only gets longer.
	if(set_fire_ticks > 0 && fire_ticks < set_fire_ticks)
		fire_ticks = static_cast<int16_t>(set_fire_ticks);

	// Looting is remembered until the animal dies, because the extra drops are
	// rolled where the drop table is (in update(), below).
	if(looting > killing_looting)
		killing_looting = static_cast<int8_t>(looting);

	GameAudio::mobSound(audioKind(kind()), true, blocksToPlayer(x, z));

	// Knockback throws the animal further than the plain hit does.
	const GLFix impulse = GLFix(10 + 4 * (knockback_steps < 0 ? 0 : knockback_steps));
	GLFix ay = attacker_yaw;
	ay.normaliseAngle();
	vx /= 2;
	vz /= 2;
	vx += fast_sin(ay) * impulse;
	vz += fast_cos(ay) * impulse;
	if(on_ground)
	{
		vy /= 2;
		vy += GLFix(12);
		const GLFix cap(22);
		if(vy > cap)
			vy = cap;
	}

	if(health < 0)
		health = 0;
}

void LivestockEntity::feed()
{
	if(health <= 0 || isBaby())
		return;
	if(love_timer != 0)
		return;
	love_timer = LoveTicks;
}

void LivestockEntity::update()
{
	const Livestock::Stats &st = Livestock::stats(kind());
	const GLFix speed = GLFix(st.wander_speed);

	if(hurt_time > 0)
		--hurt_time;
	if(hurt_resistant > 0)
		--hurt_resistant;

	// Burning: one point of damage a second, like vanilla, for as long as the
	// ticks last. Fire Aspect is what starts it (applyMeleeDamage).
	if(fire_ticks > 0 && health > 0)
	{
		--fire_ticks;
		if((fire_ticks % 20) == 0)
			health -= Survival::FireDamage;
	}

	if(flee_timer > 0)
		--flee_timer;
	if(love_timer > 0)
		--love_timer;
	else if(love_timer < 0)
		++love_timer;
	if(age < 0)
		++age;

	++ticks_alive;
	const bool dead = health <= 0;

	if(dead && !loot_spawned)
	{
		loot_spawned = true;
		// Looting adds one more roll of the drop table per level, which is what
		// vanilla does with it.
		const int rolls = 1 + Enchanting::lootingExtraDrops(killing_looting);
		for(int roll = 0; roll < rolls; ++roll)
		for(unsigned int i = 0; i < st.drop_count; ++i)
		{
			const uint16_t stack = st.drop_stack[i];
			if(stack == 0)
				continue;
			unsigned int count = st.drop_min[i];
			if(st.drop_max[i] > st.drop_min[i])
				count += static_cast<unsigned int>(rand() % (st.drop_max[i] - st.drop_min[i] + 1));
			if(count > 0)
				spawnWorldDrop(x, y, z, stack, count);
		}

		// Killing an animal is worth experience, and a baby is not worth any (the
		// rule is survival.h's table, so the mods here cannot disagree with it).
		if(!isBaby())
			world_task.addExperience(Survival::xpFromMob(Survival::XpMob::Passive));
	}

	if(dead)
	{
		if(death_time <= 20)
			++death_time;
		vx *= GLFix(0.92f);
		vz *= GLFix(0.92f);
	}
	else
	{
		if(!dead && (ticks_alive % st.idle_interval) == 0)
			GameAudio::mobSound(audioKind(kind()), false, blocksToPlayer(x, z));

		if(flee_timer > 0)
		{
			// Run straight away from the player at an increased speed.
			const GLFix away_x = x - world_task.x;
			const GLFix away_z = z - world_task.z;
			yaw = angleFromVector(away_x, away_z);
			const GLFix run = speed * 5 / 2;
			vx = fast_sin(yaw) * run;
			vz = fast_cos(yaw) * run;
			dir_timer = 20;
		}
		else if(--dir_timer <= 0)
		{
			// Idle or wander: most of the time stand still and graze.
			if((rand() % 8) < 3)
			{
				vx = 0;
				vz = 0;
			}
			else
			{
				yaw = GLFix(rand() % 360);
				vx = fast_sin(yaw) * speed;
				vz = fast_cos(yaw) * speed;
			}
			dir_timer = 40 + rand() % 80;
		}
	}

	const GLFix old_x = x;
	const GLFix old_z = z;

	// Axis-separated AABB sweep (same approach as the original chicken mob).
	if(!world.intersect(aabb))
	{
		AABB moved = aabb;
		moved.low_x += vx;
		moved.high_x += vx;
		if(!world.intersect(moved))
		{
			x += vx;
			aabb = moved;
		}
		else
		{
			if(on_ground)
				vy = GLFix(40); // step/hop over a one-block obstacle
			vx = 0;
			dir_timer = 0;
		}

		moved = aabb;
		moved.low_z += vz;
		moved.high_z += vz;
		if(!world.intersect(moved))
		{
			z += vz;
			aabb = moved;
		}
		else
		{
			if(on_ground)
				vy = GLFix(40);
			vz = 0;
			dir_timer = 0;
		}

		AABB moved_y = aabb;
		moved_y.low_y += vy;
		moved_y.high_y += vy;
		if(!world.intersect(moved_y))
		{
			y += vy;
			aabb = moved_y;
			on_ground = false;
		}
		else
		{
			if(vy < GLFix(0))
				on_ground = true;
			vy = 0;
		}
		vy -= GLFix(5);
	}

	const GLFix w = GLFix(st.width);
	aabb = { x - w / 2, y, z - w / 2, x + w / 2, y + GLFix(st.height), z + w / 2 };

	if(dead)
	{
		swing_intensity *= GLFix(0.85f);
		return;
	}

	const GLFix dx = x - old_x;
	const GLFix dz = z - old_z;
	GLFix target_amp = GLFix(std::sqrt(static_cast<float>(dx * dx + dz * dz))) * GLFix(0.33f);
	if(target_amp > GLFix(1))
		target_amp = GLFix(1);
	swing_intensity += (target_amp - swing_intensity) * GLFix(0.4f);

	const GLFix horizontal_speed = GLFix(std::sqrt(static_cast<float>(vx * vx + vz * vz)));
	if(horizontal_speed > GLFix(0))
	{
		walk_timer += horizontal_speed;
		walk_timer.normaliseAngle();
	}
}

void LivestockEntity::render() const
{
	if(health <= 0 && death_time > 20)
		return;

	if(hurt_time > 0)
	{
		const GLFix t = GLFix(hurt_time) / GLFix(10);
		nglSetTextureModulate(GLFix(1), GLFix(1) - t * GLFix(0.52f), GLFix(1) - t * GLFix(0.48f));
	}
	else if(fire_ticks > 0 && health > 0)
	{
		// Burning animals are drawn glowing orange, which is the only part of the
		// vanilla look a texture modulate can carry.
		nglSetTextureModulate(GLFix(1), GLFix(0.55f), GLFix(0.25f));
	}

	GLFix render_yaw = yaw + GLFix(180);
	render_yaw.normaliseAngle();

	GLFix t = walk_timer;
	t.normaliseAngle();
	const GLFix cos_t = fast_cos(t);

	glPushMatrix();
	glTranslatef(x, y, z);
	nglRotateY(render_yaw);

	if(health <= 0 && death_time > 0)
	{
		float df = static_cast<float>(death_time - 1) / 20.0f * 1.6f;
		if(df < 0.f)
			df = 0.f;
		float f = std::sqrt(df);
		if(f > 1.f)
			f = 1.f;
		nglRotateZ(GLFix(f * 90.0f));
	}

	if(kind() == Livestock::Species::Chicken)
	{
		// --- original bespoke chicken model ---------------------------------
		const GLFix S = GLFix(BLOCK_SIZE) / GLFix(16);
		const GLFix neg_cos_t = -cos_t;
		GLFix rl = cos_t * GLFix(55) * swing_intensity;
		GLFix ll = neg_cos_t * GLFix(55) * swing_intensity;
		rl.normaliseAngle();
		ll.normaliseAngle();

		GLFix wing_z = fast_sin(GLFix(static_cast<int>((ticks_alive * 11) % 360))) * GLFix(32);
		GLFix wing_z_neg = -wing_z;
		wing_z.normaliseAngle();
		wing_z_neg.normaliseAngle();

		auto pivotY = [&](GLFix mc_py) -> GLFix {
			return (GLFix(24) - mc_py) * S;
		};

		glPushMatrix();
		glTranslatef(0, pivotY(GLFix(15)), GLFix(-4) * S);
		drawQuadBox(GLFix(-2) * S, GLFix(0), GLFix(-2) * S,
		            GLFix(4) * S, GLFix(6) * S, GLFix(3) * S,
		            0, 0, 4, 6, 3);
		drawQuadBox(GLFix(-2) * S, GLFix(2) * S, GLFix(-4) * S,
		            GLFix(4) * S, GLFix(2) * S, GLFix(2) * S,
		            14, 0, 4, 2, 2);
		drawQuadBox(GLFix(-1) * S, GLFix(0), GLFix(-3) * S,
		            GLFix(2) * S, GLFix(2) * S, GLFix(2) * S,
		            14, 4, 2, 2, 2);
		glPopMatrix();

		glPushMatrix();
		glTranslatef(0, pivotY(GLFix(16)), 0);
		nglRotateX(GLFix(90));
		drawQuadBox(GLFix(-3) * S, GLFix(-4) * S, GLFix(-3) * S,
		            GLFix(6) * S, GLFix(8) * S, GLFix(6) * S,
		            0, 9, 6, 8, 6);
		glPopMatrix();

		glPushMatrix();
		glTranslatef(GLFix(-2) * S, pivotY(GLFix(19)), GLFix(1) * S);
		nglRotateX(rl);
		drawQuadBox(GLFix(-1) * S, GLFix(-5) * S, GLFix(-3) * S,
		            GLFix(3) * S, GLFix(5) * S, GLFix(3) * S,
		            26, 0, 3, 5, 3);
		glPopMatrix();

		glPushMatrix();
		glTranslatef(GLFix(1) * S, pivotY(GLFix(19)), GLFix(1) * S);
		nglRotateX(ll);
		drawQuadBox(GLFix(-1) * S, GLFix(-5) * S, GLFix(-3) * S,
		            GLFix(3) * S, GLFix(5) * S, GLFix(3) * S,
		            26, 0, 3, 5, 3, true);
		glPopMatrix();

		glPushMatrix();
		glTranslatef(GLFix(-4) * S, pivotY(GLFix(13)), 0);
		nglRotateZ(wing_z);
		drawQuadBox(0, GLFix(-4) * S, GLFix(-3) * S,
		            GLFix(1) * S, GLFix(4) * S, GLFix(6) * S,
		            24, 13, 1, 4, 6);
		glPopMatrix();

		glPushMatrix();
		glTranslatef(GLFix(4) * S, pivotY(GLFix(13)), 0);
		nglRotateZ(wing_z_neg);
		drawQuadBox(GLFix(-1) * S, GLFix(-4) * S, GLFix(-3) * S,
		            GLFix(1) * S, GLFix(4) * S, GLFix(6) * S,
		            24, 13, 1, 4, 6, true);
		glPopMatrix();

		glPopMatrix();
		nglResetTextureModulate();
		return;
	}

	// --- the vanilla model (cow / pig / sheep / horse) -----------------------
	const Mob::MobModel &m = Livestock::model(kind());
	const Livestock::Stats &st = Livestock::stats(kind());

	if(!m.empty())
	{
		GLFix S = GLFix(BLOCK_SIZE) / GLFix(16) * static_cast<int>(st.render_scale) / 100;
		if(isBaby())
			S = S * BabyScalePercent / 100;

		// Vanilla limb swing: cos(limbSwing * 0.6662) * 1.4, with the diagonal
		// legs in phase. Mob::draw picks the sign per leg from the part's pose.
		GLFix swing = cos_t * GLFix(35) * swing_intensity;
		swing.normaliseAngle();

		// The body already carries the mob's facing, so the head leads it by 0.
		Mob::draw(m, S, swing, GLFix(0));
	}

	glPopMatrix();
	nglResetTextureModulate();
}

// ---------------------------------------------------------------------------
// world-facing API
// ---------------------------------------------------------------------------

void clearLivestockEntities()
{
	livestock_entities.clear();
}

void initLivestockEntities()
{
	livestock_entities.clear();
	// Chunks are not generated yet at reset time, so the initial attempts below
	// normally find nothing. Prime the timer so the first real update (which runs
	// after the world loads its chunks) spawns straight away.
	spawn_timer = SpawnFillInterval;

	const unsigned int target = Livestock::maxEntities() / 2;
	for(unsigned int i = 0; i < target; ++i)
		spawnOne();
}

void updateLivestockEntities()
{
	for(auto it = livestock_entities.begin(); it != livestock_entities.end();)
	{
		it->update();
		const bool expired = it->health <= 0 && it->death_time > 20;
		const bool far_away = it->love_timer == 0
			&& blocksToPlayer(it->x, it->z) > static_cast<int>(Livestock::despawnDistanceBlocks());
		if(expired || far_away)
			it = livestock_entities.erase(it);
		else
			++it;
	}

	// Simple breeding: two adults in love close together make a baby.
	unsigned int pending = 0;
	GLFix baby_x[PendingBabies], baby_y[PendingBabies], baby_z[PendingBabies];
	uint8_t baby_species[PendingBabies];
	for(unsigned int i = 0; i < livestock_entities.size() && pending < PendingBabies; ++i)
	{
		LivestockEntity &a = livestock_entities[i];
		if(!a.inLove() || a.isBaby())
			continue;
		for(unsigned int j = i + 1; j < livestock_entities.size(); ++j)
		{
			LivestockEntity &b = livestock_entities[j];
			if(!b.inLove() || b.isBaby() || b.species != a.species)
				continue;
			if(absFix(a.x - b.x) > GLFix(2 * BLOCK_SIZE) || absFix(a.z - b.z) > GLFix(2 * BLOCK_SIZE))
				continue;

			baby_x[pending] = (a.x + b.x) / 2;
			baby_y[pending] = (a.y + b.y) / 2;
			baby_z[pending] = (a.z + b.z) / 2;
			baby_species[pending] = a.species;
			++pending;

			a.love_timer = BreedCooldownTicks;
			b.love_timer = BreedCooldownTicks;
			break;
		}
	}
	for(unsigned int i = 0; i < pending; ++i)
	{
		if(livestock_entities.size() >= Livestock::maxEntities())
			break;
		livestock_entities.emplace_back(static_cast<Livestock::Species>(baby_species[i]),
		                                baby_x[i], baby_y[i], baby_z[i], true);
	}

	// Procedural, biome-weighted spawning around the player. Fill quickly at
	// first, then settle into a slow ambient rate.
	const unsigned int interval = (livestock_entities.size() * 2 < Livestock::maxEntities())
		? SpawnFillInterval : SpawnSteadyInterval;
	if(++spawn_timer >= interval)
	{
		spawn_timer = 0;
		spawnOne();
	}
}

void renderLivestockEntities()
{
	if(livestock_entities.empty())
		return;

	for(unsigned int s = 0; s < SpeciesCount; ++s)
	{
		bool any = false;
		for(const LivestockEntity &e : livestock_entities)
		{
			if(e.species == s && blocksToPlayer(e.x, e.z) <= 48)
			{
				any = true;
				break;
			}
		}
		if(!any)
			continue;

		glBindTexture(textureFor(static_cast<Livestock::Species>(s)));
		glBegin(GL_QUADS);
		for(const LivestockEntity &e : livestock_entities)
		{
			if(e.species == s && blocksToPlayer(e.x, e.z) <= 48)
				e.render();
		}
		glEnd();
	}
}

unsigned int livestockCount() { return livestock_entities.size(); }

LivestockEntity *livestockRayHit(GLFix ox, GLFix oy, GLFix oz, GLFix dx, GLFix dy, GLFix dz, GLFix &dist)
{
	LivestockEntity *best = nullptr;
	GLFix best_dist = GLFix::maxValue();
	for(LivestockEntity &e : livestock_entities)
	{
		if(!e.isAliveMob())
			continue;
		GLFix hit_dist;
		if(e.aabb.intersectsRay(ox, oy, oz, dx, dy, dz, hit_dist) == AABB::NONE)
			continue;
		if(hit_dist < GLFix(0))
			continue;
		if(hit_dist < best_dist)
		{
			best_dist = hit_dist;
			best = &e;
		}
	}
	dist = best_dist;
	return best;
}

bool livestockTryInteract(GLFix ox, GLFix oy, GLFix oz, GLFix dx, GLFix dy, GLFix dz, uint8_t held_item)
{
	if(!isBreedingFood(held_item))
		return false;

	GLFix dist;
	LivestockEntity *hit = livestockRayHit(ox, oy, oz, dx, dy, dz, dist);
	if(hit == nullptr)
		return false;

	if(hit->isBaby() || hit->love_timer != 0)
		return false;

	hit->feed();
	return true;
}
