// Beds for WorldTask: lying down, skipping the night, and coming back to the bed
// after dying.
//
// The rules live in bed.h, which is pure and host-tested (tests/bed_test.cc): how
// the two halves encode their facing, when sleeping is allowed, how long a night
// is, and which cells count as the bed a player is respawning at. This file is
// the thin part that has to exist on the calculator, because it is the clock, the
// weather and the survival state that decide the answer.
//
// There is no bed *entity*: the two block halves hold everything there is to
// know, and the one thing that outlives a frame -- where the player wakes up --
// is a position, saved with the world. That keeps the whole feature inside the
// block and save architecture the chests already use.
//
// Sleeping skips the night by moving the clock, not by running it faster. The
// sun, the stars, the weather, the mob spawns and the survival ticks are all
// functions of the clock, so paying one `WorldClock::advance` is what makes them
// all arrive at morning together.

#include "worldtask.h"

#include "audio_manager.h"
#include "audio_sounds.h"
#include "bed.h"
#include "world.h"
#include "worldclock.h"

namespace
{
    /**
     * Darkens a 565 pixel towards black by `alpha`/256. The weather's own blend
     * (worldweather.cpp) goes towards a colour, which is not what a fade needs, so
     * this one is a plain scaling of the three channels.
     */
    unsigned short darken565(unsigned short pixel, unsigned int alpha)
    {
        const unsigned int keep = 256u - alpha;
        const unsigned int r = (((pixel >> 11) & 0x1F) * keep) >> 8;
        const unsigned int g = (((pixel >> 5) & 0x3F) * keep) >> 8;
        const unsigned int b = ((pixel & 0x1F) * keep) >> 8;
        return static_cast<unsigned short>((r << 11) | (g << 5) | b);
    }
}

bool WorldTask::trySleepInBed(int x, int y, int z, uint8_t data, bool complete)
{
    // A thunderstorm is as good a reason to lie down as the night is, the way it
    // is in Minecraft, which is why the weather module is asked instead of a flag
    // this file would have to keep in step with it.
    const bool storming = weather.state == Weather::State::Thunder;

    const Bed::SleepResult result = Bed::canSleep(WorldClock::isNight(), storming, complete);
    if(result != Bed::CanSleep)
    {
        setMessage(Bed::sleepMessage(result));
        return true;
    }

    // Remembered before the night is skipped: the *foot* cell is what is stored,
    // so breaking either half later can be recognised as breaking this bed.
    int foot_x, foot_y, foot_z;
    Bed::footOf(x, y, z, data, foot_x, foot_y, foot_z);
    bed_spawn.valid = true;
    bed_spawn.x = foot_x;
    bed_spawn.y = foot_y;
    bed_spawn.z = foot_z;

    // Sleeping always lands on the next dawn, so the day counter moves on by one
    // and the night that was slept through is a night that happened.
    WorldClock::advance(Bed::ticksToMorning(WorldClock::time()));

    sleeping = true;
    sleep_ms = Bed::FadeTotalMs;

    // Nothing is said here: the fade is the answer to the click, and "Good
    // morning" comes at the end of it, when there is a screen to read it on.
    return true;
}

void WorldTask::forgetBedSpawn(int x, int y, int z, uint8_t data)
{
    if(Bed::isSpawnBed(bed_spawn, x, y, z, data))
        bed_spawn = Bed::SpawnPoint();
}

void WorldTask::restoreBedSpawn(bool valid, int x, int y, int z)
{
    bed_spawn.valid = valid;
    bed_spawn.x = x;
    bed_spawn.y = y;
    bed_spawn.z = z;
}

void WorldTask::updateSleep(GLFix dt)
{
    if(sleep_ms == 0)
        return;

    // Real milliseconds, like the clock: a fade counted in frames would take some
    // seven seconds on the CX's three steps a second and a third of a second on a
    // desktop.
    const unsigned int elapsed = static_cast<unsigned int>(dt * GLFix(static_cast<int>(simulation_tick_ms)));
    sleep_ms = elapsed >= sleep_ms ? 0 : sleep_ms - elapsed;

    if(sleep_ms > 0)
        return;

    sleeping = false;
    setMessage(Bed::sleepMessage(Bed::CanSleep));
    GameAudio::playSound(GameAudio::Sound::RandomLevelup);
}

void WorldTask::renderSleepFade()
{
    if(!sleeping || screen == nullptr || screen->bitmap == nullptr)
        return;

    // Clear at both ends of the fade and darkest in the middle of it, so the night
    // goes by while the screen is at its blackest and the morning is already there
    // as it lifts again (Bed::fadeAlpha).
    const unsigned int alpha = Bed::fadeAlpha(Bed::FadeTotalMs, sleep_ms, Bed::FadeMaxAlpha);

    unsigned short *pixels = screen->bitmap;
    const int count = SCREEN_WIDTH * SCREEN_HEIGHT;
    for(int i = 0; i < count; ++i)
        pixels[i] = darken565(pixels[i], alpha);
}
