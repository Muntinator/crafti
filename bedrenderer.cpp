#include "bedrenderer.h"

#include "bed.h"
#include "worldtask.h"

// The bed's four tiles are painted into row 5 of the atlas at load time by
// terrain.cpp (paintBedTile). They are the only tiles in the game that belong to
// a *shape* rather than to a block -- the two ends of the bed are told apart by
// which top tile they use -- so they are named here instead of being read out of
// the block_textures table.
static constexpr int bed_tile_row = 5;
static constexpr int bed_head_top_tile = 0;  // the blanket with the pillow pad
static constexpr int bed_underside_tile = 1; // plain planks
static constexpr int bed_side_tile = 2;      // the blanket turned over the frame
static constexpr int bed_foot_top_tile = 3;  // the blanket on its own

constexpr GLFix BedRenderer::bed_height;

/** One of the bed's tiles at full block size. */
static const TextureAtlasEntry &bedTile(const int column)
{
    return terrain_atlas[column][bed_tile_row].current;
}

/** The data byte of a block as a bed half, with the power flag left out. */
static uint8_t bedData(const BLOCK_WDATA block)
{
    return static_cast<uint8_t>(getBLOCKDATA(block));
}

bool BedRenderer::isBedPartner(const BLOCK_WDATA block, const int local_x, const int local_y, const int local_z, Chunk &c)
{
    const uint8_t data = bedData(block);
    int dx, dy, dz;
    Bed::partnerOffset(data, dx, dy, dz);

    const BLOCK_WDATA partner = c.getGlobalBlockRelative(local_x + dx, local_y + dy, local_z + dz);
    if(getBLOCK(partner) != BLOCK_BED)
        return false;

    // Whether the two data bytes are the two ends of one bed is bed.h's rule, not
    // this renderer's: the world task asks the same question to decide whether a
    // right click is a whole bed or half of one.
    return Bed::isPartner(data, bedData(partner));
}

void BedRenderer::renderSpecialBlock(const BLOCK_WDATA block, GLFix x, GLFix y, GLFix z, Chunk &c)
{
    const uint8_t data = bedData(block);
    const bool head = Bed::isHead(data);
    BLOCK_SIDE facing = Bed::facing(data);

    // A bed written straight into the world (by /setblock, or by an older save)
    // can carry a facing that is not horizontal, which means nothing for a bed.
    // It is drawn pointing along +z rather than not at all, so the block can at
    // least be seen and broken.
    if(!Bed::isHorizontal(facing))
        facing = BLOCK_BACK;

    const int local_x = x / BLOCK_SIZE, local_y = y / BLOCK_SIZE, local_z = z / BLOCK_SIZE;

    // The face that meets the other half. Both halves draw it, so the two quads
    // would sit in the same plane and z-fight -- but it is only dropped when the
    // block next door really is the other half of this bed. A bed whose partner
    // was replaced therefore still closes up, which is what makes half a bed look
    // like a broken bed instead of a hole.
    const bool seamless = isBedPartner(block, local_x, local_y, local_z, c);

    const TextureAtlasEntry &top = bedTile(head ? bed_head_top_tile : bed_foot_top_tile);
    const TextureAtlasEntry &underside = bedTile(bed_underside_tile);
    const TextureAtlasEntry &side = bedTile(bed_side_tile);

    const GLFix slab_top = y + bed_height;
    const GLFix max_x = x + BLOCK_SIZE, max_z = z + BLOCK_SIZE;

    // Every face is shaded by which way it points (the same table normal blocks
    // use) and drawn from both sides, the way the pressure plate's top face is:
    // the bed is a thin slab the camera can get under, and a face that culled the
    // wrong way would simply vanish.
    if(!seamless || facing != BLOCK_FRONT)
    {
        const COLOR color = BlockRenderer::computeLighting(BLOCK_FRONT, TEXTURE_DRAW_BACKFACE);
        c.addUnalignedVertex({x, y, z, side.left, side.bottom, color});
        c.addUnalignedVertex({x, slab_top, z, side.left, side.top, color});
        c.addUnalignedVertex({max_x, slab_top, z, side.right, side.top, color});
        c.addUnalignedVertex({max_x, y, z, side.right, side.bottom, color});
    }

    if(!seamless || facing != BLOCK_BACK)
    {
        const COLOR color = BlockRenderer::computeLighting(BLOCK_BACK, TEXTURE_DRAW_BACKFACE);
        c.addUnalignedVertex({max_x, y, max_z, side.left, side.bottom, color});
        c.addUnalignedVertex({max_x, slab_top, max_z, side.left, side.top, color});
        c.addUnalignedVertex({x, slab_top, max_z, side.right, side.top, color});
        c.addUnalignedVertex({x, y, max_z, side.right, side.bottom, color});
    }

    if(!seamless || facing != BLOCK_RIGHT)
    {
        const COLOR color = BlockRenderer::computeLighting(BLOCK_RIGHT, TEXTURE_DRAW_BACKFACE);
        c.addUnalignedVertex({max_x, y, z, side.right, side.bottom, color});
        c.addUnalignedVertex({max_x, slab_top, z, side.right, side.top, color});
        c.addUnalignedVertex({max_x, slab_top, max_z, side.left, side.top, color});
        c.addUnalignedVertex({max_x, y, max_z, side.left, side.bottom, color});
    }

    if(!seamless || facing != BLOCK_LEFT)
    {
        const COLOR color = BlockRenderer::computeLighting(BLOCK_LEFT, TEXTURE_DRAW_BACKFACE);
        c.addUnalignedVertex({x, y, max_z, side.left, side.bottom, color});
        c.addUnalignedVertex({x, slab_top, max_z, side.left, side.top, color});
        c.addUnalignedVertex({x, slab_top, z, side.right, side.top, color});
        c.addUnalignedVertex({x, y, z, side.right, side.bottom, color});
    }

    {
        const COLOR color = BlockRenderer::computeLighting(BLOCK_TOP, TEXTURE_DRAW_BACKFACE);
        c.addUnalignedVertex({x, slab_top, z, top.left, top.bottom, color});
        c.addUnalignedVertex({x, slab_top, max_z, top.left, top.top, color});
        c.addUnalignedVertex({max_x, slab_top, max_z, top.right, top.top, color});
        c.addUnalignedVertex({max_x, slab_top, z, top.right, top.bottom, color});
    }

    {
        const COLOR color = BlockRenderer::computeLighting(BLOCK_BOTTOM, TEXTURE_DRAW_BACKFACE);
        c.addUnalignedVertex({max_x, y, z, underside.left, underside.bottom, color});
        c.addUnalignedVertex({max_x, y, max_z, underside.left, underside.top, color});
        c.addUnalignedVertex({x, y, max_z, underside.right, underside.top, color});
        c.addUnalignedVertex({x, y, z, underside.right, underside.bottom, color});
    }
}

