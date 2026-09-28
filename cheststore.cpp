#include "cheststore.h"

#include <cstring>

#include "itemrules.h"

namespace ChestStore {

/**
 * A record is the whole container, not one half of it: slots 0..26 belong to the
 * chest at (x,y,z) and, in a double chest, slots 27..53 belong to the chest at
 * (other_x,other_y,other_z). The container GUI therefore addresses slots 0..53
 * from either half, and only breaking a chest cares which half it was, because
 * vanilla drops the items of the removed half.
 */
namespace {

/** Insertion order is preserved so a save file is stable between saves. */
std::vector<Chest> chests;

int findIndex(int x, int y, int z)
{
    for(size_t i = 0; i < chests.size(); ++i)
    {
        const Chest &c = chests[i];
        if(c.x == x && c.y == y && c.z == z)
            return static_cast<int>(i);
        if(c.slot_count == LargeSlotCount && c.other_x == x && c.other_y == y && c.other_z == z)
            return static_cast<int>(i);
    }
    return -1;
}

bool sameStack(const Stack &s, BLOCK_WDATA block, unsigned short damage)
{
    return s.block == block && s.damage == damage;
}

void writeStack(unsigned char *out, size_t &offset, const Stack &s)
{
    const uint16_t block = s.block;
    const uint16_t damage = s.damage;
    const uint32_t count = s.count;
    std::memcpy(out + offset, &block, sizeof(block)); offset += sizeof(block);
    std::memcpy(out + offset, &damage, sizeof(damage)); offset += sizeof(damage);
    std::memcpy(out + offset, &count, sizeof(count)); offset += sizeof(count);
}

bool readStack(const unsigned char *in, size_t length, size_t &offset, Stack &s)
{
    if(offset + 8 > length)
        return false;

    uint16_t block = 0;
    uint16_t damage = 0;
    uint32_t count = 0;
    std::memcpy(&block, in + offset, sizeof(block)); offset += sizeof(block);
    std::memcpy(&damage, in + offset, sizeof(damage)); offset += sizeof(damage);
    std::memcpy(&count, in + offset, sizeof(count)); offset += sizeof(count);
    s.block = block;
    s.damage = damage;
    s.count = count;
    return true;
}

constexpr size_t RecordHeaderSize = sizeof(int32_t) * 7;
constexpr size_t StackSize = sizeof(uint16_t) * 2 + sizeof(uint32_t);
constexpr size_t RecordSize = RecordHeaderSize + StackSize * static_cast<size_t>(LargeSlotCount);

} // namespace

void place(int x, int y, int z)
{
    if(findIndex(x, y, z) >= 0)
        return;

    Chest c;
    c.x = x;
    c.y = y;
    c.z = z;
    c.other_x = x;
    c.other_y = y;
    c.other_z = z;
    c.slot_count = SlotCount;
    chests.push_back(c);
}

bool exists(int x, int y, int z)
{
    return findIndex(x, y, z) >= 0;
}

bool link(int x, int y, int z, int nx, int ny, int nz)
{
    if(x == nx && y == ny && z == nz)
        return false;

    const int a = findIndex(x, y, z);
    const int b = findIndex(nx, ny, nz);
    if(a < 0 || b < 0 || a == b)
        return false;
    if(chests[static_cast<size_t>(a)].slot_count != SlotCount || chests[static_cast<size_t>(b)].slot_count != SlotCount)
        return false;

    // The chest that was already there keeps slots 0..26 and the one just placed
    // owns the upper half, so the result does not depend on placement order.
    const Chest placed_chest = chests[static_cast<size_t>(a)];
    const int keep = (a < b) ? (b - 1) : b;
    chests.erase(chests.begin() + a);

    Chest &primary = chests[static_cast<size_t>(keep)];
    primary.slot_count = LargeSlotCount;
    primary.other_x = placed_chest.x;
    primary.other_y = placed_chest.y;
    primary.other_z = placed_chest.z;
    return true;
}

int remove(int x, int y, int z, Stack *out, int out_capacity)
{
    const int index = findIndex(x, y, z);
    if(index < 0)
        return 0;

    Chest &c = chests[static_cast<size_t>(index)];
    const bool removed_is_primary = (c.x == x && c.y == y && c.z == z);
    const int first = removed_is_primary ? 0 : SlotCount;

    int written = 0;
    for(int i = 0; i < SlotCount; ++i)
    {
        const Stack &s = c.slots[first + i];
        if(s.count == 0 || getBLOCK(s.block) == BLOCK_AIR)
            continue;
        if(out != nullptr && written < out_capacity)
            out[written] = s;
        ++written;
    }

    if(c.slot_count <= SlotCount)
    {
        chests.erase(chests.begin() + index);
        return written;
    }

    // A double chest loses its pair. When the surviving half is the secondary one
    // it becomes the container, so its items move down into slots 0..26.
    if(removed_is_primary)
    {
        for(int i = 0; i < SlotCount; ++i)
            c.slots[i] = c.slots[SlotCount + i];
        c.x = c.other_x;
        c.y = c.other_y;
        c.z = c.other_z;
    }
    for(int i = SlotCount; i < LargeSlotCount; ++i)
        c.slots[i] = Stack();
    c.other_x = c.x;
    c.other_y = c.y;
    c.other_z = c.z;
    c.slot_count = SlotCount;

    return written;
}

int slotCountOf(int x, int y, int z)
{
    const int index = findIndex(x, y, z);
    if(index < 0)
        return 0;
    return chests[static_cast<size_t>(index)].slot_count;
}

bool isLarge(int x, int y, int z)
{
    return slotCountOf(x, y, z) == LargeSlotCount;
}

Stack stackAt(int x, int y, int z, int index)
{
    const int chest_index = findIndex(x, y, z);
    if(chest_index < 0)
        return Stack();

    const Chest &c = chests[static_cast<size_t>(chest_index)];
    if(index < 0 || index >= c.slot_count)
        return Stack();

    return c.slots[index];
}

void setStackAt(int x, int y, int z, int index, Stack stack)
{
    const int chest_index = findIndex(x, y, z);
    if(chest_index < 0)
        return;

    Chest &c = chests[static_cast<size_t>(chest_index)];
    if(index < 0 || index >= c.slot_count)
        return;

    c.slots[index] = stack;
}

bool addStack(int x, int y, int z, BLOCK_WDATA block, unsigned int count, unsigned short damage)
{
    const int chest_index = findIndex(x, y, z);
    if(chest_index < 0 || count == 0 || getBLOCK(block) == BLOCK_AIR)
        return false;

    Chest &c = chests[static_cast<size_t>(chest_index)];
    const int limit = ItemRules::maxStackSize(block);

    // Each half of a double chest physically holds its own 27 slots, so an item
    // that comes in through the half the player is standing at fills that half
    // first and only spills into the other one when it is full.
    const bool asked_from_primary = (c.x == x && c.y == y && c.z == z);
    const int own = asked_from_primary ? 0 : (c.slot_count == LargeSlotCount ? SlotCount : 0);
    const int other = (c.slot_count == LargeSlotCount && own == 0) ? SlotCount : 0;

    // First pass: top up a matching stack that still has room for the whole item.
    for(int h = 0; h < 2; ++h)
    {
        const int base = (h == 0) ? own : other;
        const int end = (base + SlotCount < c.slot_count) ? base + SlotCount : c.slot_count;
        for(int i = base; i < end; ++i)
        {
            Stack &s = c.slots[i];
            if(s.count == 0 || getBLOCK(s.block) == BLOCK_AIR)
                continue;
            if(!sameStack(s, block, damage))
                continue;
            if(static_cast<unsigned int>(limit) < s.count + count)
                continue;

            s.count += count;
            return true;
        }
    }

    // Second pass: an empty slot.
    for(int h = 0; h < 2; ++h)
    {
        const int base = (h == 0) ? own : other;
        const int end = (base + SlotCount < c.slot_count) ? base + SlotCount : c.slot_count;
        for(int i = base; i < end; ++i)
        {
            Stack &s = c.slots[i];
            if(s.count != 0 && getBLOCK(s.block) != BLOCK_AIR)
                continue;
            s.block = block;
            s.count = count;
            s.damage = damage;
            return true;
        }
    }

    return false;
}

unsigned int itemCount(int x, int y, int z)
{
    const int index = findIndex(x, y, z);
    if(index < 0)
        return 0;

    const Chest &c = chests[static_cast<size_t>(index)];
    unsigned int total = 0;
    for(int i = 0; i < c.slot_count; ++i)
    {
        if(getBLOCK(c.slots[i].block) == BLOCK_AIR)
            continue;
        total += c.slots[i].count;
    }
    return total;
}

void clear()
{
    chests.clear();
}

int chestCount()
{
    return static_cast<int>(chests.size());
}

const std::vector<Chest> &all()
{
    return chests;
}

void insertRecord(const Chest &chest)
{
    Chest c = chest;
    if(c.slot_count != LargeSlotCount)
        c.slot_count = SlotCount;
    if(c.slot_count == SlotCount)
    {
        c.other_x = c.x;
        c.other_y = c.y;
        c.other_z = c.z;
    }
    chests.push_back(c);
}

size_t serializedSize()
{
    return sizeof(uint32_t) + chests.size() * RecordSize;
}

size_t serialize(unsigned char *out, size_t capacity)
{
    if(out == nullptr)
        return 0;

    const size_t needed = serializedSize();
    if(capacity < needed)
        return 0;

    size_t offset = 0;
    const uint32_t count = static_cast<uint32_t>(chests.size());
    std::memcpy(out + offset, &count, sizeof(count));
    offset += sizeof(count);

    for(const Chest &c : chests)
    {
        const int32_t header[7] = { c.x, c.y, c.z, c.other_x, c.other_y, c.other_z, c.slot_count };
        std::memcpy(out + offset, header, sizeof(header));
        offset += sizeof(header);

        for(int i = 0; i < LargeSlotCount; ++i)
        {
            const Stack s = i < c.slot_count ? c.slots[i] : Stack();
            writeStack(out, offset, s);
        }
    }

    return offset;
}

bool deserialize(const unsigned char *in, size_t length)
{
    if(in == nullptr || length < sizeof(uint32_t))
        return false;

    chests.clear();

    size_t offset = 0;
    uint32_t count = 0;
    std::memcpy(&count, in + offset, sizeof(count));
    offset += sizeof(count);

    for(uint32_t i = 0; i < count; ++i)
    {
        if(offset + RecordHeaderSize > length)
        {
            chests.clear();
            return false;
        }

        int32_t header[7] = {};
        std::memcpy(header, in + offset, sizeof(header));
        offset += sizeof(header);

        Chest c;
        c.x = header[0];
        c.y = header[1];
        c.z = header[2];
        c.other_x = header[3];
        c.other_y = header[4];
        c.other_z = header[5];
        c.slot_count = header[6] == LargeSlotCount ? LargeSlotCount : SlotCount;

        for(int s = 0; s < LargeSlotCount; ++s)
        {
            Stack stack;
            if(!readStack(in, length, offset, stack))
            {
                chests.clear();
                return false;
            }
            c.slots[s] = stack;
        }

        insertRecord(c);
    }

    return true;
}

} // namespace ChestStore
