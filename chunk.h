#ifndef CHUNK_H
#define CHUNK_H

#include <cstdint>
#include <vector>
#include <tuple>
#include <zlib.h>

#include "gl.h"
#include "gldrawarray.h"
#include "terrain.h"
#include "aabb.h"
#include "particle.h"
#include "oregeneration.h"
#include "structuregen.h"
#include "blocklight.h"

class World;

class Chunk
{
public:
    Chunk(int x, int y, int z);
    void logic(bool ticks_enabled);
    void render();
    void setDirty(bool dirty = true) { build_dirty = dirty; }
    bool isDirty() { return build_dirty; }
    bool isBuildDirty() const { return build_dirty; }
    /** Queue membership bookkeeping for World::build_queue (see world.cpp). */
    bool isBuildQueued() const { return build_queued; }
    void setBuildQueued(bool queued) { build_queued = queued; }
    void buildGeometryAsync(); // Used by build queue
    void swapMeshes(); // Swap build and render mesh buffers
    BLOCK_WDATA getLocalBlock(const int x, const int y, const int z) const;
    void setLocalBlock(const int x, const int y, const int z, const BLOCK_WDATA block, bool set_dirty = true);
    void changeLocalBlock(const int x, const int y, const int z, const BLOCK_WDATA block); //Calls removeBlock and addBlock
    void changeGlobalBlockRelative(const int x, const int y, const int z, const BLOCK_WDATA block);
    BLOCK_WDATA getGlobalBlockRelative(const int x, const int y, const int z) const;
    void setGlobalBlockRelative(const int x, const int y, const int z, const BLOCK_WDATA block, bool set_dirty = true);
    AABB &getAABB() { return aabb; }
    bool intersects(AABB &other);
    bool intersectsRay(GLFix x, GLFix y, GLFix z, GLFix dx, GLFix dy, GLFix dz, GLFix &dist, VECTOR3 &pos, AABB::SIDE &side, bool ignore_water);
    void generate();
    bool saveToFile(gzFile file);
    bool loadFromFile(gzFile file);

    //Redstone power: See wirerenderer.cpp for details
    //Whether the block receives power from the specified side.
    bool gettingPowerFrom(const int x, const int y, const int z, BLOCK_SIDE side, bool ignore_redstone_wire = false);
    //Whether the block receives power directly or indirectly.
    bool isBlockPowered(const int x, const int y, const int z, bool ignore_redstone_wire = false);

    GLFix absX() { return abs_x; }
    GLFix absY() { return abs_y; }
    GLFix absZ() { return abs_z; }

    //Used by BlockRenderers, had to make it public because friendship is not inheritable
    void addAlignedVertex(const int x, const int y, const int z, GLFix u, GLFix v, const COLOR c);

    //Same as addAlignedVertex, but terrain_quad is the bound texture
    void addAlignedVertexQuad(const int x, const int y, const int z, GLFix u, GLFix v, const COLOR c);

    //Same as addAlignedVertex, but with nglForceColor on
    void addAlignedVertexForceColor(const int x, const int y, const int z, GLFix u, GLFix v, const COLOR c);

    // For unaligned vertices: If set, the two triangles in the quad do backface culling independently.
    // This is necessary when they're not on the same plane.
    static constexpr COLOR INDEPENDENT_TRIS = 0x0001;

    //Doesn't have to be aligned, terrain_current is bound
    void addUnalignedVertex(const GLFix x, const GLFix y, const GLFix z, const GLFix u, const GLFix v, const COLOR c);
    void addUnalignedVertex(const VERTEX &v);

    //The callback is called on every frame
    struct Animation {
        GLFix x, y, z;
        void (*animate)(GLFix x, GLFix y, GLFix z, Chunk &c);
    };
    void addAnimation(const Animation &animation);

    // Add a particle. It lives until it removes itself in Particle::logic
    void addParticle(const Particle &particle);

    // Spawn particles for destroying the block at that offset
    void spawnDestructionParticles(const int x, const int y, const int z);

    //Don't render something twice
    void setLocalBlockSideRendered(const int x, const int y, const int z, const BLOCK_SIDE_BITFIELD side);
    bool isLocalBlockSideRendered(const int x, const int y, const int z, const BLOCK_SIDE_BITFIELD side);

    static constexpr int SIZE = 8;

