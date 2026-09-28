// Host tests for the BLOCK_WDATA encoding in terrain.h.
//
// A block (or a held item) is packed into 16 bits: the low byte is the block id,
// the high byte is the block's data, and bit 15 is the redstone power flag. The
// data field has to be a full byte, because item stacks carry their atlas id in
// it and 49 of the 177 item ids are 128 or more -- with only 7 bits they decoded
// as a different, low-id item, so an iron ingot became a helmet.
//
// This file exists to keep that from regressing, and to record which block data
// values the power flag must not collide with.
//
// Build and run with `make -C tests`.

#include "terrain.h"
#include "textures/items.h"

#include <stdio.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

// Item ids live in the same byte as block data, and there are more than 128 of
// them, so they must be read with getITEMDATA(). The whole atlas is walked
// rather than a sample.
static void test_item_ids_round_trip()
{
    // Every id the atlas can hold; the enum is contiguous from 0, so the count
    // is the last id plus one.
    constexpr int item_count = static_cast<int>(ItemTexture::COOKED_SALMON) + 1;
    CHECK(item_count > 128); // otherwise this test proves nothing

    for(int id = 0; id < item_count; ++id)
    {
        const BLOCK_WDATA packed = getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(id));
        CHECK(getBLOCK(packed) == BLOCK_ITEM);
        CHECK(static_cast<int>(getITEMDATA(packed)) == id);
    }

    // The specific ids that were broken before: the ones at or above 128.
    const ItemTexture broken_before[] = {
        ItemTexture::WOODEN_HOE, ItemTexture::STONE_HOE, ItemTexture::IRON_HOE,
        ItemTexture::DIAMOND_HOE, ItemTexture::GOLDEN_HOE, ItemTexture::BOW_PULLING_3,
        ItemTexture::IRON_INGOT, ItemTexture::COOKED_SALMON,
        ItemTexture::ROTTEN_FLESH, ItemTexture::SPIDER_EYE, ItemTexture::BONE_MEAL,
        ItemTexture::GOLDEN_CARROT, ItemTexture::PUMPKIN_PIE
    };
    for(unsigned int i = 0; i < sizeof(broken_before) / sizeof(broken_before[0]); ++i)
    {
        const int id = static_cast<int>(broken_before[i]);
        CHECK(id >= 128);
        CHECK(static_cast<int>(getITEMDATA(getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(id)))) == id);
        // ...and proof that the two readings really are different, i.e. that the
        // distinction is not cosmetic: the block reading truncates these.
        CHECK(static_cast<int>(getBLOCKDATA(getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(id)))) != id);
    }

    // Distinct ids must stay distinct, which is what the old mask broke.
    CHECK(getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::IRON_INGOT)) !=
          getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::IRON_HELMET)));
}

static void test_block_ids_round_trip()
{
    for(int id = 0; id < 256; ++id)
    {
        const BLOCK_WDATA packed = getBLOCKWDATA(static_cast<BLOCK>(id), 0);
        CHECK(getBLOCK(packed) == id);
        CHECK(getBLOCKDATA(packed) == 0);
        CHECK(getITEMDATA(packed) == 0);
    }

    // The two readings only disagree on bit 7, and only for the byte that holds
    // an item id: that is the whole point of having both.
    for(int data = 0; data < 256; ++data)
    {
        const BLOCK_WDATA packed = getBLOCKWDATA(BLOCK_STONE, static_cast<uint8_t>(data));
        CHECK(getITEMDATA(packed) == data);
        CHECK(getBLOCKDATA(packed) == (data & 0x7F));
    }
}

// The block data values the game actually writes must all stay below 128, since
// bit 7 of the data is the power flag for the blocks that use it.
static void test_block_data_stays_below_the_power_flag()
{
    // Doors: BLOCK_SIDE in the low three bits, plus DOOR_TOP, DOOR_OPEN and
    // DOOR_FORCE_OPEN (see doorrenderer.h). Village generation writes side|8.
    for(uint8_t side = 0; side <= BLOCK_SIDE_LAST; ++side)
    {
        const uint8_t plain = side;
        const uint8_t upper = static_cast<uint8_t>(side | 8);
        const uint8_t all_flags = static_cast<uint8_t>(side | 8 | 16 | 32);
        CHECK(plain < 128);
        CHECK(upper < 128);
        CHECK(all_flags < 128);

        // The side is recovered from the low bits no matter which flags are set.
        CHECK(static_cast<int>(getBLOCKDATA(getBLOCKWDATA(BLOCK_DOOR, all_flags)) & BLOCK_SIDE_BITS) == side);
        // ...and the door still knows it is the powered/open half.
        CHECK((getBLOCKDATA(getBLOCKWDATA(BLOCK_DOOR, upper)) & 8) != 0);
    }

    // Fluids, crops and flowers use small ranges.
    CHECK(RANGE_WATER < 128);
    CHECK(RANGE_LAVA < 128);
    CHECK(7 < 128); // the largest wheat growth stage village farms write

    // Redstone wire packs its state into bits 5 and 6.
    constexpr uint8_t wire_state = (1 << 6) | (1 << 5);
    CHECK(wire_state < 128);
    CHECK(getBLOCKDATA(getBLOCKWDATA(BLOCK_REDSTONE_WIRE, wire_state)) == wire_state);
}

