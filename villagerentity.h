#ifndef VILLAGERENTITY_H
#define VILLAGERENTITY_H

#include <stdint.h>
#include <vector>

#include "gl.h"
#include "aabb.h"
#include "villagegen.h"

/** The three kinds a village plans for (Village::villagerProfession). */
enum class VillagerProfession : uint8_t
{
    Farmer = 0,
    Blacksmith,
    Librarian,
    Count
};

constexpr unsigned int VillagerProfessionCount = 3;

/**
 * A village inhabitant.
 *
 * Villagers exist so the generated villages feel alive without costing much on
 * a CX:
 *
 *  - every villager is bound to the village it was spawned in (the whole
 *    Village::Plan is kept) and never wanders further than Village::Radius from
 *    the centre, so the population stays where the player can see it,
 *  - a two-phase day schedule keeps them working near their profession's spot
 *    during the day and walking home when the clock says so (there is no sky,
 *    so the phase comes from a simple global tick counter),
 *  - AI, physics and idle sounds are skipped entirely beyond
 *    villagerActivationDistanceBlocks(), which is the hibernation the CX needs
 *    once the player walks away,
 *  - villagers beyond villagerDespawnDistanceBlocks() are removed and re-spawn
 *    deterministically from the village plan when the player comes back, so
 *    nothing extra has to be written to the save file,
 *  - the global population is capped by villagerEntityLimit() (a small number
 *    on the calculator) and per village by Village::villagerBudget().
 *
 * They reuse the humanoid model and skin with a profession tint instead of a
 * bespoke skin, which keeps the sprite data at one copy.
 */
struct VillagerEntity
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
    int16_t flee_timer;
    int16_t trade_cooldown;
    uint16_t ticks_alive;
    uint16_t idle_timer;

    uint8_t profession; ///< VillagerProfession
    /** Spawn-ring index; selects the plaza spot, house and workplace. */
    uint8_t home_slot;

    bool on_ground;
    bool resting; ///< true while walking home for the night

    /** The village this villager belongs to (origin, ground level and seed). */
    Village::Plan home;

    AABB aabb;

    VillagerEntity();
    VillagerEntity(uint8_t profession, uint8_t home_slot, const Village::Plan &plan, GLFix px, GLFix py, GLFix pz);

    void update();
    void applyMeleeDamage(int amount, GLFix attacker_yaw);

    bool isAliveMob() const { return health > 0; }
    bool inTradeCooldown() const { return trade_cooldown > 0; }
    void startTradeCooldown() { trade_cooldown = TradeCooldownTicks; }

    /** Emits the model; the villager skin must already be bound. */
    void render() const;

    static constexpr int16_t TradeCooldownTicks = 200;
};

/** Outcome of a right-click on a villager, so the caller knows whether to place a block. */
enum class TradeResult : uint8_t
{
    NoTarget = 0, ///< nothing under the crosshair: fall through to block placement
    NoOffer,      ///< a villager was hit but does not want the held item
    Busy,         ///< a villager was hit but is still busy with the last trade
    Traded
};

extern std::vector<VillagerEntity> villager_entities;

void initVillagerEntities();
void updateVillagerEntities();
void renderVillagerEntities();
void clearVillagerEntities();
unsigned int villagerCount();

/** Global cap on live villagers (smaller on the calculator). */
unsigned int villagerEntityLimit();
/** Villagers beyond this many blocks are hibernated: no AI, physics or sounds. */
unsigned int villagerActivationDistanceBlocks();
/** Villagers beyond this many blocks are removed and re-spawn near the player later. */
unsigned int villagerDespawnDistanceBlocks();
/** True while the shared day clock says villagers should be inside. */
bool villagerIsResting();

VillagerEntity *villagerRayHit(GLFix ox, GLFix oy, GLFix oz, GLFix dx, GLFix dy, GLFix dz, GLFix &dist);

/**
 * Simple barter with a targeted villager. On Traded the caller consumes
 * `want_count` of the held item and adds `result_stack`/`result_count`.
 */
TradeResult villagerTryTrade(GLFix ox, GLFix oy, GLFix oz, GLFix dx, GLFix dy, GLFix dz,
                             uint16_t held_stack, unsigned int held_count,
                             unsigned int &consumed_count,
                             uint16_t &result_stack, unsigned int &result_count,
                             const char **message);

/** How many of the held item this villager wants, or 0 if it has no offer. */
unsigned int villagerWantedCount(uint8_t profession, uint16_t held_stack);

#endif // VILLAGERENTITY_H