    const int x, y, z;
    int tick_counter = 1; //1 to trigger a tick the next frame

private:
    //Terrain generation
    void makeTree(unsigned int x, unsigned int y, unsigned int z);
    // Carves the caves and ravines out of the chunk. `column_height` holds the
    // surface height of every column, sampled during terrain generation.
    void carveUnderground(unsigned int world_seed, const int column_height[SIZE][SIZE]);
    void generateOreVeins(); // Generate ore veins with Minecraft-like distribution
    void generateSingleOreVein(const OreDistribution &ore_dist, int center_x, int center_y, int center_z, unsigned int seed);

    //Procedural villages: writes only the blocks of overlapping villages that fall in this chunk.
    void generateVillages();
    struct VillageWriteContext
    {
        Chunk *chunk;
        int base_x, base_y, base_z; // chunk origin in world block coordinates
    };
    static void villageWriteBlock(void *context, int world_x, int world_y, int world_z, uint16_t block);

    //Per-block lighting (chunklight.cpp, plus the sky map in chunk.cpp): the
    //shade of a vertex from how deep it sits under the sky and from the blocks
    //that glow. Baked when the mesh is built, so nothing of it runs per frame.
    COLOR litColor(const COLOR color, const int x, const int y, const int z);
    int lightLevel(const int x, const int y, const int z);
    int localLightLevel(const int x, const int y, const int z);
    void rebuildLightField();
    int skyHeightOf(const int local_x, const int local_z);
    int columnSkyHeight(const int world_x, const int world_z) const;
    void rebuildSkyHeights();

    // Highest sky-blocking block of every column, and the light the chunk's own
    // emitters spread. Both are rebuilt lazily -- each needs work that generation
    // and a block change have already paid for -- and neither is saved: they are
    // a function of the blocks.
    uint8_t column_sky_height[SIZE][SIZE];
    bool sky_heights_valid = false;
    uint8_t block_light[BlockLight::Field::Cells];
    bool light_field_valid = false;

    //Procedural dungeons, ruins and temples: same contract as the villages, and
    //run after them so a structure always wins where the two overlap.
    void generateStructures();
    struct StructureWriteContext
    {
        Chunk *chunk;
        int base_x, base_y, base_z; // chunk origin in world block coordinates
    };
    static void structureWriteBlock(void *context, int world_x, int world_y, int world_z, uint16_t block);

    //Data
    unsigned int getPosition(unsigned int x, unsigned int y, unsigned int z);

    //Rendering
    void geometrySpecialBlock(BLOCK_WDATA block, unsigned int x, unsigned int y, unsigned int z, BLOCK_SIDE side);
    void buildGeometry(); // Builds into build_* buffers

    //Data
    const GLFix abs_x, abs_y, abs_z;
    AABB aabb;
    BLOCK_WDATA blocks[SIZE][SIZE][SIZE];

    //Rendering - double buffered (render_* is displayed, build_* is being built)
    bool build_dirty = true;
    bool build_complete = false;
    //Set while this chunk sits in World::build_queue, so queue membership is an
    //O(1) test instead of walking the queue for every dirty chunk each frame.
    bool build_queued = false;
    static int pos_indices[SIZE + 1][SIZE + 1][SIZE + 1];
    BLOCK_SIDE_BITFIELD sides_rendered[SIZE][SIZE][SIZE] = {}; //It could be that other chunks already rendered parts of our blocks
    
    // Render mesh (displayed each frame)
    std::vector<VECTOR3> positions;
    std::vector<ProcessedPosition> positions_processed;
    std::vector<IndexedVertex> vertices, vertices_quad, vertices_color;
    std::vector<VERTEX> vertices_unaligned; //The optimized drawing with indices doesn't work with unaligned positions
    
    // Build mesh (built asynchronously by build queue)
    std::vector<VECTOR3> build_positions;
    std::vector<ProcessedPosition> build_positions_processed;
    std::vector<IndexedVertex> build_vertices, build_vertices_quad, build_vertices_color;
    std::vector<VERTEX> build_vertices_unaligned;
    
    std::vector<Animation> animations;
    std::vector<Particle> particles;
};

//Doesn't really belong here, but still more than everywhere else
void drawLoadingtext(const int i);

/**
 * Resolves and registers the village plans whose cell could overlap the given
 * block column. Chunks loaded from a save file never run generate(), so the
 * villager spawner calls this once to repopulate the plan registry.
 */
void registerVillagesNearColumn(int world_x, int world_z);

/**
 * True when a world block position is a structure's chest, in which case `out`
 * holds that structure's plan and `out_index` the index of the chest in it. The
 * chest store uses this to fill the chest the first time it is touched: the
 * contents are a pure function of the plan, so nothing has to be saved for a
 * chest the player never finds.
 */
bool structureChestAt(int world_x, int world_y, int world_z, Structures::Plan &out, int &out_index);

#endif // CHUNK_H