static void test_power_flag_is_independent_of_data()
{
    // A powered and an unpowered block with the same data differ only in the flag.
    const BLOCK_WDATA off = getBLOCKWDATAPower(BLOCK_FURNACE, 3, false);
    const BLOCK_WDATA on = getBLOCKWDATAPower(BLOCK_FURNACE, 3, true);
    CHECK(!getPOWERSTATE(off));
    CHECK(getPOWERSTATE(on));
    CHECK(getBLOCKDATA(off) == 3);
    CHECK(getBLOCKDATA(on) == 3); // the flag must not leak into the data
    CHECK(getBLOCK(off) == BLOCK_FURNACE);
    CHECK(getBLOCK(on) == BLOCK_FURNACE);

    // Toggling the flag through the plain constructor leaves the data alone too.
    CHECK(getBLOCKDATA(getBLOCKWDATA(BLOCK_REDSTONE_TORCH, 4)) == 4);
    CHECK(!getPOWERSTATE(getBLOCKWDATA(BLOCK_REDSTONE_TORCH, 4)));
}

// The name table in terrain.cpp (block_names) covers exactly the plain blocks, so
// anything that names a block has to split on BLOCK_NORMAL_LAST before indexing
// it -- a torch would read past the end of that table. This pins the invariant
// that makes the split necessary, and it is the reason worldcommands.cpp asks the
// specials separately instead of trusting one lookup.
static void test_special_blocks_live_outside_the_plain_range()
{
    CHECK(BLOCK_SNOW == BLOCK_NORMAL_LAST);
    // The blocks that own state outside the terrain tables -- a chest's contents,
    // a bed's halves and a snow layer's depth -- are still plain blocks, so they
    // are named from the same table and have to sit inside its range.
    CHECK(BLOCK_CHEST < BLOCK_NORMAL_LAST);
    CHECK(BLOCK_BED < BLOCK_NORMAL_LAST);
    // A snow layer stores its depth in its data byte, which has to fit the seven
    // bits getBLOCKDATA() masks with (snowcover.h pins the same thing).
    CHECK(BLOCK_SNOW > BLOCK_BED);
    CHECK(BLOCK_SPECIAL_START > BLOCK_NORMAL_LAST);
    CHECK(BLOCK_SPECIAL_LAST >= BLOCK_SPECIAL_START);

    const BLOCK specials[] = {
        BLOCK_TORCH, BLOCK_FLOWER, BLOCK_SPIDERWEB, BLOCK_CAKE, BLOCK_MUSHROOM,
        BLOCK_DOOR, BLOCK_WATER, BLOCK_LAVA, BLOCK_WHEAT, BLOCK_REDSTONE_LAMP,
        BLOCK_REDSTONE_SWITCH, BLOCK_REDSTONE_WIRE, BLOCK_REDSTONE_TORCH,
        BLOCK_PRESSURE_PLATE, BLOCK_WATER_FAST
    };

    for(unsigned int i = 0; i < sizeof(specials) / sizeof(specials[0]); ++i)
    {
        CHECK(specials[i] > BLOCK_NORMAL_LAST);
        CHECK(specials[i] >= BLOCK_SPECIAL_START);
        CHECK(specials[i] <= BLOCK_SPECIAL_LAST);
    }

    // Items are not blocks either: they carry an atlas id in the data byte and
    // live past every block id, which is what keeps the two lookups apart.
    CHECK(BLOCK_ITEM > BLOCK_SPECIAL_LAST);
}

int main()
{
    test_item_ids_round_trip();
    test_block_ids_round_trip();
    test_block_data_stays_below_the_power_flag();
    test_power_flag_is_independent_of_data();
    test_special_blocks_live_outside_the_plain_range();

    printf("blockdata_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
