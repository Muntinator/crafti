#include "villagerentity.h"

#include <cmath>
#include <cstdlib>

#include "audio_manager.h"
#include "audio_sounds.h"
#include "chunk.h" // registerVillagesNearColumn
#include "fastmath.h"
#include "gl.h"
#include "textures/steve.h" // the humanoid skin, shared with nothing else
#include "terrain.h"
#include "textures/items.h"
#include "world.h"
#include "worldtask.h"

std::vector<VillagerEntity> villager_entities;

namespace
{
	constexpr GLFix VillagerWidth(77);   // ~0.6 block, same footprint as the player
	constexpr GLFix VillagerHeight(230); // ~1.8 blocks

	/** Movement speed in nGL units per tick. */
	constexpr int WalkSpeed = 4;
	/** Ticks between idle sounds while the player is close enough to hear. */
	constexpr int IdleSoundInterval = 260;
	/** Ticks a hurt villager keeps running. */
	constexpr int FleeTicks = 70;

	/** Length of the two schedule phases, in ticks (no sky, so this is the clock). */
	constexpr unsigned int WorkTicks = 1600;
	constexpr unsigned int RestTicks = 800;
	unsigned int day_timer = 0;

	/** How close the player must be for a village to be populated with villagers. */
	constexpr int SpawnSearchRadiusBlocks = 20;
	/** Spawn cadence: quick while a village is still filling up, slower afterwards. */
	constexpr unsigned int SpawnFillInterval = 30;
	constexpr unsigned int SpawnSteadyInterval = 100;
	unsigned int spawn_timer = 0;

	/** Ring of free spots on the village plaza (offsets in blocks from the origin). */
	const int spawn_offsets[8][2] = {
		{ 3, 0 }, { 0, 3 }, { -3, 0 }, { 0, -3 },
		{ 4, 4 }, { -4, 4 }, { 4, -4 }, { -4, -4 }
	};

	struct TradeOffer
	{
		uint8_t want_item;
		unsigned int want_count;
		uint8_t give_item;
		unsigned int give_count;
		const char *message;
	};

	// One offer per profession. Item ids are stored in the BLOCK_ITEM metadata and
	// are read back with getITEMDATA(), which keeps the whole byte, so ids of 128
	// or more are usable (IRON_INGOT is 145).
	static_assert(static_cast<int>(ItemTexture::COOKED_SALMON) > 127, "item ids above 127 exist");
	static_assert(static_cast<int>(ItemTexture::IRON_INGOT) > 127, "the blacksmith trade relies on a high item id");

	const TradeOffer trade_offers[VillagerProfessionCount] = {
		{ static_cast<uint8_t>(ItemTexture::WHEAT_SEEDS), 1, static_cast<uint8_t>(ItemTexture::APPLE), 1,
			"Farmer: seeds for an apple" },
		// A smith's trade, and the reason the item id needs a full byte: the ingot
		// is id 145.
		{ static_cast<uint8_t>(ItemTexture::COAL), 2, static_cast<uint8_t>(ItemTexture::IRON_INGOT), 1,
			"Blacksmith: 2 coal for an iron ingot" },
		{ static_cast<uint8_t>(ItemTexture::WHEAT_SEEDS), 3, static_cast<uint8_t>(ItemTexture::BREAD), 1,
			"Librarian: seeds for bread" }
	};
	static_assert(sizeof(trade_offers) / sizeof(trade_offers[0]) == VillagerProfessionCount, "one offer per profession");

	inline GLFix absFix(GLFix v) { return v < GLFix(0) ? -v : v; }
	inline int absI(int v) { return v < 0 ? -v : v; }

	uint16_t itemStack(uint8_t item)
	{
		return static_cast<uint16_t>((static_cast<uint16_t>(item) << 8) | BLOCK_ITEM);
	}

	/** Chebyshev distance to the player in blocks, for culling, sound and hibernation. */
	int blocksToPlayer(GLFix ax, GLFix az)
	{
		const int bx = absFix(ax - world_task.x).toInteger<int>() / BLOCK_SIZE;
		const int bz = absFix(az - world_task.z).toInteger<int>() / BLOCK_SIZE;
		return bx > bz ? bx : bz;
	}

	GLFix angleFromVector(GLFix dx, GLFix dz)
	{
		const float a = std::atan2(static_cast<float>(dx), static_cast<float>(dz)) * 57.29578f;
		GLFix result(a);
		result.normaliseAngle();
		return result;
	}

