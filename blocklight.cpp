#include "blocklight.h"

namespace BlockLight
{

int skyLevel(int world_y, int sky_y)
{
    if(sky_y >= SkyAlwaysOpen)
        return MaxLevel;

    // The block at the surface (one below the highest sky-blocking block) still
    // sees the sky; everything under it loses one level per block.
    const int depth = sky_y - world_y - 1;
    if(depth <= 0)
        return MaxLevel;
    if(depth >= MaxLevel)
        return 0;
    return MaxLevel - depth;
}

int shadeForLevel(int level)
{
    if(level <= 0)
        return MinShade;
    if(level >= MaxLevel)
        return MaxShade;
    return MinShade + (level * (MaxShade - MinShade)) / MaxLevel;
}

int combineShade(int face_shade, int level)
{
    if(face_shade <= 0)
        face_shade = NeutralFaceShade;

    const int shade = shadeForLevel(level);
    return (face_shade * shade) >> 8;
}

int emitterLevel(int block_id)
{
    switch(block_id)
    {
    case BlockGlowstone:
        return MaxLevel;
    case BlockLava:
        return MaxLevel;
    case BlockTorch:
        return 14;
    case BlockRedstoneTorch:
        return 7;
    default:
        return 0;
    }
}

bool isEmitter(int block_id)
{
    return emitterLevel(block_id) > 0;
}

namespace Field
{

void clear(uint8_t *field)
{
    if(field == nullptr)
        return;
    for(int i = 0; i < Cells; ++i)
        field[i] = 0;
}

int plant(uint8_t *field, int x, int y, int z, int level)
{
    if(field == nullptr || !inBounds(x, y, z))
        return 0;

    int clamped = level;
    if(clamped > MaxLevel)
        clamped = MaxLevel;
    if(clamped < 0)
        clamped = 0;

    uint8_t &cell = field[Index(x, y, z)];
    if(cell < static_cast<uint8_t>(clamped))
    {
        cell = static_cast<uint8_t>(clamped);
        return clamped;
    }
    return 0;
}

int at(const uint8_t *field, int x, int y, int z)
{
    if(field == nullptr || !inBounds(x, y, z))
        return 0;
    return field[Index(x, y, z)];
}

int relax(uint8_t *field)
{
    if(field == nullptr)
        return 0;

    // The lattice is small enough (512 cells) that a single pass over it, reading
    // the neighbours of each cell, is cheaper than keeping a work list, and a
    // fixed number of passes is easier to bound than a queue that can grow.
    static const int offsets[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };

    int changed = 0;
    for(int y = 0; y < Size; ++y)
        for(int z = 0; z < Size; ++z)
            for(int x = 0; x < Size; ++x)
            {
                int best = field[Index(x, y, z)];
                for(int i = 0; i < 6; ++i)
                {
                    const int nx = x + offsets[i][0];
                    const int ny = y + offsets[i][1];
                    const int nz = z + offsets[i][2];
                    if(!inBounds(nx, ny, nz))
                        continue;
                    const int neighbour = field[Index(nx, ny, nz)] - 1;
                    if(neighbour > best)
                        best = neighbour;
                }

                if(best != field[Index(x, y, z)])
                {
                    field[Index(x, y, z)] = static_cast<uint8_t>(best);
                    ++changed;
                }
            }

    return changed;
}

int brightest(const uint8_t *field)
{
    if(field == nullptr)
        return 0;

    int best = 0;
    for(int i = 0; i < Cells; ++i)
        if(field[i] > best)
            best = field[i];
    return best;
}

} // namespace Field

} // namespace BlockLight
