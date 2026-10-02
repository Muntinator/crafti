#ifndef BEDRENDERER_H
#define BEDRENDERER_H

#include "blockrenderer.h"

/**
 * Draws the bed: two low slabs, a foot and a head, that meet along their short
 * edge to make one piece of furniture.
 *
 * The bed is the first *horizontal* multi-block structure in the game. The door
 * is two blocks stacked vertically and the cake is a single cell, and this takes
 * a little from each: the shape and the lighting are the cake's (a 9/16 mattress
 * drawn with unaligned vertices and per-face shading), and the two-halves-move-
 * together rule is the door's.
 *
 * The one thing neither of them has to deal with is the seam. Two adjacent slabs
 * that both draw the face between them put two quads in the same plane, which
 * z-fights and flickers; so a half skips the face along its partner when -- and
 * only when -- the block on the other side really is the other half of the same
 * bed. A bed whose partner has been replaced therefore still draws closed, and a
 * proper bed has no coincident geometry at all.
 *
 * The tiles come from columns 4..7 of atlas row 4, cut out of the official bed
 * model's texture by tools/textures/gen_block_textures.py, and none of them has
 * to be turned to follow which way the bed points.
 */
class BedRenderer : public DumbBlockRenderer
{
public:
    virtual void renderSpecialBlock(const BLOCK_WDATA block, GLFix x, GLFix y, GLFix z, Chunk &c) override;
    /** The bed's own faces come from renderSpecialBlock, so this has nothing to add. */
    virtual void geometryNormalBlock(const BLOCK_WDATA, const int, const int, const int, const BLOCK_SIDE, Chunk &) override {}
    virtual bool isOpaque(const BLOCK_WDATA /*block*/) override { return false; }
    virtual bool isObstacle(const BLOCK_WDATA /*block*/) override { return true; }
    virtual bool isOriented(const BLOCK_WDATA /*block*/) override { return true; }
    virtual bool isFullyOriented(const BLOCK_WDATA /*block*/) override { return false; }

    virtual bool isBlockShaped(const BLOCK_WDATA /*block*/) override { return false; }
    virtual AABB getAABB(const BLOCK_WDATA /*block*/, GLFix x, GLFix y, GLFix z) override;

    virtual void drawPreview(const BLOCK_WDATA block, TEXTURE &dest, int x, int y) override;

    /**
     * Breaking one half takes the other with it, and forgets the bed as a respawn
     * point: half a bed is not a bed, and a player who breaks the bed they slept
     * in should not wake up next to where it used to be.
     */
    virtual void removedBlock(const BLOCK_WDATA block, int local_x, int local_y, int local_z, Chunk &c) override;

    /**
     * Right-clicking a bed tries to sleep in it. This is the hook the world task
     * already calls on interactive blocks (the door toggles in the same place).
     */
    virtual bool action(const BLOCK_WDATA block, const int local_x, const int local_y, const int local_z, Chunk &c) override;

    virtual const char* getName(const BLOCK_WDATA /*block*/) override;

    /** The height of the mattress: 9/16 of a block, as in Minecraft. */
    static constexpr GLFix bed_height = BLOCK_SIZE / 16 * 9;

protected:
    /**
     * True when the bed half at this local position really is this bed's partner:
     * the same facing and the other end of the bed. Used by the renderer for the
     * seam and by action() to tell a whole bed from half of one.
     */
    static bool isBedPartner(const BLOCK_WDATA block, const int local_x, const int local_y, const int local_z, Chunk &c);
};

#endif // BEDRENDERER_H
