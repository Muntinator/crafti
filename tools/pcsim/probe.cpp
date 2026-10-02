/*
 * Debug probe for the headless desktop harness.
 *
 * The harness runs the real game, so it can simply read the real globals: there
 * is nothing to stub and nothing to keep in sync. A scenario line
 *
 *     probe where am I
 *
 * prints one line of state into the run log, which is how the tour is aimed at
 * interesting places without guessing coordinates.
 *
 * This file is only compiled into tools/pcsim (see tools/pcsim/Makefile); the
 * game's own targets do not see it.
 */

#include "worldtask.h"
#include "worldcommands.h"
#include "inventory.h"
#include "itemicons.h"
#include "worldclock.h"

#include <stdio.h>
#include <string>

/** One letter for the block a column's surface is made of. */
static char surfaceLetter(BLOCK_WDATA block)
{
    switch(getBLOCK(block))
    {
    case BLOCK_GRASS: return 'G';
    case BLOCK_DIRT: return 'd';
    case BLOCK_SAND: return 's';
    case BLOCK_STONE: return 'r';
    case BLOCK_WATER: return 'w';
    case BLOCK_WATER_FAST: return 'w';
    case BLOCK_WOOD: return 'T';
    case BLOCK_LEAVES: return 'l';
    case BLOCK_COBBLESTONE: return 'c';
    case BLOCK_BEDROCK: return 'B';
    case BLOCK_SNOW: return '*';
    case BLOCK_AIR: return ' ';
    default: return '?';
    }
}

/**
 * A little height map around the player, so a tour can be aimed at a hill or a
 * shoreline without a bird's eye view first. Each cell is one column: the digit
 * is the surface height modulo ten, the letter is the block on top of it.
 */
void pcsim_probe_scan(int radius)
{
    const int bx = (world_task.x / BLOCK_SIZE).floor();
    const int bz = (world_task.z / BLOCK_SIZE).floor();

    int lowest = 1000, highest = -1;

    printf("[scan] %d columns around (%d,%d), rows are z, top is north (-z)\n", radius, bx, bz);
    for(int z = bz - radius; z <= bz + radius; ++z)
    {
        std::string line;
        for(int x = bx - radius; x <= bx + radius; ++x)
        {
            int surface = -1;
            for(int y = World::HEIGHT * Chunk::SIZE - 1; y >= 0; --y)
            {
                if(getBLOCK(world.getBlock(x, y, z)) != BLOCK_AIR)
                {
                    surface = y;
                    break;
                }
            }
            if(surface < 0)
            {
                line += " .";
                continue;
            }
            if(surface < lowest)
                lowest = surface;
            if(surface > highest)
                highest = surface;

            const char letter = surfaceLetter(world.getBlock(x, surface, z));
            line += '0' + (surface % 10);
            line += (x == bx && z == bz) ? '@' : letter;
        }
        printf("[scan] %s%s\n", line.c_str(), z == bz ? "   <- player's row" : "");
    }
    printf("[scan] surface height %d..%d\n", lowest, highest);
    fflush(stdout);
}

/**
 * Runs a console command through the same entry point the console uses.
 *
 * Commands are written to be run from anywhere (worldcommands.h says so), and
 * going through here instead of typing at the console is what makes a tour
 * reliable: no keystroke can be dropped, and the reply lands in the run log.
 */
void pcsim_run_command(const char *line)
{
    char reply[192] = {};
    runCommand(line, reply, sizeof(reply));
    printf("[cmd] /%s -> %s\n", line, reply);
    fflush(stdout);
}

/**
 * Points the camera at an exact heading.
 *
 * `look` is a *rate* -- a per-frame mouse delta -- which is what a motion clip
 * wants but not what a still does: to frame the sun, or the moon on a given
 * night, the camera has to be at a known yaw and pitch, not at wherever a rate
 * happened to leave it. This writes the two angles the mouse would have written
 * (worldtask.cpp does `yr += rel_x/3`), so the renderer cannot tell the
 * difference. Angles are in degrees and wrapped like the game's own.
 */
void pcsim_aim(int yaw, int pitch)
{
    world_task.yr = GLFix(yaw).normaliseAngle();
    world_task.xr = GLFix(pitch).normaliseAngle();
    printf("[aim] yaw=%d pitch=%d\n", world_task.yr.toInteger<int>(), world_task.xr.toInteger<int>());
    fflush(stdout);
}

