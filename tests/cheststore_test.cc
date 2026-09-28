// Host tests for the chest storage (cheststore.cpp).
//
// A chest is a block entity: the world only remembers BLOCK_CHEST and this module
// owns the contents, including the double chest (two chests sharing one 54-slot
// container) and the rule that breaking one half drops that half's items and
// leaves the other half a single chest. The module is engine-free, so all of it
// can be driven here, together with the save-file round trip.
//
// Build and run with `make -C tests`.

#include "cheststore.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

using namespace ChestStore;

static BLOCK_WDATA block(int id, int data = 0)
{
    return getBLOCKWDATA(static_cast<BLOCK>(id), static_cast<uint8_t>(data));
}

static BLOCK_WDATA item(int id)
{
    return getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(id));
}

static int countOf(const Stack *stacks, int n, BLOCK_WDATA wanted)
{
    int total = 0;
    for(int i = 0; i < n; ++i)
        if(stacks[i].block == wanted)
            total += static_cast<int>(stacks[i].count);
    return total;
}

static void test_place_and_fill()
{
    clear();
    CHECK(chestCount() == 0);
    CHECK(!exists(4, 12, 4));
    CHECK(slotCountOf(4, 12, 4) == 0);

    place(4, 12, 4);
    CHECK(exists(4, 12, 4));
    CHECK(chestCount() == 1);
    CHECK(slotCountOf(4, 12, 4) == SlotCount);
    CHECK(!isLarge(4, 12, 4));

    // Placing again (reopening, or a save that already knew the chest) must not
    // create a second container.
    place(4, 12, 4);
    CHECK(chestCount() == 1);

    // Merging into the same slot before using an empty one, like a hopper.
    CHECK(addStack(4, 12, 4, block(BLOCK_DIRT), 10));
    CHECK(addStack(4, 12, 4, block(BLOCK_DIRT), 5));
    CHECK(itemCount(4, 12, 4) == 15);
    CHECK(stackAt(4, 12, 4, 0).count == 15);
    CHECK(stackAt(4, 12, 4, 0).block == block(BLOCK_DIRT));
    CHECK(stackAt(4, 12, 4, 1).count == 0);

    // A different item takes the next empty slot.
    CHECK(addStack(4, 12, 4, block(BLOCK_STONE), 64));
    CHECK(stackAt(4, 12, 4, 1).block == block(BLOCK_STONE));

    // Damaged tools never merge and never stack, even when they are identical.
    CHECK(addStack(4, 12, 4, item(96), 1, 10));
    CHECK(addStack(4, 12, 4, item(96), 1, 11));
    CHECK(stackAt(4, 12, 4, 2).count == 1);
    CHECK(stackAt(4, 12, 4, 3).count == 1);
    CHECK(stackAt(4, 12, 4, 2).damage == 10);
    CHECK(stackAt(4, 12, 4, 3).damage == 11);

    // Two identical *undamaged* pickaxes are still two stacks: a pickaxe does not
    // stack, so the second one must not be folded into the first.
    CHECK(addStack(4, 12, 4, item(96), 1, 0));
    CHECK(addStack(4, 12, 4, item(96), 1, 0));
    CHECK(stackAt(4, 12, 4, 4).count == 1);
    CHECK(stackAt(4, 12, 4, 5).count == 1);
}

static void test_full_chest()
{
    clear();
    place(0, 0, 0);

    // One full stack per slot: a chest slot holds 64, so 27 of them fill it.
    for(int i = 0; i < SlotCount; ++i)
    {
        CHECK(addStack(0, 0, 0, block(BLOCK_STONE), 64));
        CHECK(stackAt(0, 0, 0, i).count == 64);
    }

    // Every slot is taken by a full stack of stone, so nothing else fits.
    CHECK(!addStack(0, 0, 0, block(BLOCK_DIRT), 1));
    CHECK(stackAt(0, 0, 0, SlotCount - 1).block == block(BLOCK_STONE));

    // Freeing a slot makes room again.
    setStackAt(0, 0, 0, 3, Stack());
    CHECK(addStack(0, 0, 0, block(BLOCK_DIRT), 7));
    CHECK(stackAt(0, 0, 0, 3).block == block(BLOCK_DIRT));

    // A chest that does not exist accepts nothing, so a caller cannot silently
    // lose an item into a chest that was already broken.
    CHECK(!addStack(9, 9, 9, block(BLOCK_DIRT), 1));

    // Out-of-range reads and writes are ignored rather than corrupting memory.
    CHECK(stackAt(0, 0, 0, -1).count == 0);
    CHECK(stackAt(0, 0, 0, SlotCount).count == 0);
    setStackAt(0, 0, 0, 999, Stack());
}

