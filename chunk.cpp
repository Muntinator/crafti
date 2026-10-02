#include <cassert>
#include <cstdlib>
#include <algorithm>
#include <libndls.h>

#include "world.h"
#include "chunk.h"
#include "biomegen.h"
#include "fastmath.h"
#include "blockrenderer.h"
#include "oregeneration.h"
#include "villagegen.h"
#include "structuregen.h"
#include "menuui.h"
#include "task.h"

#ifndef NDEBUG
    #define debug(...) printf(__VA_ARGS__)
#else
    #define debug(...)
#endif

constexpr const int Chunk::SIZE;
static_assert(Village::ChunkBlocks == Chunk::SIZE, "villagegen.h and Chunk::SIZE disagree on the chunk size!");
static_assert(Structures::ChunkBlocks == Chunk::SIZE, "structuregen.h and Chunk::SIZE disagree on the chunk size!");
int Chunk::pos_indices[SIZE + 1][SIZE + 1][SIZE + 1];

Chunk::Chunk(int x, int y, int z)
    : x(x), y(y), z(z), abs_x(x*SIZE*BLOCK_SIZE), abs_y(y*SIZE*BLOCK_SIZE), abs_z(z*SIZE*BLOCK_SIZE), aabb(abs_x, abs_y, abs_z, abs_x + SIZE*BLOCK_SIZE, abs_y + SIZE*BLOCK_SIZE, abs_z + SIZE*BLOCK_SIZE)
{}

static constexpr bool inBounds(int x, int y, int z)
{
    return x >= 0 && y >= 0 && z >= 0 && x < Chunk::SIZE && y < Chunk::SIZE && z < Chunk::SIZE;
}

static constexpr int localFromGlobal(const int global)
{
    static_assert(Chunk::SIZE == 8, "Update the bit operations accordingly!");
    return global & 0b111;
}

static constexpr int chunkFromGlobal(const int global)
{
    static_assert(Chunk::SIZE == 8, "Update the bit operations accordingly!");
    return global >> 3;
}

// Generation randomness must be a function of world seed and chunk coordinates,
// not the order in which streaming happens to generate chunks.
static unsigned int chunkGenerationSeed(unsigned int world_seed, int x, int y, int z, unsigned int salt)
{
    unsigned int seed = world_seed
        ^ (static_cast<unsigned int>(x) * 73856093u)
        ^ (static_cast<unsigned int>(y) * 19349663u)
        ^ (static_cast<unsigned int>(z) * 83492791u)
        ^ salt;
    seed ^= seed >> 16;
    seed *= 0x7feb352du;
    seed ^= seed >> 15;
    seed *= 0x846ca68bu;
    seed ^= seed >> 16;
    return seed;
}

static unsigned int nextGenerationRandom(unsigned int &seed)
{
    seed = seed * 1664525u + 1013904223u;
    return seed;
}

// Surface height and surface materials of a terrain column, shared by terrain
// generation and village site selection so both always agree on where the
// ground is and what it is made of. `world_x` and `world_z` are world block
// coordinates, the result describes one world column.
//
// The Perlin base below is unchanged from the original generator; biomegen.h
// then adds the mountain lift, carves any river bed and picks the two surface
// blocks. Keeping that split means the biome layer is testable on the host
// (tests/biomegen_test.cc) while the terrain keeps its familiar shape.
static BiomeGen::Column terrainColumn(const PerlinNoise &noise, unsigned int world_seed, int world_x, int world_z)
{
    static_assert(Chunk::SIZE == 8, "Update the noise scale accordingly!");
    static_assert(BiomeGen::MaxHeight == World::HEIGHT * Chunk::SIZE - 3,
                  "biomegen.h and World::HEIGHT disagree on how tall the world is!");
    static_assert(BiomeGen::MinHeight == 4, "biomegen.h moved the floor of the world!");

    // Equivalent to (local/Chunk::SIZE + chunk)/4 of the original inline code.
    const GLFix nx = GLFix(world_x) / (Chunk::SIZE * 4);
    const GLFix nz = GLFix(world_z) / (Chunk::SIZE * 4);

    const GLFix e1 = noise.noise(nx, nz, 10);
    const GLFix e2 = noise.noise(nx * 2, nz * 2, 20) * GLFix(0.65f);
    const GLFix e3 = noise.noise(nx * 4, nz * 4, 30) * GLFix(0.35f);
    const GLFix e4 = noise.noise(nx * 8, nz * 8, 40) * GLFix(0.20f);

    GLFix noise_val = (e1 + e2 + e3 + e4) / GLFix(2.20f);
    // Push valleys down and peaks up for less flat terrain.
    noise_val = (noise_val + noise_val * noise_val) / 2;

    constexpr int world_gen_min = BiomeGen::MinHeight;
    constexpr int world_gen_max = BiomeGen::MaxHeight;
    const int base_height = world_gen_min + (noise_val * (world_gen_max - world_gen_min)).round();

    BiomeGen::Column column;
    BiomeGen::columnAt(world_seed, world_x, world_z, base_height, column);
    return column;
}

static int terrainSurfaceHeight(const PerlinNoise &noise, unsigned int world_seed, int world_x, int world_z)
{
    return terrainColumn(noise, world_seed, world_x, world_z).height;
}

// Integer division rounding towards negative infinity, needed because chunks to
// the north/west have negative coordinates.
static int floorDiv(int value, int divisor)
{
    int quotient = value / divisor;
    if((value % divisor) != 0 && ((value < 0) != (divisor < 0)))
        --quotient;
    return quotient;
}

// The sky map: for every column of this chunk, the world Y of the highest block
// that blocks the sky. A vertex's sky light is then simply how far below that it
// sits (blocklight.h does the falloff), which is what makes a cave, a tunnel or
// the space under an overhang dark while the ground above it stays bright.
//
// The terrain surface comes from the same column function the terrain and the
// villages use, so the two always agree, and anything this chunk placed above it
// (a tree, a house, a ruin) is taken into account as well. Blocks in *other*
// chunks of the same column are deliberately left out: a canopy two chunks up
// costs a few levels of light, not a visible difference, and leaving it out keeps
// the cost of the whole thing to one terrain sample per column.
int Chunk::columnSkyHeight(const int world_x, const int world_z) const
{
    // A flat or graph world has nothing to do with the terrain noise, and its
    // blocks are lit from above either way, so nothing there is darkened.
    if(world.worldType() != World::WorldType::Terrain)
        return BlockLight::SkyAlwaysOpen;

    const PerlinNoise &noise = world.noiseGenerator();
    int highest = terrainSurfaceHeight(noise, world.seedValue(), world_x, world_z) - 1;
    if(highest < 0)
        highest = 0;

    const int local_x = world_x - this->x * SIZE;
    const int local_z = world_z - this->z * SIZE;
    if(local_x >= 0 && local_x < SIZE && local_z >= 0 && local_z < SIZE)
    {
        for(int y = SIZE - 1; y >= 0; --y)
        {
            const int world_y = this->y * SIZE + y;
            if(world_y <= highest)
                break; // the terrain is already higher than anything in this chunk

            const BLOCK_WDATA block = blocks[local_x][y][local_z];
            if(block != BLOCK_AIR && global_block_renderer.isOpaque(block))
            {
                highest = world_y;
                break;
            }
        }
    }

    const int ceiling = World::HEIGHT * SIZE - 1;
    return highest > ceiling ? ceiling : highest;
}

void Chunk::rebuildSkyHeights()
{
    for(int x = 0; x < SIZE; ++x)
        for(int z = 0; z < SIZE; ++z)
            column_sky_height[x][z] = static_cast<uint8_t>(columnSkyHeight(this->x * SIZE + x, this->z * SIZE + z));

    sky_heights_valid = true;
}

