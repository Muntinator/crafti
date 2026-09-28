#ifndef LIVESTOCKENTITY_H
#define LIVESTOCKENTITY_H

#include <stdint.h>
#include <vector>

#include "gl.h"
#include "aabb.h"
#include "chunk.h"
#include "livestockspecies.h"

/**
 * Passive livestock (cow, pig, sheep, chicken, horse).
 *
 * The entity is deliberately small and allocation-free: one fixed struct in one
 * std::vector, no per-entity pointers, no pathfinding. AI is a three-state
 * machine (idle / wander / flee) with tick timers, and collision reuses the
 * proven axis-separated AABB sweep from the original chicken mob. The spawner
 * keeps the population under Livestock::maxEntities() and removes animals that
 * wander past Livestock::despawnDistanceBlocks(), which matters on the CX where
 * every entity costs both simulation and geometry time.
 */
struct LivestockEntity
{
    GLFix x, y, z;
    GLFix vx, vy, vz;
    GLFix yaw;
    GLFix walk_timer;
    GLFix swing_intensity;

    int16_t health;
    int16_t hurt_time;
    int16_t hurt_resistant;
    int16_t death_time;
    int16_t dir_timer;
    /** >0: in love and looking for a mate; <0: breeding cooldown counting up. */
    int16_t love_timer;
    int16_t flee_timer;
    /** Negative while a baby, counting up to 0 when fully grown. */
    int16_t age;
    uint16_t ticks_alive;
    /** Ticks the animal is on fire for, from Fire Aspect or from being struck. */
    int16_t fire_ticks;
    /**
     * Looting level of the weapon that last damaged it, kept so the drop table can
     * roll its extra drops when the animal dies (vanilla rolls them at the kill).
     */
    int8_t killing_looting;

    uint8_t species; ///< Livestock::Species

    bool on_ground;
    bool loot_spawned;

    AABB aabb;

    LivestockEntity();
    LivestockEntity(Livestock::Species species, GLFix px, GLFix py, GLFix pz, bool baby = false);

    void update();
    /**
     * Player melee. `knockback_steps` and `looting` are the enchantment levels on
     * the weapon that hit it (enchanting.h): the first throws the animal further,
     * the second rolls extra drops, and `set_fire_ticks` is Fire Aspect's burning
     * time. All three default to "no enchantment", which is what a punch is.
     */
    void applyMeleeDamage(int amount, GLFix attacker_yaw, int knockback_steps = 0,
                          int looting = 0, int set_fire_ticks = 0);
    void feed();

    bool isAliveMob() const { return health > 0; }
    bool isBaby() const { return age < 0; }
    bool inLove() const { return love_timer > 0; }
    Livestock::Species kind() const { return static_cast<Livestock::Species>(species); }

    /** Emits the model; the species texture must already be bound. */
    void render() const;
};

extern std::vector<LivestockEntity> livestock_entities;

/** Spawns the initial population (call on world reset). */
void initLivestockEntities();
/** One game-logic tick: AI, physics, breeding, spawning and despawning. */
void updateLivestockEntities();
/** Draws every live animal, grouped by species to minimise texture switches. */
void renderLivestockEntities();
void clearLivestockEntities();

unsigned int livestockCount();

/** Nearest animal under the crosshair ray, or nullptr. Writes the hit distance. */
LivestockEntity *livestockRayHit(GLFix ox, GLFix oy, GLFix oz, GLFix dx, GLFix dy, GLFix dz, GLFix &dist);

/**
 * Feeds a targeted animal if held_item is breeding food. Returns true when an
 * animal was interacted with, so the caller can skip block placement.
 */
bool livestockTryInteract(GLFix ox, GLFix oy, GLFix oz, GLFix dx, GLFix dy, GLFix dz, uint8_t held_item);

#endif // LIVESTOCKENTITY_H
