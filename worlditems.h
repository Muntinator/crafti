#ifndef WORLDITEMS_H
#define WORLDITEMS_H

#include "terrain.h"

/**
 * The item side of the world: wearing the held tool, wearing armour, and the
 * chests as blocks (placing, pairing, opening and breaking one).
 *
 * These live outside worldtask.cpp because they are used from several places in
 * the interaction code, and they are the parts the world task and the inventory
 * task share.
 */

/**
 * Wears the held item by `amount`. A tool that reaches its limit breaks: the
 * slot is emptied, a message says so and a breaking sound plays. Returns true
 * when the item broke.
 */
bool wearHeldItem(int amount);

/**
 * Puts the held armour on, swapping with the piece already in that slot (which
 * goes back into the inventory). Returns false when the held item is not armour.
 */
bool tryEquipHeldArmor();

/** Remembers a chest that was just placed and pairs it with an adjacent chest. */
void placeChestAt(int x, int y, int z);

/**
 * Places a bed with its foot at (x,y,z) and its head one cell further along the
 * direction the player is looking (bed.h's facingForYaw).
 *
 * Both cells have to be free and the player must not end up standing inside
 * either of them, because half a bed is not a bed: the placement either happens
 * whole or not at all, and it reports which of the two it was. The caller spends
 * the held bed and plays the sound on success.
 */
bool tryPlaceBed(int x, int y, int z, int yaw_degrees);

/**
 * Makes sure the chest at these coordinates has a container, creating (and
 * seeding) one when it is the first time the chest has been touched. A chest the
 * world generated — a dungeon, a ruin or a temple — is filled with the loot of
 * the structure it belongs to, which is a pure function of the world seed, so an
 * unvisited chest costs neither memory nor save space.
 */
void ensureChestAt(int x, int y, int z);

/**
 * Breaks the chest at these coordinates: its contents are dropped into the world
 * as item entities and the storage is forgotten.
 */
void breakChestAt(int x, int y, int z);

/** Drops everything a chest holds without removing the storage (world reset). */
void clearChests();

#endif // WORLDITEMS_H
