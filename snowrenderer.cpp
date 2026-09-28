#include "snowrenderer.h"

#include "snowcover.h"

// The snow layer is drawn with the texture packs' own tile at atlas (5,7), which
// terrain.cpp maps to BLOCK_SNOW for every face. It is a light grey speckled
// tile that no block used before, so all three packs get snow without any art
// being painted for it.
static const TextureAtlasEntry &snowTile()
{
    return block_textures[BLOCK_SNOW][BLOCK_TOP].current;
}

GLFix SnowRenderer::layerHeight(const BLOCK_WDATA block)
{
    const int layers = SnowCover::layersOf(static_cast<uint8_t>(getBLOCKDATA(block)));
    return GLFix(layers * SnowCover::LayerSixteenths * (BLOCK_SIZE / 16));
}

bool SnowRenderer::sideCovered(const BLOCK_WDATA block, const int local_x, const int local_y, const int local_z,
                              const BLOCK_SIDE side, Chunk &c)
{
    int dx = 0, dy = 0, dz = 0;
    switch(side)
    {
    case BLOCK_FRONT: dz = -1; break;
    case BLOCK_BACK: dz = 1; break;
    case BLOCK_LEFT: dx = -1; break;
    case BLOCK_RIGHT: dx = 1; break;
    case BLOCK_TOP: dy = 1; break;
    case BLOCK_BOTTOM: dy = -1; break;
    }

    const BLOCK_WDATA neighbour = c.getGlobalBlockRelative(local_x + dx, local_y + dy, local_z + dz);

    if(getBLOCK(neighbour) == BLOCK_SNOW)
    {
        // A deeper layer next door buries this face; a shallower one does not, so
        // the step between them stays visible.
        return SnowCover::layersOf(static_cast<uint8_t>(getBLOCKDATA(neighbour)))
            >= SnowCover::layersOf(static_cast<uint8_t>(getBLOCKDATA(block)));
    }

    return global_block_renderer.isOpaque(neighbour);
}

void SnowRenderer::renderSpecialBlock(const BLOCK_WDATA block, GLFix x, GLFix y, GLFix z, Chunk &c)
{
    const int local_x = x / BLOCK_SIZE, local_y = y / BLOCK_SIZE, local_z = z / BLOCK_SIZE;

    const GLFix slab_top = y + layerHeight(block);
    const GLFix max_x = x + BLOCK_SIZE, max_z = z + BLOCK_SIZE;
    const TextureAtlasEntry &tile = snowTile();

    // Every face is drawn from both sides, the way the bed's are: the layer is a
    // slab the camera can get under, and a face that culled the wrong way would
    // simply vanish as the player walked around it.
    if(!sideCovered(block, local_x, local_y, local_z, BLOCK_FRONT, c))
    {
        const COLOR color = BlockRenderer::computeLighting(BLOCK_FRONT, TEXTURE_DRAW_BACKFACE);
        c.addUnalignedVertex({x, y, z, tile.left, tile.bottom, color});
        c.addUnalignedVertex({x, slab_top, z, tile.left, tile.top, color});
        c.addUnalignedVertex({max_x, slab_top, z, tile.right, tile.top, color});
        c.addUnalignedVertex({max_x, y, z, tile.right, tile.bottom, color});
    }

    if(!sideCovered(block, local_x, local_y, local_z, BLOCK_BACK, c))
    {
        const COLOR color = BlockRenderer::computeLighting(BLOCK_BACK, TEXTURE_DRAW_BACKFACE);
        c.addUnalignedVertex({max_x, y, max_z, tile.left, tile.bottom, color});
        c.addUnalignedVertex({max_x, slab_top, max_z, tile.left, tile.top, color});
        c.addUnalignedVertex({x, slab_top, max_z, tile.right, tile.top, color});
        c.addUnalignedVertex({x, y, max_z, tile.right, tile.bottom, color});
    }

    if(!sideCovered(block, local_x, local_y, local_z, BLOCK_RIGHT, c))
    {
        const COLOR color = BlockRenderer::computeLighting(BLOCK_RIGHT, TEXTURE_DRAW_BACKFACE);
        c.addUnalignedVertex({max_x, y, z, tile.right, tile.bottom, color});
        c.addUnalignedVertex({max_x, slab_top, z, tile.right, tile.top, color});
        c.addUnalignedVertex({max_x, slab_top, max_z, tile.left, tile.top, color});
        c.addUnalignedVertex({max_x, y, max_z, tile.left, tile.bottom, color});
    }

    if(!sideCovered(block, local_x, local_y, local_z, BLOCK_LEFT, c))
    {
        const COLOR color = BlockRenderer::computeLighting(BLOCK_LEFT, TEXTURE_DRAW_BACKFACE);
        c.addUnalignedVertex({x, y, max_z, tile.left, tile.bottom, color});
        c.addUnalignedVertex({x, slab_top, max_z, tile.left, tile.top, color});
        c.addUnalignedVertex({x, slab_top, z, tile.right, tile.top, color});
        c.addUnalignedVertex({x, y, z, tile.right, tile.bottom, color});
    }

    {
        const COLOR color = BlockRenderer::computeLighting(BLOCK_TOP, TEXTURE_DRAW_BACKFACE);
        c.addUnalignedVertex({x, slab_top, z, tile.left, tile.bottom, color});
        c.addUnalignedVertex({x, slab_top, max_z, tile.left, tile.top, color});
        c.addUnalignedVertex({max_x, slab_top, max_z, tile.right, tile.top, color});
        c.addUnalignedVertex({max_x, slab_top, z, tile.right, tile.bottom, color});
    }
}

void SnowRenderer::geometryNormalBlock(const BLOCK_WDATA /*block*/, const int local_x, const int local_y, const int local_z,
                                       const BLOCK_SIDE side, Chunk &c)
{
    // The layer's own faces are drawn by renderSpecialBlock, with one exception:
    // its underside, which is inside the ground and therefore not worth a quad
    // unless there is nothing under it. This is the case the chunk asks for when
    // the cell below is open air, exactly as the cake does it, so a layer left
    // floating (by /setblock, or by the ground being dug out) is not see-through
    // from underneath.
    if(side != BLOCK_BOTTOM)
        return;

    renderNormalBlockSide(local_x, local_y, local_z, side, snowTile(), c);
}

AABB SnowRenderer::getAABB(const BLOCK_WDATA block, GLFix x, GLFix y, GLFix z)
{
    return {x, y, z, x + BLOCK_SIZE, y + layerHeight(block), z + BLOCK_SIZE};
}

void SnowRenderer::drawPreview(const BLOCK_WDATA /*block*/, TEXTURE &dest, int x, int y)
{
    const TextureAtlasEntry tex = block_textures[BLOCK_SNOW][BLOCK_FRONT].resized;
    BlockRenderer::drawTextureAtlasEntry(*terrain_resized, tex, dest, x, y);
}

const char *SnowRenderer::getName(const BLOCK_WDATA /*block*/)
{
    return block_names[BLOCK_SNOW];
}