	/** World-space centre of a village, in nGL units. */
	void homeCentre(const Village::Plan &plan, GLFix &cx, GLFix &cz)
	{
		cx = GLFix(plan.origin_x * BLOCK_SIZE) + GLFix(BLOCK_SIZE / 2);
		cz = GLFix(plan.origin_z * BLOCK_SIZE) + GLFix(BLOCK_SIZE / 2);
	}

	/** Feet height of a village's ground layer, in nGL units. */
	GLFix homeGround(const Village::Plan &plan)
	{
		return GLFix((plan.ground_y + 1) * BLOCK_SIZE);
	}

	/**
	 * A spot just outside a structure, on the village side of it, so villagers do
	 * not push into walls or end up standing in a farm's water trench. `back`
	 * fans several villagers out into a short line instead of one stacked block.
	 */
	void approachPoint(const Village::Plan &plan, int centre_x, int centre_z, int back, GLFix &out_x, GLFix &out_z)
	{
		const int dx = plan.origin_x - centre_x;
		const int dz = plan.origin_z - centre_z;
		int bx = centre_x;
		int bz = centre_z;

		if(absI(dx) >= absI(dz))
			bx += dx > 0 ? back : -back;
		else
			bz += dz > 0 ? back : -back;

		out_x = GLFix(bx * BLOCK_SIZE) + GLFix(BLOCK_SIZE / 2);
		out_z = GLFix(bz * BLOCK_SIZE) + GLFix(BLOCK_SIZE / 2);
	}

	/**
	 * Where a villager of this profession works during the day. Returns false when
	 * the village has no such structure, so the caller can fall back to the plaza.
	 */
	bool workplaceFor(const Village::Plan &plan, uint8_t profession, int slot, GLFix &out_x, GLFix &out_z)
	{
		int cx = 0, cz = 0;

		switch(profession)
		{
		case static_cast<uint8_t>(VillagerProfession::Blacksmith):
			Village::shedCentre(plan, cx, cz);
			break;
		case static_cast<uint8_t>(VillagerProfession::Librarian):
		{
			const int houses = Village::houseCount(plan);
			if(houses <= 0 || !Village::houseCentre(plan, (slot + 1) % houses, cx, cz))
				return false;
			break;
		}
		default:
			if(!Village::farmCentre(plan, slot % 2, cx, cz))
				return false;
			break;
		}

		approachPoint(plan, cx, cz, 4 + (slot % 3), out_x, out_z);
		return true;
	}

	/** The doorstep of the house this villager sleeps next to, or false. */
	bool homeDoorstepFor(const Village::Plan &plan, int slot, GLFix &out_x, GLFix &out_z)
	{
		const int houses = Village::houseCount(plan);
		if(houses <= 0)
			return false;

		int cx = 0, cz = 0;
		if(!Village::houseCentre(plan, slot % houses, cx, cz))
			return false;

		approachPoint(plan, cx, cz, 3 + (slot % 2), out_x, out_z);
		return true;
	}

	/** A modest tint per profession, applied to the shared humanoid skin. */
	void professionTint(uint8_t profession, GLFix &r, GLFix &g, GLFix &b)
	{
		switch(profession)
		{
		case static_cast<uint8_t>(VillagerProfession::Blacksmith): r = GLFix(0.80f); g = GLFix(0.72f); b = GLFix(0.68f); break;
		case static_cast<uint8_t>(VillagerProfession::Librarian):  r = GLFix(1.05f); g = GLFix(1.02f); b = GLFix(1.08f); break;
		default:                                                   r = GLFix(1.00f); g = GLFix(0.88f); b = GLFix(0.62f); break;
		}
	}

	// --- humanoid model: the same box layout the player is drawn with --------

	TextureAtlasEntry skinArea(int u, int v, int w, int h)
	{
		return { static_cast<unsigned>(u), static_cast<unsigned>(u + w),
		         static_cast<unsigned>(v), static_cast<unsigned>(v + h) };
	}

	TextureAtlasEntry mirrorU(TextureAtlasEntry t)
	{
		const unsigned tmp = t.left;
		t.left = t.right;
		t.right = tmp;
		return t;
	}