int Chunk::skyHeightOf(const int local_x, const int local_z)
{
    if(!sky_heights_valid)
        rebuildSkyHeights();

    return column_sky_height[local_x][local_z];
}

namespace
{
    // Village plans are a pure function of (seed, frequency, cell), but every
    // chunk of a cell asks for the same plan while streaming. A handful of
    // entries is enough to turn the site search into a one-time cost per cell.
    struct VillagePlanCacheEntry
    {
        bool used = false;
        unsigned int seed = 0;
        int frequency = 0;
        int cell_x = 0, cell_z = 0;
        bool has_plan = false;
        Village::Plan plan;
    };

    constexpr int VillagePlanCacheSize = 8;
    VillagePlanCacheEntry village_plan_cache[VillagePlanCacheSize];
    int village_plan_cache_cursor = 0;

    /** Rejects sites whose terrain is too uneven for the flat village layout. */
    bool villageSiteIsFlat(const PerlinNoise &noise, unsigned int world_seed, int origin_x, int origin_z, int ground_y)
    {
        int lowest = ground_y;
        int highest = ground_y;
        for(int i = -1; i <= 1; ++i)
            for(int j = -1; j <= 1; ++j)
            {
                const int sample = terrainSurfaceHeight(noise, world_seed, origin_x + i * Village::Radius, origin_z + j * Village::Radius);
                if(sample < lowest)
                    lowest = sample;
                if(sample > highest)
                    highest = sample;
            }
        return highest - lowest <= Village::FlatnessTolerance;
    }

    /** Picks the first candidate in a cell that sits on flat, dry ground. */
    bool resolveVillagePlan(unsigned int world_seed, int frequency, int cell_x, int cell_z, const PerlinNoise &noise, Village::Plan &out)
    {
        for(int i = 0; i < VillagePlanCacheSize; ++i)
        {
            const VillagePlanCacheEntry &entry = village_plan_cache[i];
            if(entry.used && entry.seed == world_seed && entry.frequency == frequency
                && entry.cell_x == cell_x && entry.cell_z == cell_z)
            {
                if(entry.has_plan)
                    out = entry.plan;
                return entry.has_plan;
            }
        }

        bool has_plan = false;
        Village::Plan plan;
        if(Village::cellHasVillage(world_seed, cell_x, cell_z, frequency))
        {
            for(int candidate = 0; candidate < Village::CandidateCount && !has_plan; ++candidate)
            {
                int origin_x, origin_z;
                Village::cellCandidate(world_seed, cell_x, cell_z, candidate, origin_x, origin_z);

                const int ground_y = terrainSurfaceHeight(noise, world_seed, origin_x, origin_z);
                if(ground_y < Village::MinGroundY || ground_y > Village::MaxGroundY)
                    continue;
                if(!villageSiteIsFlat(noise, world_seed, origin_x, origin_z, ground_y))
                    continue;

                has_plan = Village::planVillage(world_seed, cell_x, cell_z, candidate, ground_y, plan);
            }
        }

        // Publish accepted plans so villagers can find the real villages.
        if(has_plan)
            Village::registerPlan(plan);

        VillagePlanCacheEntry &entry = village_plan_cache[village_plan_cache_cursor];
        village_plan_cache_cursor = (village_plan_cache_cursor + 1) % VillagePlanCacheSize;
        entry.used = true;
        entry.seed = world_seed;
        entry.frequency = frequency;
        entry.cell_x = cell_x;
        entry.cell_z = cell_z;
        entry.has_plan = has_plan;
        entry.plan = plan;

        if(has_plan)
            out = plan;
        return has_plan;
    }
}

unsigned int Chunk::getPosition(unsigned int x, unsigned int y, unsigned int z)
{
    assert (x <= Chunk::SIZE && y <= Chunk::SIZE && z <= Chunk::SIZE);

    if(pos_indices[x][y][z] == -1)
    {
        pos_indices[x][y][z] = build_positions.size();
        build_positions.emplace_back(VECTOR3{x*BLOCK_SIZE, y*BLOCK_SIZE, z*BLOCK_SIZE});
    }

    return pos_indices[x][y][z];
}

void Chunk::addAlignedVertex(const int x, const int y, const int z, GLFix u, GLFix v, const COLOR c)
{
    // The one place every block face goes through: the per-block light is baked
    // into the shade byte here, so no renderer has to know about it.
    build_vertices.emplace_back(IndexedVertex{getPosition(x, y, z), u, v, litColor(c, x, y, z)});
}

void Chunk::addUnalignedVertex(const GLFix x, const GLFix y, const GLFix z, const GLFix u, const GLFix v, const COLOR c)
{
    const int block_x = (x / BLOCK_SIZE).round();
    const int block_y = (y / BLOCK_SIZE).round();
    const int block_z = (z / BLOCK_SIZE).round();
    build_vertices_unaligned.emplace_back(VERTEX{x, y, z, u, v, litColor(c, block_x, block_y, block_z)});
}

void Chunk::addUnalignedVertex(const VERTEX &v)
{
    build_vertices_unaligned.push_back(v);
}

void Chunk::addAnimation(const Chunk::Animation &animation)
{
    animations.push_back(animation);
}

void Chunk::addParticle(const Particle &particle)
{
    particles.push_back(particle);
}

void Chunk::addAlignedVertexQuad(const int x, const int y, const int z, GLFix u, GLFix v, const COLOR c)
{
    build_vertices_quad.emplace_back(IndexedVertex{getPosition(x, y, z), u, v, litColor(c, x, y, z)});
}

void Chunk::addAlignedVertexForceColor(const int x, const int y, const int z, GLFix u, GLFix v, const COLOR c)
{
    build_vertices_color.emplace_back(IndexedVertex{getPosition(x, y, z), u, v, c});
}

void Chunk::setLocalBlockSideRendered(const int x, const int y, const int z, const BLOCK_SIDE_BITFIELD side)
{
    sides_rendered[x][y][z] |= side;
}

bool Chunk::isLocalBlockSideRendered(const int x, const int y, const int z, const BLOCK_SIDE_BITFIELD side)
{
    return sides_rendered[x][y][z] & side;
}

