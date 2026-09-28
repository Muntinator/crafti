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

    uint8_t species; ///< Livestock::Species

    bool on_ground;
    bool loot_spawned;

    AABB aabb;

    LivestockEntity();
    LivestockEntity(Livestock::Species species, GLFix px, GLFix py, GLFix pz, bool baby = false);

    void update();
    void applyMeleeDamage(int amount, GLFix attacker_yaw);
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