static void test_single_chest_break()
{
    clear();
    place(2, 20, 3);
    addStack(2, 20, 3, block(BLOCK_DIRT), 12);
    addStack(2, 20, 3, item(6), 4, 0); // coal

    Stack dropped[LargeSlotCount];
    const int n = remove(2, 20, 3, dropped, LargeSlotCount);
    CHECK(n == 2);
    CHECK(countOf(dropped, n, block(BLOCK_DIRT)) == 12);
    CHECK(countOf(dropped, n, item(6)) == 4);
    CHECK(!exists(2, 20, 3));
    CHECK(chestCount() == 0);

    // Breaking a chest that is not there yields nothing.
    CHECK(remove(2, 20, 3, dropped, LargeSlotCount) == 0);

    // Empty slots are not reported as dropped items.
    clear();
    place(1, 1, 1);
    addStack(1, 1, 1, block(BLOCK_SAND), 1);
    setStackAt(1, 1, 1, 5, Stack());
    CHECK(remove(1, 1, 1, dropped, LargeSlotCount) == 1);

    // A caller with a small buffer still learns how much was in the chest, and
    // gets as much as fits.
    clear();
    place(1, 1, 1);
    addStack(1, 1, 1, block(BLOCK_STONE), 1);
    addStack(1, 1, 1, block(BLOCK_SAND), 1);
    addStack(1, 1, 1, block(BLOCK_DIRT), 1);
    Stack small[2];
    CHECK(remove(1, 1, 1, small, 2) == 3);
    CHECK(small[0].block == block(BLOCK_STONE));
    CHECK(small[1].block == block(BLOCK_SAND));
}

static void test_double_chest()
{
    clear();
    place(10, 5, 10);
    place(11, 5, 10);
    // link(chest just placed, chest that was already there): the one that was
    // already there keeps slots 0..26.
    CHECK(link(11, 5, 10, 10, 5, 10));

    // Both halves see the same 54-slot container.
    CHECK(slotCountOf(10, 5, 10) == LargeSlotCount);
    CHECK(slotCountOf(11, 5, 10) == LargeSlotCount);
    CHECK(isLarge(10, 5, 10));
    CHECK(isLarge(11, 5, 10));
    CHECK(chestCount() == 1);

    // Whatever half put an item in, both halves address the same slots: 0..26 are
    // the chest at (10,5,10) and 27..53 the one at (11,5,10).
    addStack(10, 5, 10, block(BLOCK_DIRT), 3);
    addStack(11, 5, 10, block(BLOCK_SAND), 5);
    CHECK(stackAt(10, 5, 10, 0).block == block(BLOCK_DIRT));
    CHECK(stackAt(10, 5, 10, SlotCount).block == block(BLOCK_SAND));
    CHECK(stackAt(11, 5, 10, SlotCount).block == block(BLOCK_SAND));
    CHECK(stackAt(11, 5, 10, 0).block == block(BLOCK_DIRT));
    CHECK(itemCount(11, 5, 10) == 8);

    // A full left half spills into the right half rather than reporting failure:
    // slots 1..26 take 26 stacks of stone, and the next one lands past slot 27.
    for(int i = 1; i < SlotCount; ++i)
        addStack(10, 5, 10, block(BLOCK_STONE), 64);
    CHECK(stackAt(10, 5, 10, SlotCount - 1).count == 64);
    CHECK(addStack(10, 5, 10, block(BLOCK_STONE), 64));
    CHECK(stackAt(10, 5, 10, SlotCount + 1).block == block(BLOCK_STONE));
    CHECK(stackAt(10, 5, 10, SlotCount).block == block(BLOCK_SAND));

    // Emptying the whole container, the way a chest that is broken and put back
    // (or an emptied double chest) ends up: both halves must go.
    for(int i = 0; i < LargeSlotCount; ++i)
        setStackAt(10, 5, 10, i, Stack());
    CHECK(itemCount(10, 5, 10) == 0);
    CHECK(stackAt(10, 5, 10, 0).count == 0);
    CHECK(stackAt(11, 5, 10, SlotCount).count == 0);

    // A chest cannot be paired with itself or with a chest that is already paired.
    CHECK(!link(10, 5, 10, 10, 5, 10));
    CHECK(!link(10, 5, 10, 99, 5, 99));

    clear();

    // Breaking the *left* half drops only the left half's items and leaves the
    // right half standing as a single chest with its own items.
    place(0, 6, 0);
    place(1, 6, 0);
    CHECK(link(1, 6, 0, 0, 6, 0)); // placed right-hand first: pairing must not care
    addStack(0, 6, 0, block(BLOCK_DIRT), 2);   // slot 0, the left half
    addStack(0, 6, 0, block(BLOCK_STONE), 1);  // slot 1
    addStack(1, 6, 0, block(BLOCK_SAND), 9);   // goes to slot 27+: the right half

    Stack dropped[LargeSlotCount];
    const int n = remove(0, 6, 0, dropped, LargeSlotCount);
    CHECK(n == 2);
    CHECK(countOf(dropped, n, block(BLOCK_DIRT)) == 2);
    CHECK(countOf(dropped, n, block(BLOCK_STONE)) == 1);
    CHECK(countOf(dropped, n, block(BLOCK_SAND)) == 0);

    CHECK(exists(1, 6, 0));
    CHECK(slotCountOf(1, 6, 0) == SlotCount);
    CHECK(!isLarge(1, 6, 0));
    CHECK(stackAt(1, 6, 0, 0).block == block(BLOCK_SAND));
    CHECK(itemCount(1, 6, 0) == 9);

    // ...and the reverse: breaking the right half keeps the left half's items.
    clear();
    place(0, 6, 0);
    place(1, 6, 0);
    CHECK(link(0, 6, 0, 1, 6, 0));
    addStack(0, 6, 0, block(BLOCK_DIRT), 4);
    addStack(1, 6, 0, block(BLOCK_SAND), 7);
    const int m = remove(1, 6, 0, dropped, LargeSlotCount);
    CHECK(m == 1);
    CHECK(countOf(dropped, m, block(BLOCK_SAND)) == 7);
    CHECK(exists(0, 6, 0));
    CHECK(slotCountOf(0, 6, 0) == SlotCount);
    CHECK(itemCount(0, 6, 0) == 4);
    CHECK(stackAt(0, 6, 0, 0).block == block(BLOCK_DIRT));
}

