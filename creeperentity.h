#ifndef CREEPERENTITY_H
#define CREEPERENTITY_H

#include <vector>

#include "gl.h"
#include "aabb.h"
#include "chunk.h"

// Creeper mob: wanders, fuses and explodes near the player (no gunpowder on self-destruct).
struct CreeperEntity
{
    GLFix x, y, z;
    GLFix vx, vy, vz;
    GLFix yaw;

    GLFix walk_timer;
    GLFix swing_intensity;

    int health;
    int hurt_time;
    int hurt_resistant;
    int death_time;

    int dir_timer;
    bool on_ground;
    bool loot_spawned;

    /** Ticks the creeper is on fire for (Fire Aspect); 0 when it is not. */
    int fire_ticks;
    /** Looting level of the weapon that last hit it, rolled into the drop at death. */
    int8_t killing_looting;

    /** >0 while charging explosion; 0 when idle or after blast. */
    int fuse_timer;
    bool died_by_explosion;

    AABB aabb;

    static const GLFix WIDTH;
    static const GLFix HEIGHT;

    CreeperEntity();
    CreeperEntity(GLFix x, GLFix y, GLFix z);

    void update();
    /**
     * Player melee, with the weapon's enchantments: `knockback_steps` throws it
     * further, `looting` is remembered for the drop at death and `set_fire_ticks`
     * is Fire Aspect's burning time. All default to "a plain hit".
     */
    void applyMeleeDamage(int amount, GLFix attacker_yaw, int knockback_steps = 0,
                          int looting = 0, int set_fire_ticks = 0);
    bool isAliveMob() const { return health > 0; }

    void render() const;
};

extern std::vector<CreeperEntity> creeper_entities;

void initCreeperEntities();
void updateCreeperEntities();
void renderCreeperEntities();

#endif
