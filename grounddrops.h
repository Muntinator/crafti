#ifndef GROUNDDROPS_H
#define GROUNDDROPS_H

#include <cstdint>

#include "gl.h"
#include "terrain.h"

/**
 * Dropped stacks lying in the world.
 *
 * World-space position in nGL units (same as entities). stack is BLOCK_ITEM or a
 * placeable block, and damage is the wear the item already has, so a half-used
 * pickaxe stays half used while it lies on the ground and after it is picked up.
 */
void spawnWorldDrop(GLFix x, GLFix y, GLFix z, BLOCK_WDATA stack, unsigned int count, unsigned short damage = 0);
/**
 * One simulation step. elapsed_ms is the real time the step covers, which is what
 * ages the drops: a delivery that is 300 ms on the calculator is 33 ms on the
 * desktop, and both must reach the five-minute lifetime at the same time.
 */
void updateGroundDrops(unsigned int elapsed_ms);
void renderGroundDrops();
void clearGroundDrops();

#endif // GROUNDDROPS_H