void Chunk::buildGeometry()
{
    std::fill(pos_indices[0][0] + 0, pos_indices[SIZE][SIZE] + SIZE + 1, -1);

    build_positions.clear();
    build_vertices.clear();
    build_vertices_quad.clear();
    build_vertices_color.clear();
    build_vertices_unaligned.clear();
    animations.clear();

    //Bottom of world doesn't need to be drawn
    int y_start = this->y == 0 ? 0 : -1;

    //Now go through map and search for transparent blocks and draw only the sides adjacent to them
    for(int x = -1; x <= SIZE; x++)
    {
        for(int y = y_start; y <= SIZE; y++)
        {
            for(int z = -1; z <= SIZE; z++)
            {
                BLOCK_WDATA block = getGlobalBlockRelative(x, y, z), block1;

                if(block != BLOCK_AIR && global_block_renderer.isOpaque(block))
                    continue;

                if(inBounds(x - 1, y, z) && (block1 = blocks[x - 1][y][z]) != BLOCK_AIR && !(sides_rendered[x - 1][y][z] & BLOCK_RIGHT_BIT))
                    global_block_renderer.geometryNormalBlock(block1, x - 1, y, z, BLOCK_RIGHT, *this);

                if(inBounds(x + 1, y, z) && (block1 = blocks[x + 1][y][z]) != BLOCK_AIR && !(sides_rendered[x + 1][y][z] & BLOCK_LEFT_BIT))
                    global_block_renderer.geometryNormalBlock(block1, x + 1, y, z, BLOCK_LEFT, *this);

                if(inBounds(x, y - 1, z) && (block1 = blocks[x][y - 1][z]) != BLOCK_AIR && !(sides_rendered[x][y - 1][z] & BLOCK_TOP_BIT))
                    global_block_renderer.geometryNormalBlock(block1, x, y - 1, z, BLOCK_TOP, *this);

                if(inBounds(x, y + 1, z) && (block1 = blocks[x][y + 1][z]) != BLOCK_AIR && !(sides_rendered[x][y + 1][z] & BLOCK_BOTTOM_BIT))
                    global_block_renderer.geometryNormalBlock(block1, x, y + 1, z, BLOCK_BOTTOM, *this);

                if(inBounds(x, y, z - 1) && (block1 = blocks[x][y][z - 1]) != BLOCK_AIR && !(sides_rendered[x][y][z - 1] & BLOCK_BACK_BIT))
                    global_block_renderer.geometryNormalBlock(block1, x, y, z - 1, BLOCK_BACK, *this);

                if(inBounds(x, y, z + 1) && (block1 = blocks[x][y][z + 1]) != BLOCK_AIR && !(sides_rendered[x][y][z + 1] & BLOCK_FRONT_BIT))
                    global_block_renderer.geometryNormalBlock(block1, x, y, z + 1, BLOCK_FRONT, *this);
            }
        }
    }

    //Special blocks
    GLFix pos_x = 0;
    for(int x = 0; x < SIZE; x++, pos_x += BLOCK_SIZE)
    {
        GLFix pos_y = 0;
        for(int y = 0; y < SIZE; y++, pos_y += BLOCK_SIZE)
        {
            GLFix pos_z = 0;
            for(int z = 0; z < SIZE; z++, pos_z += BLOCK_SIZE)
            {
                BLOCK_WDATA block = blocks[x][y][z];

                if(getBLOCK(block) == BLOCK_AIR)
                    continue;

                if(global_block_renderer.isOpaque(getGlobalBlockRelative(x - 1, y, z))
                    && global_block_renderer.isOpaque(getGlobalBlockRelative(x + 1, y, z))
                    && global_block_renderer.isOpaque(getGlobalBlockRelative(x, y - 1, z))
                    && global_block_renderer.isOpaque(getGlobalBlockRelative(x, y + 1, z))
                    && global_block_renderer.isOpaque(getGlobalBlockRelative(x, y, z - 1))
                    && global_block_renderer.isOpaque(getGlobalBlockRelative(x, y, z + 1)))
                    continue;

                global_block_renderer.renderSpecialBlock(block, pos_x, pos_y, pos_z, *this);
            }
        }
    }

    std::fill(sides_rendered[0][0] + 0, sides_rendered[SIZE - 1][SIZE - 1] + SIZE, 0);

    build_positions_processed.resize(build_positions.size());

    build_complete = true;
    build_dirty = false;

    debug("Done!\n");
}

void Chunk::swapMeshes()
{
    if(!build_complete)
        return;

    positions.swap(build_positions);
    positions_processed.swap(build_positions_processed);
    vertices.swap(build_vertices);
    vertices_quad.swap(build_vertices_quad);
    vertices_color.swap(build_vertices_color);
    vertices_unaligned.swap(build_vertices_unaligned);

    build_complete = false;
}

void Chunk::buildGeometryAsync()
{
    buildGeometry();
}

static bool behindClip(const VECTOR3 &v1)
{
    return (transformation->data[2][0]*v1.x + transformation->data[2][1]*v1.y + transformation->data[2][2]*v1.z + transformation->data[2][3]) <= GLFix(CLIP_PLANE);
}

void Chunk::logic(bool ticks_enabled)
{
    if(ticks_enabled)
    {
        tick_counter -= 1;
        if(tick_counter == 0)
        {
            tick_counter = 10; //Do a tick every 10th frame

            for(int x = 0; x < SIZE; x++)
                for(int y = 0; y < SIZE; y++)
                    for(int z = 0; z < SIZE; z++)
                    {
                        BLOCK_WDATA block = blocks[x][y][z];
                        if(block != BLOCK_AIR)
                            global_block_renderer.tick(block, x, y, z, *this);
                    }
        }
    }

    for(int i = particles.size() - 1; i >= 0; --i)
    {
        bool remove = false;
        particles[i].logic(&remove);
        if(remove)
            particles.erase(particles.begin() + i);
    }
}

void Chunk::render()
{
    // Swap if build is complete
    if(__builtin_expect(build_complete, 0))
        swapMeshes();

    //If there's nothing to render, skip it completely
    if(positions.size() == 0 && vertices_unaligned.size() == 0
            && animations.size() == 0 && particles.size() == 0)
        return;

    glPushMatrix();
    glTranslatef(abs_x, abs_y, abs_z);

    //Basic culling
    VECTOR3 v1{0,                            0,                            0},
            v2{0 + Chunk::SIZE * BLOCK_SIZE, 0,                            0},
            v3{0,                            0 + Chunk::SIZE * BLOCK_SIZE, 0},
            v4{0 + Chunk::SIZE * BLOCK_SIZE, 0 + Chunk::SIZE * BLOCK_SIZE, 0},
            v5{0,                            0,                            0 + Chunk::SIZE * BLOCK_SIZE},
            v6{0 + Chunk::SIZE * BLOCK_SIZE, 0,                            0 + Chunk::SIZE * BLOCK_SIZE},
            v7{0,                            0 + Chunk::SIZE * BLOCK_SIZE, 0 + Chunk::SIZE * BLOCK_SIZE},
            v8{0 + Chunk::SIZE * BLOCK_SIZE, 0 + Chunk::SIZE * BLOCK_SIZE, 0 + Chunk::SIZE * BLOCK_SIZE};

    //Z-Culling (now, it's a bit cheaper than a full MultMatVectRes)
    if(behindClip(v1) && behindClip(v2) && behindClip(v3) && behindClip(v4) && behindClip(v5) && behindClip(v6) && behindClip(v7) && behindClip(v8))
        return glPopMatrix();

    VECTOR3 v9, v10, v11, v12, v13, v14, v15, v16;

    nglMultMatVectRes(transformation, &v1, &v9);
    nglMultMatVectRes(transformation, &v2, &v10);
    nglMultMatVectRes(transformation, &v3, &v11);
    nglMultMatVectRes(transformation, &v4, &v12);
    nglMultMatVectRes(transformation, &v5, &v13);
    nglMultMatVectRes(transformation, &v6, &v14);
    nglMultMatVectRes(transformation, &v7, &v15);
    nglMultMatVectRes(transformation, &v8, &v16);

    nglPerspective(&v9);
    nglPerspective(&v10);
    nglPerspective(&v11);
    nglPerspective(&v12);
    nglPerspective(&v13);
    nglPerspective(&v14);
    nglPerspective(&v15);
    nglPerspective(&v16);

    //X and Y-Culling

    if(v9.x < GLFix(0) && v10.x < GLFix(0) && v11.x < GLFix(0) && v12.x < GLFix(0) && v13.x < GLFix(0) && v14.x < GLFix(0) && v15.x < GLFix(0) && v16.x < GLFix(0))
        return glPopMatrix();

    if(v9.y < GLFix(0) && v10.y < GLFix(0) && v11.y < GLFix(0) && v12.y < GLFix(0) && v13.y < GLFix(0) && v14.y < GLFix(0) && v15.y < GLFix(0) && v16.y < GLFix(0))
        return glPopMatrix();

    if(v9.x >= SCREEN_WIDTH && v10.x >= SCREEN_WIDTH && v11.x >= SCREEN_WIDTH && v12.x >= SCREEN_WIDTH
            && v13.x >= SCREEN_WIDTH && v14.x >= SCREEN_WIDTH && v15.x >= SCREEN_WIDTH && v16.x >= SCREEN_WIDTH)
        return glPopMatrix();

    if(v9.y >= SCREEN_HEIGHT && v10.y >= SCREEN_HEIGHT && v11.y >= SCREEN_HEIGHT && v12.y >= SCREEN_HEIGHT
            && v13.y >= SCREEN_HEIGHT && v14.y >= SCREEN_HEIGHT && v15.y >= SCREEN_HEIGHT && v16.y >= SCREEN_HEIGHT)
        return glPopMatrix();

    glBindTexture(nullptr);
    nglDrawArray(vertices_color.data(), vertices_color.size(), positions.data(), positions.size(), positions_processed.data(), GL_QUADS, true);

    glBindTexture(terrain_quad);
    nglDrawArray(vertices_quad.data(), vertices_quad.size(), positions.data(), positions.size(), positions_processed.data(), GL_QUADS, false);

    glBindTexture(terrain_current);
    nglDrawArray(vertices.data(), vertices.size(), positions.data(), positions.size(), positions_processed.data(), GL_QUADS, false);

    const VERTEX *ve = vertices_unaligned.data();
    for(unsigned int i = 0; i < vertices_unaligned.size(); i += 4, ve += 4)
    {
        VERTEX v1, v2, v3, v4;

        nglMultMatVectRes(transformation, &ve[0], &v1);
        nglMultMatVectRes(transformation, &ve[1], &v2);
        nglMultMatVectRes(transformation, &ve[2], &v3);

        //nglMultMatVectRes doesn't copy u,v and c
        v1.u = ve[0].u;
        v1.v = ve[0].v;
        v1.c = ve[0].c;
        v2.u = ve[1].u;
        v2.v = ve[1].v;
        v2.c = ve[1].c;
        v3.u = ve[2].u;
        v3.v = ve[2].v;
        v3.c = ve[2].c;

        if(nglDrawTriangle(&v1, &v2, &v3, (v1.c & TEXTURE_DRAW_BACKFACE) == 0) || (v1.c & INDEPENDENT_TRIS))
        {
            nglMultMatVectRes(transformation, &ve[3], &v4);
            v4.u = ve[3].u;
            v4.v = ve[3].v;
            v4.c = ve[3].c;

            nglDrawTriangle(&v3, &v4, &v1, v1.c & INDEPENDENT_TRIS);
        }
    }

    for(auto &animation : animations)
        animation.animate(animation.x, animation.y, animation.z, *this);

    for(auto &particle : particles)
        particle.render();

    return glPopMatrix();
}

