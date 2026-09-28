// The weather touching the world: the snow that settles around the player and the
// bolt that has just landed.
//
// The rules are in weather.h and snowcover.h, which are pure and host-tested; this
// file is the thin part that has to know where the ground is. Both features run off
// the world clock rather than a counter of their own, so a reloaded save carries on
// with the snowfall it was having instead of replaying it.
//
// The cost is what makes this affordable on a CX. A snow step happens once a
// second at most, touches three columns, and reads at most (MaxGroundDrop +
// the world's height) blocks per column -- and it writes nothing unless a depth
// actually changed, so an idle snowfall is a handful of chunk lookups. A strike is
// rarer still: one every twenty to seventy seconds of a thunderstorm, and it is
// one column scan, one burst of particles and one sound.
//
// What is deliberately *not* here: a lit furnace is not put out, a strike does not
// set the ground on fire, and animals are not hit. There is no fire block in this
// game to spread or to extinguish, which is discussed in WEATHER.md.

#include "worldtask.h"

#include "audio_manager.h"
#include "snowcover.h"
#include "survival.h"
#include "weather.h"

namespace
{
    /** How far above the player's head a bolt is allowed to start its fall. */
    constexpr int StrikeSearchUp = 6;
    /** How far below the player's feet it will look for something to hit. */
    constexpr int StrikeSearchDown = 24;
    /** How far, in blocks, the player can be from the bolt and still be its target. */
    constexpr int StrikeHitBlocks = 1;
}

void WorldTask::updateSnowCover(unsigned long long total_ticks, bool snowing, bool freezing)
{
    // One step per LayerTicks, named by the clock. If several steps are missed --
    // a save being loaded, a frame that took a long time -- only the step we are
    // in is applied: the snow cannot appear in one jump, and the cost of a step
    // never multiplies.
    const unsigned long long step_index = total_ticks / SnowCover::LayerTicks;
    if(step_index == snow_step_index)
        return;
    snow_step_index = step_index;

    const int player_x = (x / BLOCK_SIZE).floor();
    const int player_z = (z / BLOCK_SIZE).floor();
    const int feet_y = (y / BLOCK_SIZE).floor();
    const int world_top = World::HEIGHT * Chunk::SIZE;

    for(int i = 0; i < SnowCover::ColumnsPerStep; ++i)
    {
        int dx, dz;
        SnowCover::columnOffset(world.seedValue(), step_index, i, dx, dz);
        const int column_x = player_x + dx;
        const int column_z = player_z + dz;

        // Where the snow goes and whether it may go there at all is snowcover.h's
        // rule, handed this column's blocks through a lambda: the same code the
        // host test walks, rather than a second copy of it here. Reading a column
        // of an unloaded chunk reports stone (World), which is a surface, so a step
        // near the edge of the loaded world simply finds no open sky above it and
        // does nothing.
        int cover_y = -1;
        const int depth = SnowCover::columnStep(
            feet_y, world_top,
            [this, column_x, column_z](const int world_y) -> SnowCover::Block
            {
                return world.getBlock(column_x, world_y, column_z);
            },
            snowing, freezing, cover_y);

        if(depth < 0)
            continue;

        world.changeBlock(column_x, cover_y, column_z,
                          depth == 0 ? BLOCK_AIR : getBLOCKWDATA(BLOCK_SNOW, static_cast<uint8_t>(depth)));
    }
}

void WorldTask::resolveLightningStrike(unsigned long long total_ticks)
{
    const int dx_offset = (x / BLOCK_SIZE).floor();
    const int dz_offset = (z / BLOCK_SIZE).floor();

    // Which strike this is, and therefore where it lands: both come from the
    // weather and the seed alone, so the frame that draws the flash and the frame
    // that resolves it cannot disagree about where the bolt went.
    int dx = 0, dz = 0;
    Weather::strikeOffset(world.seedValue(), Weather::strikeIndex(weather, total_ticks), dx, dz);

    const int strike_x = dx_offset + dx;
    const int strike_z = dz_offset + dz;

    // The bolt falls onto the first thing it meets, starting above the player's
    // head, so a strike over a tree lands in its leaves rather than under them.
    const int head_y = ((y + eye_pos) / BLOCK_SIZE).floor();
    int hit_y = -1;
    for(int wy = head_y + StrikeSearchUp; wy >= head_y - StrikeSearchDown && wy >= 0; --wy)
    {
        if(SnowCover::isPermeable(getBLOCK(world.getBlock(strike_x, wy, strike_z))))
            continue;
        hit_y = wy;
        break;
    }

    if(hit_y >= 0)
        world.spawnDestructionParticles(strike_x, hit_y, strike_z);

    const int abs_dx = dx < 0 ? -dx : dx;
    const int abs_dz = dz < 0 ? -dz : dz;

    // The thunder follows the flash, quieter the further away the bolt is: the
    // same distance scale the audio engine uses for footsteps and mobs.
    GameAudio::weatherThunder(abs_dx > abs_dz ? abs_dx : abs_dz);

    // The player is struck when the bolt lands on the block they are standing on,
    // give or take a step -- the same tolerance vanilla uses for "the bolt found
    // you". A strike is also the one source of damage that sets its victim alight,
    // and the rain that comes with the storm usually puts that out again, which is
    // what happens in vanilla too.
    if(abs_dx <= StrikeHitBlocks && abs_dz <= StrikeHitBlocks)
    {
        const int feet_y = (y / BLOCK_SIZE).floor();
        if(hit_y >= feet_y - StrikeHitBlocks && hit_y <= feet_y + StrikeHitBlocks)
        {
            fire_ticks = Survival::FireTicksFromFireBlock;
            applyDamage(Survival::LightningDamage, Survival::Damage::Lightning, "Struck by lightning!");
        }
    }
}