	void emitQuad(
		GLFix ax, GLFix ay, GLFix az,
		GLFix bx, GLFix by, GLFix bz,
		GLFix cx, GLFix cy, GLFix cz,
		GLFix dx, GLFix dy, GLFix dz,
		const TextureAtlasEntry &tex)
	{
		const COLOR flags = TEXTURE_TRANSPARENT | TEXTURE_DRAW_BACKFACE;
		nglAddVertex({ ax, ay, az, GLFix(static_cast<int>(tex.left)),  GLFix(static_cast<int>(tex.bottom)), flags });
		nglAddVertex({ bx, by, bz, GLFix(static_cast<int>(tex.left)),  GLFix(static_cast<int>(tex.top)),    flags });
		nglAddVertex({ cx, cy, cz, GLFix(static_cast<int>(tex.right)), GLFix(static_cast<int>(tex.top)),    flags });
		nglAddVertex({ dx, dy, dz, GLFix(static_cast<int>(tex.right)), GLFix(static_cast<int>(tex.bottom)), flags });
	}

	void drawBipedBox(
		GLFix bx, GLFix by, GLFix bz,
		GLFix bw, GLFix bh, GLFix bd,
		int u0, int v0, int wp, int hp, int dp,
		bool mirror = false)
	{
		auto top = skinArea(u0 + dp, v0, wp, dp);
		auto bot = skinArea(u0 + dp + wp, v0, wp, dp);
		auto rgt = skinArea(u0, v0 + dp, dp, hp);
		auto frt = skinArea(u0 + dp, v0 + dp, wp, hp);
		auto lft = skinArea(u0 + dp + wp, v0 + dp, dp, hp);
		auto bck = skinArea(u0 + dp + wp + dp, v0 + dp, wp, hp);

		if(mirror)
		{
			top = mirrorU(top);
			bot = mirrorU(bot);
			TextureAtlasEntry tmp = mirrorU(rgt);
			rgt = mirrorU(lft);
			lft = tmp;
			frt = mirrorU(frt);
			bck = mirrorU(bck);
		}

		const GLFix x0 = bx, x1 = bx + bw;
		const GLFix y0 = by, y1 = by + bh;
		const GLFix z0 = bz, z1 = bz + bd;

		emitQuad(x0, y0, z0, x0, y1, z0, x1, y1, z0, x1, y0, z0, frt);
		emitQuad(x1, y0, z1, x1, y1, z1, x0, y1, z1, x0, y0, z1, bck);
		emitQuad(x0, y0, z1, x0, y1, z1, x0, y1, z0, x0, y0, z0, rgt);
		emitQuad(x1, y0, z0, x1, y1, z0, x1, y1, z1, x1, y0, z1, lft);
		emitQuad(x0, y1, z0, x0, y1, z1, x1, y1, z1, x1, y1, z0, top);
		emitQuad(x1, y0, z0, x1, y0, z1, x0, y0, z1, x0, y0, z0, bot);
	}

	/** Ground block Y of the village centre, used to place villagers on the plaza. */
	bool spawnSpotFor(const Village::Plan &plan, int index, int &out_bx, int &out_by, int &out_bz)
	{
		const int off = index % 8;
		const int bx = plan.origin_x + spawn_offsets[off][0];
		const int bz = plan.origin_z + spawn_offsets[off][1];

		// The plaza is at ground_y, so villagers stand one block above it. Both
		// that block and the one above must be free; unloaded chunks read as
		// stone, which conveniently rejects them here.
		const int by = plan.ground_y + 1;
		if(getBLOCK(world.getBlock(bx, by, bz)) != BLOCK_AIR)
			return false;
		if(getBLOCK(world.getBlock(bx, by + 1, bz)) != BLOCK_AIR)
			return false;

		out_bx = bx;
		out_by = by;
		out_bz = bz;
		return true;
	}

	unsigned int villagersIn(const Village::Plan &plan)
	{
		unsigned int count = 0;
		for(const VillagerEntity &e : villager_entities)
			if(e.home.cell_x == plan.cell_x && e.home.cell_z == plan.cell_z)
				++count;
		return count;
	}
}

VillagerEntity::VillagerEntity()
	: x(0), y(GLFix(World::HEIGHT * Chunk::SIZE) * BLOCK_SIZE), z(0),
	  vx(0), vy(0), vz(0), yaw(0), walk_timer(0), swing_intensity(0),
	  health(20), hurt_time(0), hurt_resistant(0), death_time(0), dir_timer(60),
	  flee_timer(0), trade_cooldown(0), ticks_alive(0), idle_timer(0),
	  profession(0), home_slot(0), on_ground(false), resting(false), fire_ticks(0)
{
	aabb = { x - VillagerWidth / 2, y, z - VillagerWidth / 2,
	         x + VillagerWidth / 2, y + VillagerHeight, z + VillagerWidth / 2 };
}