BLOCK_WDATA Chunk::getLocalBlock(const int x, const int y, const int z) const
{
    assert(inBounds(x, y, z));
    return blocks[x][y][z];
}

void Chunk::setLocalBlock(const int x, const int y, const int z, const BLOCK_WDATA block, bool set_dirty)
{
    assert(inBounds(x, y, z));
    blocks[x][y][z] = block;

    // The light is a function of the blocks, so a change invalidates it: the one
    // column is patched on the spot (one terrain sample) and the emitter field is
    // rebuilt on the next mesh build, which a block change triggers anyway.
    if(sky_heights_valid)
        column_sky_height[x][z] = static_cast<uint8_t>(columnSkyHeight(this->x * SIZE + x, this->z * SIZE + z));
    light_field_valid = false;

    if(!set_dirty)
        return;

    setDirty();

    if(x == 0)
        if(Chunk *c = world.findChunk(this->x - 1, this->y, this->z))
            c->setDirty();

    if(x == Chunk::SIZE - 1)
        if(Chunk *c = world.findChunk(this->x + 1, this->y, this->z))
            c->setDirty();

    if(y == 0)
        if(Chunk *c = world.findChunk(this->x, this->y - 1, this->z))
            c->setDirty();

    if(y == Chunk::SIZE - 1)
        if(Chunk *c = world.findChunk(this->x, this->y + 1, this->z))
            c->setDirty();

    if(z == 0)
        if(Chunk *c = world.findChunk(this->x, this->y, this->z - 1))
            c->setDirty();

    if(z == Chunk::SIZE - 1)
        if(Chunk *c = world.findChunk(this->x, this->y, this->z + 1))
            c->setDirty();
}

void Chunk::changeLocalBlock(const int x, const int y, const int z, const BLOCK_WDATA block)
{
    BLOCK_WDATA current_block = blocks[x][y][z];
    setLocalBlock(x, y, z, block);
    global_block_renderer.removedBlock(current_block, x, y, z, *this);
    global_block_renderer.addedBlock(block, x, y, z, *this);
}

void Chunk::changeGlobalBlockRelative(const int x, const int y, const int z, const BLOCK_WDATA block)
{
    if(inBounds(x, y, z))
        return changeLocalBlock(x, y, z, block);

    world.changeBlock(x + this->x*SIZE, y + this->y*SIZE, z + this->z*SIZE, block);
}

BLOCK_WDATA Chunk::getGlobalBlockRelative(const int x, const int y, const int z) const
{
    if(inBounds(x, y, z))
        return getLocalBlock(x, y, z);

    return world.getBlock(x + this->x*SIZE, y + this->y*SIZE, z + this->z*SIZE);
}

void Chunk::setGlobalBlockRelative(const int x, const int y, const int z, const BLOCK_WDATA block, bool set_dirty)
{
    if(inBounds(x, y, z))
        return setLocalBlock(x, y, z, block, set_dirty);

    return world.setBlock(x + this->x*SIZE, y + this->y*SIZE, z + this->z*SIZE, block, set_dirty);
}

//Ignores any non-obstacle blocks
bool Chunk::intersects(AABB &other)
{
    if(!aabb.intersects(other))
        return false;

    AABB aabb;
    aabb.low_x = abs_x;

    for(unsigned int x = 0; x < SIZE; x++, aabb.low_x += BLOCK_SIZE)
    {
        aabb.high_x = aabb.low_x + BLOCK_SIZE;

        aabb.low_y = abs_y;

        for(unsigned int y = 0; y < SIZE; y++, aabb.low_y += BLOCK_SIZE)
        {
            aabb.high_y = aabb.low_y + BLOCK_SIZE;

            aabb.low_z = abs_z;

            for(unsigned int z = 0; z < SIZE; z++, aabb.low_z += BLOCK_SIZE)
            {
                aabb.high_z = aabb.low_z + BLOCK_SIZE;

                const BLOCK_WDATA block = blocks[x][y][z];

                if(!global_block_renderer.isObstacle(block))
                    continue;

                if(global_block_renderer.isBlockShaped(block))
                {
                    if(aabb.intersects(other))
                        return true;
                }
                else if(global_block_renderer.getAABB(block, aabb.low_x, aabb.low_y, aabb.low_z).intersects(other))
                    return true;
            }
        }
    }

    return false;
}

