/**
 * Snow cover rules. See snowcover.h for the contract.
 *
 * Nothing here touches the world: the caller finds the ground and asks what the
 * depth should become, and it is the caller's job to write the block. That is
 * what keeps the module testable and what keeps the cost of snow measurable -- a
 * step is three hash pairs and three ground searches, and it happens once a
 * second rather than once a frame.
 */

#include "snowcover.h"

namespace
{
    constexpr uint32_t SaltColumn = 0x534E4F57u; // "SNOW"

    uint32_t hashColumn(uint32_t seed, unsigned long long index, uint32_t salt)
    {
        uint32_t h = seed ^ salt
            ^ static_cast<uint32_t>(index)
            ^ static_cast<uint32_t>(index >> 32);
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        h *= 0x846ca68bu;
        h ^= h >> 16;
        return h;
    }
}

bool SnowCover::isPermeable(uint8_t block)
{
    switch(block)
    {
    case BlockAir:
    case BlockWater:
    case BlockWaterFast:
    case BlockLava:
    case BlockTorch:
    case BlockRedstoneTorch:
    case BlockRedstoneWire:
    case BlockPressurePlate:
    case BlockFlower:
    case BlockMushroom:
    case BlockWheat:
    case BlockSpiderweb:
    case BlockItem:
        return true;
    default:
        return false;
    }
}

int SnowCover::layersOf(uint8_t data)
{
    if(data < 1)
        return 1; // a snow block placed without a depth is one step deep
    if(data > MaxLayers)
        return MaxLayers;
    return data;
}

int SnowCover::step(int layers, bool snowing, bool freezing)
{
    if(layers < 0)
        layers = 0;
    if(layers > MaxLayers)
        layers = MaxLayers;

    if(!freezing)
        return layers > 0 ? layers - 1 : 0;

    if(!snowing)
        return layers;

    return layers < MaxLayers ? layers + 1 : layers;
}

void SnowCover::columnOffset(uint32_t world_seed, unsigned long long step_index, int index, int &dx, int &dz)
{
    // Each candidate column of a step gets its own hash: sharing one hash and
    // adding the index would walk the three candidates along a single line, which
    // reads as a stripe of snow appearing rather than as a scatter.
    const uint32_t h = hashColumn(world_seed, step_index * static_cast<unsigned long long>(ColumnsPerStep)
                                                + static_cast<unsigned long long>(index), SaltColumn);
    const int span = Radius * 2 + 1;
    dx = static_cast<int>(h % static_cast<uint32_t>(span)) - Radius;
    dz = static_cast<int>((h >> 12) % static_cast<uint32_t>(span)) - Radius;
}
