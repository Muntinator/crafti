#ifndef SNOWRENDERER_H
#define SNOWRENDERER_H

#include "blockrenderer.h"

/**
 * The snow layer: a slab whose height is its data byte, drawn with the texture
 * pack's own light tile (atlas (5,7), see terrain.cpp).
 *
 * It is drawn like the bed, as a hand-built slab rather than as a cube, but it
 * has none of the bed's orientation: every snow block is the same shape, and
 * only the depth changes. The depth comes from snowcover.h, which is also what
 * decides where snow appears and when it goes away.
 *
 * Two deliberate choices:
 *
 *  - **it is not an obstacle.** The engine has no auto step-up, so a block that
 *    quietly appears under a standing player and collides would trap them. Snow
 *    is a surface you walk over here, and the AABB is still the real slab so the
 *    layer can be aimed at with the crosshair and broken.
 *  - **it is not opaque.** The block under it and the blocks beside it keep their
 *    faces: buried snow therefore costs nothing extra to draw, and the ground
 *    does not lose its grass because one step of snow landed on it.
 */
class SnowRenderer : public DumbBlockRenderer
{
public:
    virtual void renderSpecialBlock(const BLOCK_WDATA block, GLFix x, GLFix y, GLFix z, Chunk &c) override;
    virtual void geometryNormalBlock(const BLOCK_WDATA block, const int local_x, const int local_y, const int local_z, const BLOCK_SIDE side, Chunk &c) override;
    virtual bool isOpaque(const BLOCK_WDATA /*block*/) override { return false; }
    virtual bool isObstacle(const BLOCK_WDATA /*block*/) override { return false; }
    virtual bool isOriented(const BLOCK_WDATA /*block*/) override { return false; }
    virtual bool isFullyOriented(const BLOCK_WDATA /*block*/) override { return false; }

    virtual bool isBlockShaped(const BLOCK_WDATA /*block*/) override { return false; }
    virtual AABB getAABB(const BLOCK_WDATA block, GLFix x, GLFix y, GLFix z) override;

    virtual void drawPreview(const BLOCK_WDATA block, TEXTURE &dest, int x, int y) override;

    virtual const char* getName(const BLOCK_WDATA block) override;

    /** The slab height of a depth, in world units. Also used by the AABB. */
    static GLFix layerHeight(const BLOCK_WDATA block);

private:
    /**
     * Whether the face of this layer that points along `side` is hidden by the
     * neighbour there: an opaque block, or another snow layer at least as deep.
     * The second half of that matters because snow is not opaque, so two layers
     * side by side would otherwise draw their touching faces in the same plane
     * and z-fight along every drift.
     */
    static bool sideCovered(const BLOCK_WDATA block, const int local_x, const int local_y, const int local_z, const BLOCK_SIDE side, Chunk &c);
};

#endif // SNOWRENDERER_H
