// Per-block lighting for a chunk.
//
// The world is meshed per chunk while the player moves, and every vertex carries
// a shade byte that the rasteriser multiplies into the texture it draws. The
// day/night cycle already uses that byte for the global tint; this file fills in
// the part that does *not* change with the time of day, so that a cave is dark
// at noon and a hilltop is visible at midnight:
//
//   shade = face direction x sky depth x whatever glows nearby
//
// Face direction (blockrenderer.cpp's computeLighting) and the day/night tint
// (nglSetGlobalShade) were already there. What is added here is the middle term,
// which is baked into the vertex when the chunk is built -- a cost that is paid
// once per mesh, not per frame -- and the emitters, which are relaxed into a
// small chunk-local lattice of light levels.
//
// Everything numeric and reusable lives in blocklight.h, which is pure and
// host-tested; this file is only the chunk's side of it.

#include "chunk.h"

#include "blocklight.h"
#include "blockrenderer.h"

static_assert(BlockLight::Field::Size == Chunk::SIZE, "blocklight.h and Chunk::SIZE disagree on the chunk size!");

namespace
{
    /** Keeps a local coordinate inside the chunk (vertices can sit on the border). */
    int clampLocal(const int value)
    {
        if(value < 0)
            return 0;
        if(value >= Chunk::SIZE)
            return Chunk::SIZE - 1;
        return value;
    }

}

void Chunk::rebuildLightField()
{
    uint8_t *field = block_light;
    BlockLight::Field::clear(field);

    // Every glowing block in this chunk is a source at its own level.
    for(int x = 0; x < SIZE; ++x)
        for(int y = 0; y < SIZE; ++y)
            for(int z = 0; z < SIZE; ++z)
            {
                const int level = BlockLight::emitterLevel(getBLOCK(blocks[x][y][z]));
                if(level > 0)
                    BlockLight::Field::plant(field, x, y, z, level);
            }

    // A light in the chunk next door reaches one block into this one: seed the
    // border cell just inside this chunk with one level less than the source.
    // That is what keeps a torch at a chunk border from stopping at it.
    static const int offsets[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };

    for(int face = 0; face < 6; ++face)
    {
        const int ox = offsets[face][0];
        const int oy = offsets[face][1];
        const int oz = offsets[face][2];

        for(int a = 0; a < SIZE; ++a)
            for(int b = 0; b < SIZE; ++b)
            {
                int x, y, z;
                if(ox != 0)      { x = ox > 0 ? SIZE - 1 : 0; y = a; z = b; }
                else if(oy != 0) { y = oy > 0 ? SIZE - 1 : 0; x = a; z = b; }
                else             { z = oz > 0 ? SIZE - 1 : 0; x = a; y = b; }

                if(!BlockLight::Field::inBounds(x + ox, y + oy, z + oz))
                {
                    const int level = BlockLight::emitterLevel(getBLOCK(getGlobalBlockRelative(x + ox, y + oy, z + oz)));
                    if(level > 1)
                        BlockLight::Field::plant(field, x, y, z, level - 1);
                }
            }
    }

    // Relax until nothing changes: a chunk with no light in or next to it stops
    // after the first pass, so the cost is proportional to what is actually lit.
    for(int pass = 0; pass < BlockLight::MaxLevel; ++pass)
    {
        if(BlockLight::Field::relax(field) == 0)
            break;
    }

    light_field_valid = true;
}

int Chunk::localLightLevel(const int x, const int y, const int z)
{
    if(!light_field_valid)
        rebuildLightField();

    return BlockLight::Field::at(block_light, x, y, z);
}

int Chunk::lightLevel(const int x, const int y, const int z)
{
    // A vertex can sit on the plane between two blocks (local -1 or SIZE), and it
    // belongs to the block inside the chunk, so the lookup is clamped inwards.
    const int block_x = clampLocal(x);
    const int block_y = clampLocal(y);
    const int block_z = clampLocal(z);

    const int sky = BlockLight::skyLevel(this->y * SIZE + block_y, skyHeightOf(block_x, block_z));
    const int emitted = localLightLevel(block_x, block_y, block_z);

    // Vanilla takes the brighter of the two channels, which is also what makes a
    // torch in a deep cave light the block it stands in.
    return emitted > sky ? emitted : sky;
}

COLOR Chunk::litColor(const COLOR color, const int x, const int y, const int z)
{
    const int shade = BlockLight::combineShade(color & 0xFF, lightLevel(x, y, z));
    return static_cast<COLOR>((color & 0xFF00) | static_cast<unsigned int>(shade));
}