bool Chunk::intersectsRay(GLFix rx, GLFix ry, GLFix rz, GLFix dx, GLFix dy, GLFix dz, GLFix &dist, VECTOR3 &pos, AABB::SIDE &side, bool ignore_water)
{
    GLFix shortest_dist;
    if(aabb.intersectsRay(rx, ry, rz, dx, dy, dz, shortest_dist) == AABB::NONE)
        return false;

    shortest_dist = GLFix::maxValue();

    AABB aabb;
    aabb.low_x = abs_x;

    for(unsigned int x = 0; x < SIZE; x++, aabb.low_x += BLOCK_SIZE)
    {
        aabb.high_x = aabb.low_x + BLOCK_SIZE;

        aabb.low_y = abs_y;

        for(unsigned int y = 0; y < SIZE; y++, aabb.low_y += BLOCK_SIZE)
        {
            aabb.high_y = aabb.low_y + BLOCK_SIZE;

            aabb.low_z = abs_z;

            for(unsigned int z = 0; z < SIZE; z++, aabb.low_z += BLOCK_SIZE)
            {
                aabb.high_z = aabb.low_z + BLOCK_SIZE;

                BLOCK_WDATA block = blocks[x][y][z];

                if(block == BLOCK_AIR || ((getBLOCK(block) == BLOCK_WATER || getBLOCK(block) == BLOCK_WATER_FAST) && ignore_water))
                    continue;

                AABB test = aabb;
                if(!global_block_renderer.isBlockShaped(block))
                    test = global_block_renderer.getAABB(block, aabb.low_x, aabb.low_y, aabb.low_z);

                GLFix new_dist;
                AABB::SIDE new_side = test.intersectsRay(rx, ry, rz, dx, dy, dz, new_dist);
                if(new_side != AABB::NONE)
                {
                    if(new_dist < shortest_dist)
                    {
                        pos.x = x;
                        pos.y = y;
                        pos.z = z;
                        side = new_side;
                        shortest_dist = new_dist;
                    }
                }
            }
        }
    }

    if(shortest_dist == GLFix::maxValue())
        return false;

    dist = shortest_dist;
    return true;
}

void Chunk::generate()
{
    //Everything air
    std::fill(blocks[0][0] + 0, blocks[SIZE - 1][SIZE - 1] + SIZE, BLOCK_AIR);

    // The light caches are a function of the blocks, so generating (or
    // regenerating) a chunk always invalidates them, including the flat and graph
    // worlds that return early below.
    sky_heights_valid = false;
    light_field_valid = false;

    debug("Generating chunk %d:%d:%d...\t", x, y, z);

    if(world.worldType() == World::WorldType::Flat)
    {
        if(this->y == 0)
        {
            for(int x = 0; x < SIZE; ++x)
                for(int z = 0; z < SIZE; ++z)
                {
                    blocks[x][0][z] = BLOCK_BEDROCK;
                    if(SIZE > 1)
                        blocks[x][1][z] = BLOCK_DIRT;
                    if(SIZE > 2)
                        blocks[x][2][z] = BLOCK_GRASS;
                }
        }

        // Leave everything else as air and skip terrain generation.
        debug("Done!\n");
        return;
    }

    if(world.worldType() == World::WorldType::Graph)
    {
        const int range = world.graphRange();
        const int reveal_x = world.graphRevealX();
        const int fill_depth = world.graphFillDepth();
        const bool unbounded = world.graphUnbounded();
        const bool do_reveal = world.isGraphLineSweepEnabled();

        // Terrain-like sampling, but only place the top surface as green wool.
        for(int lx = 0; lx < SIZE; ++lx)
        {
            const int gx = this->x * SIZE + lx;
            if((!unbounded && (gx < -range || gx > range)) || (do_reveal && gx > reveal_x))
                continue;

            for(int lz = 0; lz < SIZE; ++lz)
            {
                const int gz = this->z * SIZE + lz;
                if(!unbounded && (gz < -range || gz > range))
                    continue;

                const std::vector<World::GraphPoint> *column_points = nullptr;
                if(!world.graphPointsAt(gx, gz, column_points) || column_points == nullptr)
                    continue;

                for(const World::GraphPoint &p : *column_points)
                {
                    const int surface_y = p.y;
                    const BLOCK_WDATA top_block = p.block;
                    for(int k = 0; k < fill_depth; ++k)
                    {
                        const int yk = surface_y - k;
                        if(yk < 0)
                            break;
                        if(yk / SIZE == this->y)
                            blocks[lx][yk % SIZE][lz] = top_block;
                    }
                }
            }
        }

        debug("Done!\n");
        return;
    }

    const PerlinNoise &noise = world.noiseGenerator();
    const unsigned int world_seed = world.seedValue();

    // A forest is far denser than the old uniform tree pass, so this cap has to
    // leave room for one while still bounding how many canopies the CX has to
    // build at once. The per-biome density does the real work.
    constexpr int max_trees = (Chunk::SIZE * Chunk::SIZE) / 6;
    constexpr int sea_level = BiomeGen::SeaLevel;
    int trees = 0;
    unsigned int feature_rng = chunkGenerationSeed(world_seed, this->x, this->y, this->z, 0x46544e52u);

    // Surface height of every column, kept for the cave/ravine pass below so the
    // carving never has to sample the terrain noise a second time.
    int column_height[SIZE][SIZE] = {};

    for(int x = 0; x < SIZE; x++)
        for(int z = 0; z < SIZE; z++)
        {
            // Shared with the village generator so both agree on the surface.
            const int world_x = this->x * SIZE + x;
            const int world_z = this->z * SIZE + z;
            const BiomeGen::Column column = terrainColumn(noise, world_seed, world_x, world_z);
            const int height = column.height;
            column_height[x][z] = height;

            int height_left = height - this->y * Chunk::SIZE;
            int height_here = std::min(height_left, Chunk::SIZE);

            int y = 0;

            //Bottom layer of lowest chunk is bedrock
            if(this->y == 0)
                blocks[x][y++][z] = BLOCK_BEDROCK;

            for(; y < height_here; y++)
            {
                int to_surface = height_left - y;

                //Deep underground
                if(to_surface > 5)
                {
                    blocks[x][y][z] = BLOCK_STONE;
                }
                else if(to_surface == 1)
                {
                    // The biome decides what the top block is: grass, sand in a
                    // desert, river bed or beach, bare stone on a high mountain.
                    blocks[x][y][z] = column.surface;
                    if(column.surface == BLOCK_GRASS && (nextGenerationRandom(feature_rng) & 0xFFu) == 0u)
                        setGlobalBlockRelative(x, y + 1, z, getBLOCKWDATA(BLOCK_FLOWER, nextGenerationRandom(feature_rng) & 0x1u));
                }
                else
                    blocks[x][y][z] = column.subsurface;
            }

            const int local_sea_level = sea_level - this->y * Chunk::SIZE;
            int water_start = std::max(height_left, 0);
            // Keep water one block below the prior sea-line so beaches stay sand-first.
            int water_end = std::min(local_sea_level, Chunk::SIZE);
            for(int y = water_start; y < water_end; ++y)
            {
                if(blocks[x][y][z] == BLOCK_AIR)
                    blocks[x][y][z] = getBLOCKWDATA(BLOCK_WATER_FAST, 0);
            }

            // Trees are biome density, not a global noise threshold: a forest
            // is a wood, a plain has a couple of trees and a desert has none.
            const int tree_density = BiomeGen::treeDensityPercent(column.biome);
            if(trees < max_trees
                && tree_density > 0
                && height > sea_level + 1
                && height_left > 0
                && height_left <= Chunk::SIZE
                && blocks[x][height_left - 1][z] == BLOCK_GRASS
                && (nextGenerationRandom(feature_rng) % 100u) < static_cast<unsigned int>(tree_density))
            {
                makeTree(x, height_here, z);
                trees++;
            }
        }

    // Caves and ravines, carved before the ores so a vein never ends up hanging
    // in mid-air, and before the villages so a building is never hollowed out.
    carveUnderground(world_seed, column_height);

    // Generate ore veins using Minecraft-like distribution
    generateOreVeins();

    generateVillages();

    // Rare structures are generated last, so a dungeon is not accidentally
    // filled in again by the village pass and a ruin can stand on a village
    // edge without the two of them fighting over a block.
    generateStructures();

    // The light depends on the blocks that are now in place, so both caches are
    // dropped and rebuilt the first time this chunk is meshed.
    sky_heights_valid = false;
    light_field_valid = false;

    debug("Done!\n");
}

