#ifndef CHESTSTORE_H
#define CHESTSTORE_H

#include <cstdint>
#include <cstddef>
#include <vector>

#include "terrain.h"

/**
 * What is inside the chests.
 *
 * Chests are block entities: the world only stores BLOCK_CHEST, and this module
 * owns the contents, keyed by the chest's block position. A single chest has 27
 * slots; two chests placed next to each other form a double chest that shares
 * one 54-slot storage, exactly like vanilla (the contents of the right half are
 * the upper 27 slots, so breaking one half keeps the other half's items).
 *
 * Kept free of engine state so tests/cheststore_test.cc can drive the whole
 * thing on the host, including the save/load round trip.
 */
namespace ChestStore {

constexpr int SlotCount = 27;
constexpr int LargeSlotCount = SlotCount * 2;

/** One slot of a chest. `count` is 0 for an empty slot. */
struct Stack {
    BLOCK_WDATA block = BLOCK_AIR;
    /** Item durability, for the tools and armour that have any. */
    unsigned short damage = 0;
    unsigned int count = 0;
};

struct Chest {
    /** The half that owns slots 0..26 — the one a save file lists first. */
    int x = 0, y = 0, z = 0;
    /** Second half of a double chest, or the same position for a single chest. */
    int other_x = 0, other_y = 0, other_z = 0;
    /** SlotCount or LargeSlotCount. */
    int slot_count = SlotCount;
    Stack slots[LargeSlotCount];
};

// ---- placement -------------------------------------------------------------

/**
 * Remembers a chest that was just placed. Does nothing when the position is
 * already known, so placing and re-opening the same chest is safe.
 */
void place(int x, int y, int z);
/** True when a chest storage exists at this position. */
bool exists(int x, int y, int z);

/**
 * Turns the chest at (x,y,z) and the one at (nx,ny,nz) into one double chest when
 * both are single and neither is already paired. Returns true when they paired.
 */
bool link(int x, int y, int z, int nx, int ny, int nz);

/**
 * Forgets a chest that was broken and copies the contents of the removed half
 * into `out` (up to `out_capacity` stacks), returning how many were written.
 * A single chest always yields its own 27 slots; breaking one half of a double
 * chest yields that half's 27 slots and leaves the other half a single chest
 * carrying its own items.
 */
int remove(int x, int y, int z, Stack *out, int out_capacity);

/** Slot count of the storage at this position: 0 when there is no chest. */
int slotCountOf(int x, int y, int z);
bool isLarge(int x, int y, int z);

// ---- contents --------------------------------------------------------------

Stack stackAt(int x, int y, int z, int index);
void setStackAt(int x, int y, int z, int index, Stack stack);

/**
 * Adds a stack to the chest the way a hopper or an item pickup would: merge into
 * matching stacks first, then fill empty slots. Returns false when it does not
 * fit (the caller then keeps the item in the world).
 */
bool addStack(int x, int y, int z, BLOCK_WDATA block, unsigned int count, unsigned short damage = 0);
/** Total number of items in the chest, for tests and the HUD. */
unsigned int itemCount(int x, int y, int z);

// ---- whole store -----------------------------------------------------------

void clear();
int chestCount();
/** Records in insertion order, which is the order the save file writes them in. */
const std::vector<Chest> &all();
/** Re-adds one record read from a save file (single or double chest). */
void insertRecord(const Chest &chest);

// ---- serialisation (host-tested round trip) --------------------------------

size_t serializedSize();
size_t serialize(unsigned char *out, size_t capacity);
bool deserialize(const unsigned char *in, size_t length);

} // namespace ChestStore

#endif // CHESTSTORE_H