AABB BedRenderer::getAABB(const BLOCK_WDATA /*block*/, GLFix x, GLFix y, GLFix z)
{
    return {x, y, z, x + BLOCK_SIZE, y + bed_height, z + BLOCK_SIZE};
}

void BedRenderer::drawPreview(const BLOCK_WDATA /*block*/, TEXTURE &dest, int x, int y)
{
    // The icon is the head end seen from above: the pillow on its blanket, which
    // is what tells a bed apart from a red wool block at icon size.
    BlockRenderer::drawTextureAtlasEntry(*terrain_resized, terrain_atlas[bed_head_top_tile][bed_tile_row].resized, dest, x, y);
}

bool BedRenderer::action(const BLOCK_WDATA block, const int local_x, const int local_y, const int local_z, Chunk &c)
{
    const uint8_t data = bedData(block);
    const bool complete = isBedPartner(block, local_x, local_y, local_z, c);

    // Either half can be used to lie down, so the world task is handed the cell
    // that was clicked along with whether its partner is there.
    return world_task.trySleepInBed(local_x + c.x * Chunk::SIZE,
                                    local_y + c.y * Chunk::SIZE,
                                    local_z + c.z * Chunk::SIZE,
                                    data, complete);
}

void BedRenderer::removedBlock(const BLOCK_WDATA block, int local_x, int local_y, int local_z, Chunk &c)
{
    const uint8_t data = bedData(block);

    // Half a bed is not a bed. Breaking either half takes the other with it, or
    // the leftover half would stay as an invisible obstacle that cannot be
    // pointed at without breaking it. No drop comes of this: the half that was
    // struck drops the one bed, which is what the world task already hands out.
    int dx, dy, dz;
    Bed::partnerOffset(data, dx, dy, dz);

    if(getBLOCK(c.getGlobalBlockRelative(local_x + dx, local_y + dy, local_z + dz)) == BLOCK_BED)
        c.setGlobalBlockRelative(local_x + dx, local_y + dy, local_z + dz, BLOCK_AIR);

    // ...and the bed stops being where the player wakes up, whichever half was
    // struck. The world position is the local one plus the chunk's own origin.
    world_task.forgetBedSpawn(local_x + c.x * Chunk::SIZE,
                              local_y + c.y * Chunk::SIZE,
                              local_z + c.z * Chunk::SIZE,
                              data);
}

const char *BedRenderer::getName(const BLOCK_WDATA /*block*/)
{
    return "Bed";
}