bool Chunk::saveToFile(gzFile file)
{
    if(gzfwrite(blocks, sizeof(***blocks), SIZE*SIZE*SIZE, file) == SIZE*SIZE*SIZE)
    {
        debug("Saved chunk %d:%d:%d successfully.\n", x, y, z);
        return true;
    }
    else
    {
        debug("Saving chunk %d:%d:%d failed!\n", x, y, z);
        return false;
    }
}

bool Chunk::loadFromFile(gzFile file)
{
    if(gzfread(blocks, sizeof(***blocks), SIZE*SIZE*SIZE, file) == SIZE*SIZE*SIZE)
    {
        // A chunk read from a save file has to rebuild both light caches, and
        // neither of them is in the file: the sky map comes from the terrain and
        // from the blocks, the emitter field from the blocks.
        sky_heights_valid = false;
        light_field_valid = false;
        debug("Loaded chunk %d:%d:%d successfully.\n", x, y, z);
        return true;
    }
    else
    {
        debug("Loading chunk %d:%d:%d failed!\n", x, y, z);
        return false;
    }
}

bool Chunk::gettingPowerFrom(const int x, const int y, const int z, BLOCK_SIDE side, bool ignore_redstone_wire)
{
    auto gettingStrongPowerFrom = [this, ignore_redstone_wire](const int x, const int y, const int z, BLOCK_SIDE side) {
        BLOCK_WDATA block = getGlobalBlockRelative(x, y, z);
        if(getBLOCK(block) == BLOCK_AIR || (ignore_redstone_wire && getBLOCK(block) == BLOCK_REDSTONE_WIRE))
            return false;

        return global_block_renderer.powersSide(block, side) == PowerState::StronglyPowered;
    };

    BLOCK_WDATA block = getGlobalBlockRelative(x, y, z);
    if(getBLOCK(block) == BLOCK_AIR || (ignore_redstone_wire && getBLOCK(block) == BLOCK_REDSTONE_WIRE))
        return false;

    if(global_block_renderer.powersSide(block, side) != PowerState::NotPowered)
        return true;

    if(!global_block_renderer.isOpaque(block))
        return false;

    return gettingStrongPowerFrom(x-1, y, z, BLOCK_RIGHT)
        || gettingStrongPowerFrom(x+1, y, z, BLOCK_LEFT)
        || gettingStrongPowerFrom(x, y-1, z, BLOCK_TOP)
        || gettingStrongPowerFrom(x, y+1, z, BLOCK_BOTTOM)
        || gettingStrongPowerFrom(x, y, z-1, BLOCK_BACK)
        || gettingStrongPowerFrom(x, y, z+1, BLOCK_FRONT);
}

bool Chunk::isBlockPowered(const int x, const int y, const int z, bool ignore_redstone_wire)
{
    return gettingPowerFrom(x-1, y, z, BLOCK_RIGHT, ignore_redstone_wire)
        || gettingPowerFrom(x+1, y, z, BLOCK_LEFT, ignore_redstone_wire)
        || gettingPowerFrom(x, y-1, z, BLOCK_TOP, ignore_redstone_wire)
        || gettingPowerFrom(x, y+1, z, BLOCK_BOTTOM, ignore_redstone_wire)
        || gettingPowerFrom(x, y, z-1, BLOCK_BACK, ignore_redstone_wire)
        || gettingPowerFrom(x, y, z+1, BLOCK_FRONT, ignore_redstone_wire);
}

void Chunk::generateOreVeins()
{
    // Generate ore veins after terrain filling by sampling world Y with distribution weight.
    for(int ore_idx = 0; ore_idx < OreDistributions::ore_count; ++ore_idx)
    {
        const OreDistribution &ore_dist = OreDistributions::ore_list[ore_idx];

        for(int vein_attempt = 0; vein_attempt < ore_dist.veins_per_chunk; ++vein_attempt)
        {
            unsigned int seed = chunkGenerationSeed(
                world.seedValue(), x, y, z,
                static_cast<unsigned int>(ore_idx) * 2654435761u ^ static_cast<unsigned int>(vein_attempt));
            auto nextRand = [&seed]() {
                return nextGenerationRandom(seed);
            };

            const int y_range = ore_dist.y_max - ore_dist.y_min;
            if(y_range < 0)
                continue;

            int world_y = ore_dist.y_min + static_cast<int>(nextRand() % static_cast<unsigned int>(y_range + 1));
            for(int i = 0; i < 6; ++i)
            {
                const int candidate = ore_dist.y_min + static_cast<int>(nextRand() % static_cast<unsigned int>(y_range + 1));
                const float weight = ore_dist.distribution == OreDistributionType::Triangle
                    ? triangleDistributionProbability(candidate, ore_dist)
                    : 1.0f;
                const float roll = static_cast<float>(nextRand() & 0xFFFFu) / 65535.0f;
                if(roll <= weight)
                {
                    world_y = candidate;
                    break;
                }
            }

            const int local_y = world_y - y * Chunk::SIZE;
            if(local_y < 0 || local_y >= Chunk::SIZE)
                continue;

            const int center_x = static_cast<int>(nextRand() % Chunk::SIZE);
            const int center_z = static_cast<int>(nextRand() % Chunk::SIZE);
            generateSingleOreVein(ore_dist, center_x, local_y, center_z, nextRand());
        }
    }
}

// Note: triangleDistributionProbability is now implemented in oregeneration.cpp
// This local version is not needed - removed to avoid redefinition

void Chunk::generateSingleOreVein(const OreDistribution &ore_dist, int center_x, int center_y, int center_z, unsigned int seed)
{
    auto nextRand = [&seed]() {
        return nextGenerationRandom(seed);
    };

    int x = center_x;
    int y = center_y;
    int z = center_z;
    const int target_blocks = std::max(1, 1 + static_cast<int>(nextRand() % static_cast<unsigned int>(std::max(1, ore_dist.vein_size))));

    int placed = 0;
    const int tries = target_blocks * 8;
    for(int i = 0; i < tries && placed < target_blocks; ++i)
    {
        x += static_cast<int>(nextRand() % 3) - 1;
        y += static_cast<int>(nextRand() % 3) - 1;
        z += static_cast<int>(nextRand() % 3) - 1;

        if(x < 0 || x >= Chunk::SIZE || y < 0 || y >= Chunk::SIZE || z < 0 || z >= Chunk::SIZE)
            continue;

        if(getBLOCK(blocks[x][y][z]) != BLOCK_STONE)
            continue;

        blocks[x][y][z] = ore_dist.ore_block;
        ++placed;
    }
}

// Carves the caves and ravines out of the chunk.
//
// Both are underground-only features: the top block of every column survives, so
// neither can open the sky, drain a lake or leave a tree standing over a hole.
// They are pure functions of the world seed and the world coordinates, so two
// neighbouring chunks always carve the same tunnel across the border between
// them and none of this needs any cross-chunk bookkeeping.
void Chunk::carveUnderground(const unsigned int world_seed, const int column_height[SIZE][SIZE])
{
    for(int x = 0; x < SIZE; x++)
        for(int z = 0; z < SIZE; z++)
        {
            const int world_x = this->x * SIZE + x;
            const int world_z = this->z * SIZE + z;
            const int height = column_height[x][z];

            BiomeGen::Ravine ravine;
            BiomeGen::ravineAt(world_seed, world_x, world_z, height, ravine);

            for(int y = 0; y < SIZE; y++)
            {
                const int world_y = this->y * SIZE + y;

                // Bedrock stays, and so does everything from one block below the
                // surface up.
                if(world_y <= 1 || world_y >= height - 1)
                    continue;

                const BLOCK type = getBLOCK(blocks[x][y][z]);
                // Only real rock is carved. Water, air, anything already dug out
                // (and anything a village left behind) stays exactly as it is.
                if(type != BLOCK_STONE && type != BLOCK_DIRT && type != BLOCK_SAND)
                    continue;

                const bool in_ravine = ravine.present && world_y >= ravine.lowest && world_y <= ravine.highest;
                if(in_ravine || BiomeGen::caveAt(world_seed, world_x, world_y, world_z))
                    blocks[x][y][z] = BLOCK_AIR;
            }
        }
}

