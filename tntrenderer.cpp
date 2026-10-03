#include "world.h"
#include "tntrenderer.h"

#include "audio_manager.h"

void TNTRenderer::tick(const BLOCK_WDATA /*block*/, int local_x, int local_y, int local_z, Chunk &c)
{
    if(c.isBlockPowered(local_x, local_y, local_z))
        explode(local_x, local_y, local_z, c);
}

void TNTRenderer::explode(const int local_x, const int local_y, const int local_z, Chunk &c)
{
    const int gx = local_x + c.x * Chunk::SIZE;
    const int gy = local_y + c.y * Chunk::SIZE;
    const int gz = local_z + c.z * Chunk::SIZE;
    // The renderer has no player position to attenuate against, so the charge
    // is heard at full level: a blast is meant to carry across the map anyway.
    GameAudio::explosion();
    world.explosionTNT(gx, gy, gz);
}