VillagerEntity::VillagerEntity(uint8_t profession_, uint8_t home_slot_, const Village::Plan &plan, GLFix px, GLFix py, GLFix pz)
	: x(px), y(py), z(pz),
	  vx(0), vy(0), vz(0), yaw(GLFix(rand() % 360)), walk_timer(0), swing_intensity(0),
	  health(20), hurt_time(0), hurt_resistant(0), death_time(0), dir_timer(rand() % 60),
	  flee_timer(0), trade_cooldown(0), ticks_alive(0), idle_timer(static_cast<uint16_t>(rand() % IdleSoundInterval)),
	  profession(profession_), home_slot(home_slot_), on_ground(false), resting(false), fire_ticks(0), home(plan)
{
	aabb = { x - VillagerWidth / 2, y, z - VillagerWidth / 2,
	         x + VillagerWidth / 2, y + VillagerHeight, z + VillagerWidth / 2 };
}

void VillagerEntity::applyMeleeDamage(int amount, GLFix attacker_yaw, int knockback_steps,
                                     int looting, int set_fire_ticks)
{
	(void)looting; // a villager drops nothing, so Looting has nothing to add to

	if(health <= 0 || hurt_resistant > 0)
		return;

	health -= amount;
	hurt_time = 10;
	hurt_resistant = 10;
	flee_timer = FleeTicks;

	if(set_fire_ticks > 0 && fire_ticks < set_fire_ticks)
		fire_ticks = static_cast<int16_t>(set_fire_ticks);

	GameAudio::mobSound(GameAudio::MobVillager, true, blocksToPlayer(x, z));

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

void VillagerEntity::update()
{
	if(hurt_time > 0)
		--hurt_time;
	if(hurt_resistant > 0)
		--hurt_resistant;

	// Burning: one point of damage a second, like vanilla (Fire Aspect starts it).
	if(fire_ticks > 0 && health > 0)
	{
		--fire_ticks;
		if((fire_ticks % 20) == 0)
			health -= Survival::FireDamage;
	}

	if(flee_timer > 0)
		--flee_timer;
	if(trade_cooldown > 0)
		--trade_cooldown;
	++ticks_alive;

	const bool dead = health <= 0;
	const bool rest_now = villagerIsResting();
	resting = rest_now;

	GLFix home_x, home_z;
	homeCentre(home, home_x, home_z);
	const GLFix ground_y = homeGround(home);

	if(!dead)
	{
		if((ticks_alive % IdleSoundInterval) == 0 || ++idle_timer >= IdleSoundInterval)
		{
			idle_timer = 0;
			GameAudio::mobSound(GameAudio::MobVillager, false, blocksToPlayer(x, z));
		}

		// Pick a destination: a fleeing villager runs from the player, a resting
		// one walks home, a working one heads for its profession's spot unless it
		// is close enough to potter around.
		GLFix target_x = home_x;
		GLFix target_z = home_z;

		if(flee_timer > 0)
		{
			target_x = x + (x - world_task.x);
			target_z = z + (z - world_task.z);
		}
		else if(rest_now)
		{
			// Night: gather at the doorstep of "their" house. The plaza is only a
			// fallback for a plan without houses (or an exhausted budget).
			if(!homeDoorstepFor(home, home_slot, target_x, target_z))
			{
				target_x = home_x;
				target_z = home_z;
			}
		}
		else if(!workplaceFor(home, profession, home_slot, target_x, target_z))
		{
			target_x = home_x;
			target_z = home_z;
		}

		// Never leave the village: outside the radius, always head back inside.
		const bool out_of_bounds = absFix(x - home_x) > GLFix(Village::Radius * BLOCK_SIZE)
			|| absFix(z - home_z) > GLFix(Village::Radius * BLOCK_SIZE);
		if(out_of_bounds)
		{
			target_x = home_x;
			target_z = home_z;
		}

		const GLFix dx = target_x - x;
		const GLFix dz = target_z - z;
		const GLFix distance = absFix(dx) + absFix(dz);

		if(flee_timer > 0)
		{
			yaw = angleFromVector(dx, dz);
			const GLFix run = GLFix(WalkSpeed * 3 / 2);
			vx = fast_sin(yaw) * run;
			vz = fast_cos(yaw) * run;
		}
		else if(distance > GLFix(2 * BLOCK_SIZE) || out_of_bounds)
		{
			yaw = angleFromVector(dx, dz);
			vx = fast_sin(yaw) * GLFix(WalkSpeed);
			vz = fast_cos(yaw) * GLFix(WalkSpeed);
			dir_timer = 20;
		}
		else if(--dir_timer <= 0)
		{
			// At the destination: mostly stand and look around.
			if((rand() % 8) < 6)
			{
				vx = 0;
				vz = 0;
				yaw = GLFix(rand() % 360);
			}
			else
			{
				yaw = GLFix(rand() % 360);
				vx = fast_sin(yaw) * GLFix(WalkSpeed / 2);
				vz = fast_cos(yaw) * GLFix(WalkSpeed / 2);
			}
			dir_timer = 40 + rand() % 80;
		}
	}

	if(dead)
	{
		if(death_time <= 20)
			++death_time;
		vx *= GLFix(0.92f);
		vz *= GLFix(0.92f);
	}

	const GLFix old_x = x;
	const GLFix old_z = z;

	// Axis-separated AABB sweep, same approach as the other mobs.
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

	aabb = { x - VillagerWidth / 2, y, z - VillagerWidth / 2,
	         x + VillagerWidth / 2, y + VillagerHeight, z + VillagerWidth / 2 };

	// Keep them standing on the plaza even if they were placed mid-air.
	if(!dead && y < ground_y)
	{
		y = ground_y;
		vy = 0;
		on_ground = true;
		aabb = { x - VillagerWidth / 2, y, z - VillagerWidth / 2,
		         x + VillagerWidth / 2, y + VillagerHeight, z + VillagerWidth / 2 };
	}

	if(dead)
	{
		swing_intensity *= GLFix(0.85f);
		return;
	}

	const GLFix ddx = x - old_x;
	const GLFix ddz = z - old_z;
	GLFix target_amp = GLFix(std::sqrt(static_cast<float>(ddx * ddx + ddz * ddz))) * GLFix(0.33f);
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

void VillagerEntity::render() const
{
	if(health <= 0 && death_time > 20)
		return;

	GLFix tint_r(1), tint_g(1), tint_b(1);
	professionTint(profession, tint_r, tint_g, tint_b);

	if(hurt_time > 0)
	{
		const GLFix t = GLFix(hurt_time) / GLFix(10);
		tint_g *= GLFix(1) - t * GLFix(0.52f);
		tint_b *= GLFix(1) - t * GLFix(0.48f);
	}
	else if(fire_ticks > 0 && health > 0)
	{
		// A burning villager glows orange; the texture modulate is the only fire
		// look this renderer can carry.
		tint_g *= GLFix(0.55f);
		tint_b *= GLFix(0.25f);
	}
	nglSetTextureModulate(tint_r, tint_g, tint_b);

	GLFix render_yaw = yaw + GLFix(180);
	render_yaw.normaliseAngle();

	glPushMatrix();
	glTranslatef(x, y + VillagerHeight / 2, z);
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

	const GLFix px = GLFix(BLOCK_SIZE) / GLFix(16);

	const GLFix head_w = GLFix(8) * px, head_h = GLFix(8) * px, head_d = GLFix(8) * px;
	const GLFix body_w = GLFix(8) * px, body_h = GLFix(12) * px, body_d = GLFix(4) * px;
	const GLFix arm_w = GLFix(4) * px, arm_h = GLFix(12) * px, arm_d = GLFix(4) * px;
	const GLFix leg_w = GLFix(4) * px, leg_h = GLFix(12) * px, leg_d = GLFix(4) * px;
	const GLFix torso_y = GLFix(0);
	const GLFix head_y = torso_y + body_h / 2 + head_h / 2;
	const GLFix leg_y = torso_y - body_h / 2 - leg_h / 2;
	const GLFix shoulder_y = torso_y + body_h / 2 - arm_h / 2;

	const GLFix swing = fast_sin(walk_timer) * GLFix(35) * swing_intensity;
	const GLFix swing_op = fast_sin(walk_timer + GLFix(180)) * GLFix(35) * swing_intensity;

	// Head with the characteristic big nose sticking out of the face.
	drawBipedBox(-head_w / 2, head_y - head_h / 2, -head_d / 2,
	             head_w, head_h, head_d,
	             0, 0, 8, 8, 8, false);

	const GLFix nose_w = GLFix(4) * px, nose_h = GLFix(4) * px, nose_d = GLFix(3) * px;
	drawBipedBox(-nose_w / 2, head_y - nose_h / 2, -head_d / 2 - nose_d,
	             nose_w, nose_h, nose_d,
	             11, 10, 4, 4, 3, false);

	// Body (a taller robe for the blacksmith and librarian would need a second
	// skin, so all three share this torso and differ only by the tint).
	drawBipedBox(-body_w / 2, torso_y - body_h / 2, -body_d / 2,
	             body_w, body_h, body_d,
	             16, 16, 8, 12, 4, false);

	glPushMatrix();
	glTranslatef(-body_w / 2 - arm_w / 2, shoulder_y, 0);
	nglRotateX(swing);
	drawBipedBox(-arm_w / 2, -arm_h / 2, -arm_d / 2,
	             arm_w, arm_h, arm_d,
	             40, 16, 4, 12, 4, false);
	glPopMatrix();

	glPushMatrix();
	glTranslatef(body_w / 2 + arm_w / 2, shoulder_y, 0);
	nglRotateX(swing_op);
	drawBipedBox(-arm_w / 2, -arm_h / 2, -arm_d / 2,
	             arm_w, arm_h, arm_d,
	             32, 48, 4, 12, 4, true);
	glPopMatrix();

	glPushMatrix();
	glTranslatef(-leg_w / 2, leg_y, 0);
	nglRotateX(swing_op);
	drawBipedBox(-leg_w / 2, -leg_h / 2, -leg_d / 2,
	             leg_w, leg_h, leg_d,
	             0, 16, 4, 12, 4, false);
	glPopMatrix();

	glPushMatrix();
	glTranslatef(leg_w / 2, leg_y, 0);
	nglRotateX(swing);
	drawBipedBox(-leg_w / 2, -leg_h / 2, -leg_d / 2,
	             leg_w, leg_h, leg_d,
	             16, 48, 4, 12, 4, true);
	glPopMatrix();

	glPopMatrix();

	nglResetTextureModulate();
}

// ---------------------------------------------------------------------------
// world-facing API
// ---------------------------------------------------------------------------

unsigned int villagerEntityLimit()
{
#ifdef _TINSPIRE
	return 8;
#else
	return 20;
#endif
}

unsigned int villagerActivationDistanceBlocks() { return 24; }

unsigned int villagerDespawnDistanceBlocks()
{
#ifdef _TINSPIRE
	return 40;
#else
	return 64;
#endif
}

bool villagerIsResting() { return day_timer >= WorkTicks; }

void clearVillagerEntities()
{
	villager_entities.clear();
}

void initVillagerEntities()
{
	villager_entities.clear();
	// Chunks are not generated at reset time, so no village is registered yet.
	// Prime the timer so the first real update populates the nearest village.
	spawn_timer = SpawnFillInterval;
	day_timer = 0;
}

void updateVillagerEntities()
{
	if(++day_timer >= WorkTicks + RestTicks)
		day_timer = 0;

	for(auto it = villager_entities.begin(); it != villager_entities.end();)
	{
		const int distance = blocksToPlayer(it->x, it->z);
		const bool expired = it->health <= 0 && it->death_time > 20;
		const bool far_away = distance > static_cast<int>(villagerDespawnDistanceBlocks());

		if(expired || far_away)
		{
			it = villager_entities.erase(it);
			continue;
		}

		// Hibernation: outside the activation radius nothing is simulated, so a
		// loaded village behind the player costs almost nothing.
		if(distance <= static_cast<int>(villagerActivationDistanceBlocks()))
			it->update();
		else
		{
			it->vx = 0;
			it->vz = 0;
			it->vy = 0;
		}
		++it;
	}

	if(villager_entities.size() >= villagerEntityLimit())
		return;

	const unsigned int interval = villager_entities.empty() ? SpawnFillInterval : SpawnSteadyInterval;
	if(++spawn_timer < interval)
		return;
	spawn_timer = 0;

	const int player_bx = positionToBlock(world_task.x.toInteger<int>());
	const int player_bz = positionToBlock(world_task.z.toInteger<int>());

	Village::Plan plans[4];
	int plan_count = Village::registeredPlansNear(player_bx, player_bz, SpawnSearchRadiusBlocks, plans, 4);
	if(plan_count == 0)
	{
		// Chunks that came from a save file never run generation, so the registry
		// can be empty even though the player is standing in a village. Resolving
		// the surrounding cells once repopulates it (and is a no-op after that).
		registerVillagesNearColumn(player_bx, player_bz);
		plan_count = Village::registeredPlansNear(player_bx, player_bz, SpawnSearchRadiusBlocks, plans, 4);
	}

	for(int i = 0; i < plan_count; ++i)
	{
		const Village::Plan &plan = plans[i];
		const unsigned int budget = static_cast<unsigned int>(Village::villagerBudget(plan));
		const unsigned int present = villagersIn(plan);
		if(present >= budget)
			continue;

		int bx = 0, by = 0, bz = 0;
		if(!spawnSpotFor(plan, static_cast<int>(present), bx, by, bz))
			continue;

		const int index = static_cast<int>(present);
		const int profession = Village::villagerProfession(plan, index);
		villager_entities.emplace_back(static_cast<uint8_t>(profession), static_cast<uint8_t>(index % 8), plan,
		                               GLFix(bx * BLOCK_SIZE) + GLFix(BLOCK_SIZE / 2),
		                               GLFix(by * BLOCK_SIZE),
		                               GLFix(bz * BLOCK_SIZE) + GLFix(BLOCK_SIZE / 2));
		break; // one arrival per interval keeps the frame cost flat
	}
}

void renderVillagerEntities()
{
	if(villager_entities.empty())
		return;

	bool any = false;
	for(const VillagerEntity &e : villager_entities)
	{
		if(blocksToPlayer(e.x, e.z) <= 48)
		{
			any = true;
			break;
		}
	}
	if(!any)
		return;

	glBindTexture(&steve_tex);
	glBegin(GL_QUADS);
	for(const VillagerEntity &e : villager_entities)
	{
		if(blocksToPlayer(e.x, e.z) <= 48)
			e.render();
	}
	glEnd();
}

unsigned int villagerCount() { return villager_entities.size(); }

VillagerEntity *villagerRayHit(GLFix ox, GLFix oy, GLFix oz, GLFix dx, GLFix dy, GLFix dz, GLFix &dist)
{
	VillagerEntity *best = nullptr;
	GLFix best_dist = GLFix::maxValue();
	for(VillagerEntity &e : villager_entities)
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

unsigned int villagerWantedCount(uint8_t profession, uint16_t held_stack)
{
	if(profession >= VillagerProfessionCount)
		return 0;
	if(getBLOCK(held_stack) != BLOCK_ITEM)
		return 0;
	if(getITEMDATA(held_stack) != trade_offers[profession].want_item)
		return 0;
	return trade_offers[profession].want_count;
}

TradeResult villagerTryTrade(GLFix ox, GLFix oy, GLFix oz, GLFix dx, GLFix dy, GLFix dz,
                             uint16_t held_stack, unsigned int held_count,
                             unsigned int &consumed_count,
                             uint16_t &result_stack, unsigned int &result_count,
                             const char **message)
{
	consumed_count = 0;
	result_stack = 0;
	result_count = 0;

	GLFix dist;
	VillagerEntity *hit = villagerRayHit(ox, oy, oz, dx, dy, dz, dist);
	if(hit == nullptr)
		return TradeResult::NoTarget;

	if(hit->inTradeCooldown())
	{
		if(message != nullptr)
			*message = "The villager is busy";
		return TradeResult::Busy;
	}

	const uint8_t profession = hit->profession;
	if(profession >= VillagerProfessionCount)
		return TradeResult::NoOffer;

	const TradeOffer &offer = trade_offers[profession];
	if(getBLOCK(held_stack) != BLOCK_ITEM || getITEMDATA(held_stack) != offer.want_item || held_count < offer.want_count)
	{
		if(message != nullptr)
			*message = "Not interested";
		return TradeResult::NoOffer;
	}

	hit->startTradeCooldown();
	// Sound ids are the raw pack ids; MobVillager already covers the idle/hurt sets.
	GameAudio::playSound(GameAudio::Sound::MobVillagerYes1);

	consumed_count = offer.want_count;
	result_stack = itemStack(offer.give_item);
	result_count = offer.give_count;
	if(message != nullptr)
		*message = offer.message;
	return TradeResult::Traded;
}