/**
 * Prints the state of the sky: the clock, both bodies and the light they give.
 *
 * The clock is a pure function of the tick count, so a run that stops at a
 * given time can be checked against these numbers rather than against a
 * remembered idea of what dusk looks like.
 */
void pcsim_sky(const char *tag)
{
    char clock[8] = {};
    WorldClock::formatClock(clock, sizeof(clock));
    const WorldClock::SkyColor sky = WorldClock::skyColor();

    printf("[sky] %-22s %s day %u  sun elev=%d az=%d%s  moon elev=%d az=%d%s phase=%d  "
           "light=%d shade=%.3f  bg=(%.3f,%.3f,%.3f)\n",
           tag ? tag : "", clock, WorldClock::dayCount(),
           WorldClock::sunElevationDegrees(), WorldClock::sunAzimuthDegrees(),
           WorldClock::sunAboveHorizon() ? "" : " (set)",
           WorldClock::moonElevationDegrees(), WorldClock::moonAzimuthDegrees(),
           WorldClock::moonAboveHorizon() ? "" : " (set)",
           WorldClock::moonPhase(),
           WorldClock::skyLightLevel(), WorldClock::skyLightFactor(),
           sky.r, sky.g, sky.b);
    fflush(stdout);
}

void pcsim_probe(const char *tag)
{
    const int bx = (world_task.x / BLOCK_SIZE).floor();
    const int by = (world_task.y / BLOCK_SIZE).floor();
    const int bz = (world_task.z / BLOCK_SIZE).floor();

    const BLOCK_WDATA under = world.getBlock(bx, by - 1, bz);
    const BLOCK_WDATA feet = world.getBlock(bx, by, bz);
    const BLOCK_WDATA head = world.getBlock(bx, by + 1, bz);

    // Raw ids as well as names: an unnamed block is exactly the case worth
    // seeing, and getItemName returns null for one.
    // The whole column the player is standing in, so a stuck or floating spawn
    // can be told from terrain that generated somewhere unexpected.
    int top_solid = -1, second_solid = -1;
    for(int y = World::HEIGHT * Chunk::SIZE - 1; y >= 0; --y)
    {
        if(getBLOCK(world.getBlock(bx, y, bz)) != BLOCK_AIR)
        {
            if(top_solid < 0)
                top_solid = y;
            else
            {
                second_solid = y;
                break;
            }
        }
    }

    // "stuck" is what the movement code tests: a player whose own box already
    // overlaps the world can neither walk nor fall, so this is the first thing
    // worth seeing when one does not move.
    AABB box = world_task.playerBox();
    const bool stuck = world.intersect(box);

    printf("[probe] %-24s column: top solid y=%d, next y=%d  clock: day %u time %u (%s, sun %d deg)  "
           "stuck=%d at=(%.2f,%.2f,%.2f)\n",
           tag ? tag : "", top_solid, second_solid,
           WorldClock::dayCount(), WorldClock::time(),
           WorldClock::isNight() ? "night" : "day", WorldClock::sunElevationDegrees(),
           stuck ? 1 : 0,
           world_task.x.toFloat() / BLOCK_SIZE,
           world_task.y.toFloat() / BLOCK_SIZE,
           world_task.z.toFloat() / BLOCK_SIZE);
    printf("[probe] %-24s pos=(%d,%d,%d) yaw=%d pitch=%d blocks(y-1..y+1)=%d/%d/%d "
           "names=%s,%s,%s held=%s(%d) n=%u mode=%s\n",
           tag ? tag : "",
           bx, by, bz,
           world_task.yr.toInteger<int>(), world_task.xr.toInteger<int>(),
           static_cast<int>(getBLOCK(under)), static_cast<int>(getBLOCK(feet)),
           static_cast<int>(getBLOCK(head)),
           getItemName(under) ? getItemName(under) : "?",
           getItemName(feet) ? getItemName(feet) : "?",
           getItemName(head) ? getItemName(head) : "?",
           getItemName(current_inventory.currentSlot()) ? getItemName(current_inventory.currentSlot()) : "(?)",
           static_cast<int>(getBLOCK(current_inventory.currentSlot())),
           current_inventory.currentSlotCount(),
           world_task.isCreative() ? "creative" : "survival");
    fflush(stdout);
}

void pcsim_hurt(int amount)
{
    if(amount < 0)
        amount = 0;

    // The same path a mob or a fall takes, so the death screen is reached the way
    // the game reaches it. Only survival can die; creative ignores the damage.
    world_task.hurtPlayer(static_cast<unsigned int>(amount));

    printf("[hurt] %d damage\n", amount);
    fflush(stdout);
}
