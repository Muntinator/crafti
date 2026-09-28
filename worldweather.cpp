// Weather for WorldTask: rain, thunderstorms and lightning.
//
// The rules live in weather.h, which is pure and host-tested; this file is the
// thin part that must exist on the calculator. It asks the module what the
// weather is, announces a change through the normal message channel, and paints
// the rain.
//
// The rain is drawn as screen-space streaks blended straight into the
// framebuffer, in front of the 3D scene and behind the HUD. That is the mirror
// image of what worldsky.cpp does with the sun and the stars (behind the world),
// and it needs no geometry, no depth test and no matrix work: a streak is four
// to nine pixels, so even a downpour is a few hundred pixel writes a frame.
// Blending rather than overwriting is what keeps the rain visible both against
// the bright sky and over dark terrain.
//
// A thunder flash is one full-screen blend towards white. It is deliberately an
// overlay instead of a brighter global shade: nglSetGlobalShade() multiplies the
// per-vertex shade and is already at its neutral ceiling in daylight, so pushing
// it past 256 would overflow into the wrong colour channel.

#include "worldtask.h"

#include "settingstask.h"
#include "weather.h"
#include "worldclock.h"

namespace
{
    /** Streaks in the heaviest rain. More than this is wasted on a 320x240 screen. */
    constexpr int MaxStreaks = 96;
    /** Pixels a streak is long in the heaviest rain, and in the lightest. */
    constexpr int MinStreakLength = 3;
    constexpr int MaxStreakLength = 9;
    /** How opaque a streak is, out of 256. It has to read over both sky and rock. */
    constexpr int StreakOpacity = 128;
    /** How much of the screen a lightning flash tints, out of 256. */
    constexpr int FlashOpacity = 112;
    /** Fall speed of the rain at no strength and at full strength, in px/second. */
    constexpr int MinFallSpeed = 90;
    constexpr int MaxFallSpeed = 230;
    /** Where rain_offset wraps, so it cannot grow without bound. */
    constexpr int RainOffsetWrap = 1 << 20;

    /** Deterministic per-streak hash: the pattern is stable from frame to frame. */
    uint32_t streakHash(uint32_t index)
    {
        uint32_t h = index * 0x9E3779B1u + 0x85EBCA6Bu;
        h ^= h >> 15;
        h *= 0x27D4EB2Fu;
        h ^= h >> 13;
        return h;
    }

    /** Blends a 565 pixel towards `color` by weight/256. */
    unsigned short blend565(unsigned short base, unsigned short color, int weight)
    {
        const int base_r = (base >> 11) & 0x1F;
        const int base_g = (base >> 5) & 0x3F;
        const int base_b = base & 0x1F;
        const int color_r = (color >> 11) & 0x1F;
        const int color_g = (color >> 5) & 0x3F;
        const int color_b = color & 0x1F;

        const int r = base_r + (((color_r - base_r) * weight) >> 8);
        const int g = base_g + (((color_g - base_g) * weight) >> 8);
        const int b = base_b + (((color_b - base_b) * weight) >> 8);
        return static_cast<unsigned short>((r << 11) | (g << 5) | b);
    }
}