void Chunk::generateVillages()
{
    const int freq = Village::frequency();
    if(freq <= Village::FrequencyOff)
        return;

    const unsigned int world_seed = world.seedValue();
    const PerlinNoise &noise = world.noiseGenerator();

    const int base_x = this->x * SIZE;
    const int base_z = this->z * SIZE;

    // A village reaches at most CellJitter + Radius + 2 blocks from its cell
    // centre, so a chunk near a cell border can be touched by a neighbouring
    // cell's village. Visit every cell that could overlap this chunk.
    const int reach = Village::CellJitter + Village::Radius + 2;
    const int cell_x_min = floorDiv(base_x - reach, Village::CellBlocks);
    const int cell_x_max = floorDiv(base_x + SIZE - 1 + reach, Village::CellBlocks);
    const int cell_z_min = floorDiv(base_z - reach, Village::CellBlocks);
    const int cell_z_max = floorDiv(base_z + SIZE - 1 + reach, Village::CellBlocks);

    VillageWriteContext context;
    context.chunk = this;
    context.base_x = base_x;
    context.base_y = this->y * SIZE;
    context.base_z = base_z;

    for(int cell_z = cell_z_min; cell_z <= cell_z_max; ++cell_z)
        for(int cell_x = cell_x_min; cell_x <= cell_x_max; ++cell_x)
        {
            Village::Plan plan;
            if(!resolveVillagePlan(world_seed, freq, cell_x, cell_z, noise, plan))
                continue;

            Village::emitChunk(plan, this->x, this->y, this->z, &Chunk::villageWriteBlock, &context);
        }
}

// How much a surface structure's footprint may slope before it is rejected. A
// hut whose corner hangs over a cliff reads as a bug, so a ruin and a temple are
// only placed on ground that is flat to within this many blocks.
static constexpr int surface_flatness_tolerance = 2;

// A surface structure stands on the ground; a dungeon is buried, so only it is
// happy with any surface shape. This needs the terrain function, which is why it
// is here and not in structuregen.cpp.
static bool structureSiteIsFlat(const PerlinNoise &noise, unsigned int world_seed, const Structures::Plan &plan)
{
    if(plan.kind == Structures::KindDungeon)
        return true;

    const int reach = (plan.kind == Structures::KindRuin) ? 3 : 4;
    int lowest = 0, highest = 0;

    // A 3x3 grid over the footprint: the centre, the four corners and the four
    // edge midpoints.
    for(int dz = -1; dz <= 1; ++dz)
        for(int dx = -1; dx <= 1; ++dx)
        {
            const int height = terrainSurfaceHeight(noise, world_seed, plan.origin_x + dx * reach, plan.origin_z + dz * reach);
            if(dx == -1 && dz == -1)
            {
                lowest = height;
                highest = height;
                continue;
            }
            if(height < lowest)
                lowest = height;
            if(height > highest)
                highest = height;
        }

    return (highest - lowest) <= surface_flatness_tolerance;
}

// The plan of one cell's structure, resolved from the surface its candidate
// sits on. This is the only place a structure samples the terrain, so generation
// and the chest lookup below can never disagree about where one is.
static bool resolveStructurePlan(const PerlinNoise &noise, unsigned int world_seed, int cell_x, int cell_z, Structures::Plan &out)
{
    if(!Structures::cellHasStructure(world_seed, cell_x, cell_z))
        return false;

    for(int candidate = 0; candidate < Structures::CandidateCount; ++candidate)
    {
        int site_x = 0, site_z = 0;
        Structures::cellCandidate(world_seed, cell_x, cell_z, candidate, site_x, site_z);

        // The same column function the terrain uses, so the site's height and
        // biome are the ones the world actually has.
        const BiomeGen::Column column = terrainColumn(noise, world_seed, site_x, site_z);
        if(!Structures::planStructure(world_seed, cell_x, cell_z, candidate, column.height, column.biome, out))
            continue;

        if(!structureSiteIsFlat(noise, world_seed, out))
        {
            out = Structures::Plan();
            continue;
        }

        return true;
    }

    return false;
}

void Chunk::generateStructures()
{
    const unsigned int world_seed = world.seedValue();
    const PerlinNoise &noise = world.noiseGenerator();

    const int base_x = this->x * SIZE;
    const int base_z = this->z * SIZE;

    // A structure reaches at most MaxReach blocks from its origin, so a chunk
    // near a cell border can be touched by a neighbouring cell's structure.
    const int reach = Structures::MaxReach;
    const int cell_x_min = floorDiv(base_x - reach, Structures::CellBlocks);
    const int cell_x_max = floorDiv(base_x + SIZE - 1 + reach, Structures::CellBlocks);
    const int cell_z_min = floorDiv(base_z - reach, Structures::CellBlocks);
    const int cell_z_max = floorDiv(base_z + SIZE - 1 + reach, Structures::CellBlocks);

    StructureWriteContext context;
    context.chunk = this;
    context.base_x = base_x;
    context.base_y = this->y * SIZE;
    context.base_z = base_z;

    for(int cell_z = cell_z_min; cell_z <= cell_z_max; ++cell_z)
        for(int cell_x = cell_x_min; cell_x <= cell_x_max; ++cell_x)
        {
            Structures::Plan plan;
            if(!resolveStructurePlan(noise, world_seed, cell_x, cell_z, plan))
                continue;

            Structures::emitChunk(plan, this->x, this->y, this->z, &Chunk::structureWriteBlock, &context);
        }
}

// Emits one structure block into this chunk. The generator clips to the chunk
// bounds before calling, so this only has to place the block in the grid.
void Chunk::structureWriteBlock(void *context, int world_x, int world_y, int world_z, uint16_t block)
{
    StructureWriteContext *ctx = static_cast<StructureWriteContext *>(context);
    if(ctx == nullptr || ctx->chunk == nullptr)
        return;

    const int local_x = world_x - ctx->base_x;
    const int local_y = world_y - ctx->base_y;
    const int local_z = world_z - ctx->base_z;
    if(!inBounds(local_x, local_y, local_z))
        return;

    ctx->chunk->blocks[local_x][local_y][local_z] = block;
}

// The chest of a structure, if the position is one. Only called when a chest is
// first touched, so the cost of the site search is paid once per chest, not per
// chunk.
bool structureChestAt(int world_x, int world_y, int world_z, Structures::Plan &out, int &out_index)
{
    // Only a normal terrain world generates structures at all, so a chest in a
    // flat or graph world is always a player's own chest.
    if(world.worldType() != World::WorldType::Terrain)
        return false;

    const unsigned int world_seed = world.seedValue();
    const PerlinNoise &noise = world.noiseGenerator();

    const int reach = Structures::MaxReach;
    const int cell_x_min = floorDiv(world_x - reach, Structures::CellBlocks);
    const int cell_x_max = floorDiv(world_x + reach, Structures::CellBlocks);
    const int cell_z_min = floorDiv(world_z - reach, Structures::CellBlocks);
    const int cell_z_max = floorDiv(world_z + reach, Structures::CellBlocks);

    for(int cell_z = cell_z_min; cell_z <= cell_z_max; ++cell_z)
        for(int cell_x = cell_x_min; cell_x <= cell_x_max; ++cell_x)
        {
            Structures::Plan plan;
            if(!resolveStructurePlan(noise, world_seed, cell_x, cell_z, plan))
                continue;

            for(int i = 0; i < Structures::chestCount(plan); ++i)
            {
                int chest_x = 0, chest_y = 0, chest_z = 0;
                if(!Structures::chestPosition(plan, i, chest_x, chest_y, chest_z))
                    continue;
                if(chest_x == world_x && chest_y == world_y && chest_z == world_z)
                {
                    out = plan;
                    out_index = i;
                    return true;
                }
            }
        }

    return false;
}