static void test_serialisation_round_trip()
{
    clear();
    place(3, 4, 5);
    addStack(3, 4, 5, block(BLOCK_DIRT), 12);
    addStack(3, 4, 5, item(96), 1, 27); // a half-used pickaxe keeps its damage
    place(-7, 8, 9);
    place(-6, 8, 9);
    CHECK(link(-6, 8, 9, -7, 8, 9));
    addStack(-7, 8, 9, block(BLOCK_DIAMOND), 3);
    addStack(-6, 8, 9, block(BLOCK_GOLD), 2);

    const size_t needed = serializedSize();
    CHECK(needed == sizeof(uint32_t) + 2 * (sizeof(int32_t) * 7 + 54 * 8));

    unsigned char buffer[4096];
    CHECK(needed <= sizeof(buffer));
    const size_t written = serialize(buffer, sizeof(buffer));
    CHECK(written == needed);

    // A buffer that is too small must fail rather than write past the end.
    CHECK(serialize(buffer, needed - 1) == 0);

    clear();
    CHECK(chestCount() == 0);

    CHECK(deserialize(buffer, written));
    CHECK(chestCount() == 2);

    CHECK(exists(3, 4, 5));
    CHECK(stackAt(3, 4, 5, 0).count == 12);
    CHECK(stackAt(3, 4, 5, 0).block == block(BLOCK_DIRT));
    CHECK(stackAt(3, 4, 5, 1).block == item(96));
    CHECK(stackAt(3, 4, 5, 1).damage == 27);

    CHECK(exists(-7, 8, 9));
    CHECK(exists(-6, 8, 9));
    CHECK(slotCountOf(-7, 8, 9) == LargeSlotCount);
    CHECK(stackAt(-7, 8, 9, 0).block == block(BLOCK_DIAMOND));
    CHECK(stackAt(-7, 8, 9, SlotCount).block == block(BLOCK_GOLD));
    CHECK(itemCount(-6, 8, 9) == 5);

    // A second round trip is byte-identical, so saving twice cannot drift.
    unsigned char again[4096];
    const size_t written_again = serialize(again, sizeof(again));
    CHECK(written_again == written);
    CHECK(memcmp(buffer, again, written) == 0);

    // Truncated and empty input is refused and leaves the store empty rather than
    // half-loaded.
    CHECK(!deserialize(buffer, written - 8));
    CHECK(!deserialize(buffer, 0));
    CHECK(!deserialize(nullptr, 100));
    CHECK(chestCount() == 0);

    // A record with a nonsense slot count is read as a single chest.
    clear();
    CHECK(deserialize(buffer, written));
    const std::vector<Chest> &records = all();
    CHECK(records.size() == 2);
    CHECK(records[0].x == 3 && records[0].y == 4 && records[0].z == 5);
    CHECK(records[1].slot_count == LargeSlotCount);

    // The store is shared global state, so leave it empty for the next test.
    clear();
    CHECK(chestCount() == 0);
    CHECK(!exists(3, 4, 5));
}

static void test_clear_and_item_count()
{
    clear();
    place(1, 2, 3);
    place(5, 6, 7);
    CHECK(chestCount() == 2);
    addStack(1, 2, 3, block(BLOCK_DIRT), 64);
    addStack(1, 2, 3, block(BLOCK_DIRT), 64);
    CHECK(itemCount(1, 2, 3) == 128);
    CHECK(itemCount(5, 6, 7) == 0);
    CHECK(itemCount(9, 9, 9) == 0);

    clear();
    CHECK(chestCount() == 0);
    CHECK(itemCount(1, 2, 3) == 0);
}

int main()
{
    test_place_and_fill();
    test_full_chest();
    test_single_chest_break();
    test_double_chest();
    test_serialisation_round_trip();
    test_clear_and_item_count();

    printf("cheststore_test: %d checks, %d failures\n", checks, failures);

    clear();
    return failures == 0 ? 0 : 1;
}