void WorldTask::updateWeather(GLFix dt)
{
    const int previous_state = weather.state;

    // A graph is a plot of a function, not a place, and with the day/night cycle
    // off the clock the weather hangs off does not advance either, so both keep
    // the dry frame the game has always had. Deriving it from the world seed and
    // the clock's ticks, instead of storing anything, is what makes a reloaded
    // world resume the weather it was having.
    const bool weather_enabled = world.worldType() != World::WorldType::Graph
        && settings_task.getValue(SettingsTask::DAY_NIGHT) != 0
        && settings_task.getValue(SettingsTask::WEATHER) != 0;
    if(!weather_enabled)
    {
        weather.state = Weather::Clear;
        weather.intensity = 0;
        weather.flash_period = 0;
        weather_darkness = 0;
        weather_rain = 0;
        weather_lightning = false;
        return;
    }

    const unsigned long long total_ticks =
        static_cast<unsigned long long>(WorldClock::dayCount()) * WorldClock::TicksPerDay + WorldClock::time();
    Weather::spellAt(world.seedValue(), total_ticks, weather);

    weather_darkness = Weather::darkness(weather);
    weather_rain = Weather::rainStrength(weather);
    weather_lightning = Weather::lightningAt(weather, total_ticks);

    if(weather.state != previous_state)
    {
        switch(weather.state)
        {
        case Weather::Rain: setMessage("Rain"); break;
        case Weather::Thunder: setMessage("Thunderstorm"); break;
        default: setMessage("The rain stops"); break;
        }
    }

    if(weather_rain <= 0)
        return;

    // Rain falls at a speed in pixels per second, converted through the same
    // `dt * tick_ms` the clock uses, so a 3 Hz CX frame and a 30 Hz desktop frame
    // see the drops move at the same rate.
    const int elapsed_ms = static_cast<int>(dt * GLFix(static_cast<int>(simulation_tick_ms)));
    if(elapsed_ms <= 0)
        return;

    const int speed = MinFallSpeed + (weather_rain * (MaxFallSpeed - MinFallSpeed)) / Weather::MaxIntensity;
    rain_offset += (elapsed_ms * speed) / 1000;
    if(rain_offset >= RainOffsetWrap)
        rain_offset -= RainOffsetWrap;

    // One column scan from the head upwards: rain indoors is the classic tell
    // that a weather system is faked.
    weather_outdoors = true;
    const int block_x = (x / BLOCK_SIZE).floor();
    const int block_z = (z / BLOCK_SIZE).floor();
    const int head_y = ((y + eye_pos) / BLOCK_SIZE).floor();
    for(int world_y = head_y + 1; world_y < World::HEIGHT * Chunk::SIZE; ++world_y)
    {
        const BLOCK type = getBLOCK(world.getBlock(block_x, world_y, block_z));
        if(type != BLOCK_AIR && type != BLOCK_WATER && type != BLOCK_WATER_FAST)
        {
            weather_outdoors = false;
            break;
        }
    }
}

void WorldTask::renderWeather()
{
    if(weather_rain <= 0 || !weather_outdoors || screen == nullptr || screen->bitmap == nullptr)
        return;

    unsigned short *pixels = screen->bitmap;

    // A thunder flash: the whole frame tinted towards white for its moment. One
    // blend pass, and only on the ticks weather.cpp says a flash is lit.
    if(weather_lightning)
    {
        const int count = SCREEN_WIDTH * SCREEN_HEIGHT;
        for(int i = 0; i < count; ++i)
            pixels[i] = blend565(pixels[i], 0xFFFF, FlashOpacity);
    }

    const int streak_count = (weather_rain * MaxStreaks) / Weather::MaxIntensity;
    const int streak_length = MinStreakLength
        + (weather_rain * (MaxStreakLength - MinStreakLength)) / Weather::MaxIntensity;
    const int span = SCREEN_HEIGHT + streak_length * 2;
    const unsigned short rain_color = colorRGB(GLFix(0.72f), GLFix(0.82f), GLFix(1.0f));

    for(int i = 0; i < streak_count; ++i)
    {
        const uint32_t hash = streakHash(static_cast<uint32_t>(i) + 1u);
        const int streak_x = static_cast<int>(hash % static_cast<uint32_t>(SCREEN_WIDTH));
        // Two speeds, so the drops do not fall in a rigid sheet.
        const int streak_speed = 1 + static_cast<int>((hash >> 9) & 1u);

        int streak_y = static_cast<int>((hash >> 11) % static_cast<uint32_t>(span)) - streak_length;
        streak_y += rain_offset * streak_speed;
        streak_y %= span;
        if(streak_y < 0)
            streak_y += span;
        streak_y -= streak_length;

        for(int k = 0; k < streak_length; ++k)
        {
            // A slight lean, so a streak reads as falling rain rather than as a
            // cursor being dragged down the screen.
            const int px = streak_x - k / 3;
            const int py = streak_y + k;
            if(px < 0 || py < 0 || px >= SCREEN_WIDTH || py >= SCREEN_HEIGHT)
                continue;
            unsigned short &pixel = pixels[px + py * SCREEN_WIDTH];
            pixel = blend565(pixel, rain_color, StreakOpacity);
        }
    }
}