// Re-resolves the villages around a block column. Uses the same cache and site
// search as generation, so the plan it registers is byte-identical to the one
// the terrain was generated with.
void registerVillagesNearColumn(int world_x, int world_z)
{
    const int freq = Village::frequency();
    if(freq <= Village::FrequencyOff)
        return;

    const unsigned int world_seed = world.seedValue();
    const PerlinNoise &noise = world.noiseGenerator();

    const int reach = Village::CellJitter + Village::Radius + 2;
    const int cell_x_min = floorDiv(world_x - reach, Village::CellBlocks);
    const int cell_x_max = floorDiv(world_x + reach, Village::CellBlocks);
    const int cell_z_min = floorDiv(world_z - reach, Village::CellBlocks);
    const int cell_z_max = floorDiv(world_z + reach, Village::CellBlocks);

    for(int cell_z = cell_z_min; cell_z <= cell_z_max; ++cell_z)
        for(int cell_x = cell_x_min; cell_x <= cell_x_max; ++cell_x)
        {
            Village::Plan plan;
            resolveVillagePlan(world_seed, freq, cell_x, cell_z, noise, plan);
        }
}

// Emits one village block into this chunk. The generator clips to the chunk
// bounds before calling, so this only has to place the block in the grid.
void Chunk::villageWriteBlock(void *context, int world_x, int world_y, int world_z, uint16_t block)
{
    VillageWriteContext *ctx = static_cast<VillageWriteContext *>(context);
    if(ctx == nullptr || ctx->chunk == nullptr)
        return;

    const int local_x = world_x - ctx->base_x;
    const int local_y = world_y - ctx->base_y;
    const int local_z = world_z - ctx->base_z;
    if(!inBounds(local_x, local_y, local_z))
        return;

    ctx->chunk->blocks[local_x][local_y][local_z] = block;
}

void Chunk::makeTree(unsigned int x, unsigned int y, unsigned int z)
{
    // Minecraft-like oak template (10 layers, 7x7 each).
    static const int tree[10][7][7] = {
        {{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 4, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 }},
        {{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 4, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 }},
        {{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 4, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 }},
        {{ 0, 0, 0, 5, 0, 0, 0 },{ 0, 0, 5, 5, 5, 0, 0 },{ 0, 5, 5, 5, 5, 5, 0 },{ 5, 5, 5, 4, 5, 5, 5 },{ 0, 5, 5, 5, 5, 5, 0 },{ 0, 0, 5, 5, 5, 0, 0 },{ 0, 0, 0, 5, 0, 0, 0 }},
        {{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 5, 5, 5, 0, 0 },{ 0, 0, 5, 4, 5, 0, 0 },{ 0, 0, 5, 5, 5, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 }},
        {{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 5, 5, 5, 0, 0 },{ 0, 5, 5, 5, 5, 5, 0 },{ 0, 5, 5, 4, 5, 5, 0 },{ 0, 5, 5, 5, 5, 5, 0 },{ 0, 0, 5, 5, 5, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 }},
        {{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 5, 0, 0, 0 },{ 0, 0, 5, 4, 5, 0, 0 },{ 0, 0, 0, 5, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 }},
        {{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 5, 0, 0, 0 },{ 0, 0, 5, 5, 5, 0, 0 },{ 0, 5, 5, 4, 5, 5, 0 },{ 0, 0, 5, 5, 5, 0, 0 },{ 0, 0, 0, 5, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 }},
        {{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 5, 0, 0, 0 },{ 0, 0, 5, 5, 5, 0, 0 },{ 0, 0, 0, 5, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 }},
        {{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 5, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 },{ 0, 0, 0, 0, 0, 0, 0 }},
    };

    int max_world_y = World::HEIGHT * Chunk::SIZE;
    int base_y = static_cast<int>(y) + this->y * Chunk::SIZE;
    if(base_y + 10 >= max_world_y)
        return;

    int trunk_x = static_cast<int>(x);
    int trunk_z = static_cast<int>(z);

    for(int layer = 0; layer < 10; ++layer)
        for(int dz = -3; dz <= 3; ++dz)
            for(int dx = -3; dx <= 3; ++dx)
            {
                int block = tree[layer][dz + 3][dx + 3];
                if(block == 0)
                    continue;

                int tx = trunk_x + dx;
                int ty = static_cast<int>(y) + layer;
                int tz = trunk_z + dz;

                if(block == BLOCK_WOOD)
                {
                    setGlobalBlockRelative(tx, ty, tz, BLOCK_WOOD);
                }
                else
                {
                    // During generation, unloaded neighboring chunks read as stone;
                    // always queue leaf placement so full canopies appear at chunk edges.
                    setGlobalBlockRelative(tx, ty, tz, BLOCK_LEAVES);
                }
            }
}

void Chunk::spawnDestructionParticles(const int x, const int y, const int z)
{
    if(!inBounds(x, y, z))
        return;

    const auto block = getLocalBlock(x, y, z);
    Particle p;
    p.size = 14;
    p.tae = global_block_renderer.materialTexture(block).current;

    // Use the center quarter of the texture
    const int tex_width = p.tae.right - p.tae.left,
              tex_height = p.tae.bottom - p.tae.top;
    p.tae.left += tex_width / 4;
    p.tae.right -= tex_width / 4;
    p.tae.top += tex_height / 4;
    p.tae.bottom -= tex_height / 4;

    // Random value between 0 and max (inclusive)
    const auto randMax = [](GLFix max) { return max * (rand() & 0xFF) / 0xFF; };

    // Get the center of the block contents (chunk relative coordinates)
    const auto aabb = global_block_renderer.getAABB(block, x * BLOCK_SIZE, y * BLOCK_SIZE, z * BLOCK_SIZE);
    const auto center = VECTOR3{(aabb.low_x + aabb.high_x) / 2, (aabb.low_y + aabb.high_y) / 2, (aabb.low_z + aabb.high_z) / 2};

    // Spawn four particles at the center with random velocity and offset
    for(int i = 0; i < 4; ++i)
    {
        p.vel = {randMax(10) - 5, randMax(5), randMax(10) - 5};
        p.pos = center;
        p.pos.x += randMax(100) - 50;
        p.pos.y += randMax(100) - 50;
        p.pos.z += randMax(100) - 50;
        addParticle(p);
    }
}

void drawLoadingtext(const int i)
{
    static int count = 0;
    static bool shown = false;

    if(i == -1)
    {
        count = 0;
        shown = false;
        return;
    }

    if(shown)
        return;

    count += 1;
    if(count < i)
        return;

    shown = true;

    // Vanilla's loading screen: the dirt every menu is drawn on, the label and a
    // progress bar. A blocking load cannot say how far along it is, so the bar is
    // drawn full -- it spans the whole wait. The screen is flushed here rather than
    // left for the end of the frame, because the load returns before the frame's
    // own render fills over it.
    if(Task::screen == nullptr)
        return;

    MenuUI::drawLoadingScreen(*Task::screen, MenuUI::loadingLabel, 100);
    nglDisplay();
}
