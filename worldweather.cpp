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

#include "audio_manager.h"
#include "biomegen.h"
#include "settingstask.h"
#include "snowcover.h"
#include "weather.h"
#include "worldclock.h"

namespace
{
    /** Streaks in the heaviest rain. More than this is wasted on a 320x240 screen. */
    constexpr int MaxStreaks = 96;
    /**
     * Flakes in the heaviest snow. Fewer than the streaks, and each flake is a
     * single pixel rather than a line, so a snowstorm costs less to draw than an
     * ordinary shower.
     */
    constexpr int MaxFlakes = 56;
    /** How opaque a flake is, out of 256. Snow reads brighter than rain does. */
    constexpr int FlakeOpacity = 176;
    /**
     * Fall speed of snow, in px/second: flakes drift, they do not fall like drops.
     * The slowest of these is still above 62 px/s on purpose -- the offset is kept
     * in whole pixels, so a speed low enough to move less than one pixel per frame
     * would leave the flakes hanging still on the 60 Hz desktop build while they
     * moved on the calculator.
     */
    constexpr int MinFlakeSpeed = 64;
    constexpr int MaxFlakeSpeed = 116;
    /**
     * How far a flake swings sideways over its fall, in pixels. Snow that fell in
     * a straight column would read as static; the swing is what makes it drift.
     */
    constexpr int FlakeSway = 5;
    /** Pixels of fall per pixel of sideways swing, so the drift follows the fall. */
    constexpr int FlakeSwayPixels = 9;
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
    const int previous_precipitation = weather_precipitation;

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
        weather_lightning_previous = false;
        weather_precipitation = Weather::NoPrecipitation;
        // The rain bed has to stop with the module: switching the weather off in
        // Settings would otherwise leave a shower playing over a clear sky.
        if(weather_audio != 0)
        {
            GameAudio::setWeather(0, false);
            weather_audio = 0;
        }
        // A forced spell cannot survive the module being switched off, so it is
        // dropped rather than left to come back the next time it is switched on.
        clearWeatherOverride();
        return;
    }

    const unsigned long long total_ticks =
        static_cast<unsigned long long>(WorldClock::dayCount()) * WorldClock::TicksPerDay + WorldClock::time();

    // A spell forced by /weather is counted down in game ticks, the same unit its
    // duration was converted into, so it ends after the same real time on the
    // calculator and on the desktop however long a day is set to.
    if(weather_override_ticks > 0)
    {
        const unsigned int day_seconds = WorldClock::dayLengthSeconds();
        const int elapsed_ms = static_cast<int>(dt * GLFix(static_cast<int>(simulation_tick_ms)));
        const unsigned long long elapsed_ticks = day_seconds == 0 ? 0ULL
            : static_cast<unsigned long long>(elapsed_ms) * WorldClock::TicksPerDay
                / (static_cast<unsigned long long>(day_seconds) * 1000ULL);

        weather_override_ticks = elapsed_ticks >= weather_override_ticks
            ? 0
            : weather_override_ticks - static_cast<unsigned int>(elapsed_ticks);
    }

    if(weather_override_state >= 0 && weather_override_ticks > 0)
    {
        // A forced spell is always at full strength, so what was asked for is what
        // is shown. The lightning rhythm still uses the module's own period bounds
        // instead of a second set of numbers here, so a forced storm flashes at the
        // same sort of rate as a natural one.
        weather.state = weather_override_state;
        weather.intensity = Weather::MaxIntensity;
        weather.flash_period = weather.state == Weather::Thunder
            ? (Weather::MinFlashPeriodTicks + Weather::MaxFlashPeriodTicks) / 2
            : 0;
    }
    else
    {
        weather_override_state = -1;
        Weather::spellAt(world.seedValue(), total_ticks, weather);
    }

    weather_darkness = Weather::darkness(weather);
    weather_rain = Weather::rainStrength(weather);
    weather_lightning = Weather::lightningAt(weather, total_ticks);

    // Whether the shower falls as rain or as snow is a property of where the
    // player is standing, not of the storm, so it is asked once a frame from the
    // biome temperature field: walk into a cold forest during a shower and the
    // drops become flakes without the weather changing state (weather.h keeps
    // "snow" as a spell of its own for /weather snow, which is snow anywhere).
    weather_freezing = BiomeGen::isColdAt(world.seedValue(), (x / BLOCK_SIZE).floor(), (z / BLOCK_SIZE).floor());
    weather_precipitation = Weather::precipitation(weather, weather_freezing);

    if(weather_precipitation != previous_precipitation || weather.state != previous_state)
    {
        switch(weather_precipitation)
        {
        case Weather::RainPrecipitation:
            setMessage(weather.state == Weather::Thunder ? "Thunderstorm" : "Rain");
            break;
        case Weather::SnowPrecipitation: setMessage("Snow"); break;
        default:
            setMessage(previous_precipitation == Weather::SnowPrecipitation ? "The snow stops" : "The rain stops");
            break;
        }
    }

    // The looping weather sound is started only when it changes: starting a stream
    // rewinds it, so asking for the same bed every frame would play the first
    // moment of the loop forever. Thunderstorms get their own, heavier bed, and
    // snow shares the rain's, which is what vanilla does too (this pack has no
    // separate snowfall loop; the flakes are heard as snow underfoot instead).
    const unsigned int wanted_audio = weather_precipitation == Weather::NoPrecipitation ? 0u
        : static_cast<unsigned int>(weather.state == Weather::Thunder
            ? GameAudio::Sound::AmbientWeatherThunder1
            : GameAudio::Sound::AmbientWeatherRain1);
    if(wanted_audio != weather_audio)
    {
        GameAudio::setWeather(wanted_audio, wanted_audio != 0);
        weather_audio = wanted_audio;
    }

    // A flash is a strike, and it is resolved on the frame it starts so the bolt
    // lands once per flash rather than once per tick it is lit.
    const bool strike_now = weather_lightning && !weather_lightning_previous;
    weather_lightning_previous = weather_lightning;
    if(strike_now)
        resolveLightningStrike(total_ticks);

    // Snow is laid down and melted whether or not anything is falling right now:
    // a warm spell is exactly when a drift has to go away.
    updateSnowCover(total_ticks,
                    weather_precipitation == Weather::SnowPrecipitation && weather.intensity > 0,
                    weather_freezing);

    if(weather_rain <= 0)
        return;

    // Rain falls at a speed in pixels per second, converted through the same
    // `dt * tick_ms` the clock uses, so a 3 Hz CX frame and a 30 Hz desktop frame
    // see the drops move at the same rate.
    const int elapsed_ms = static_cast<int>(dt * GLFix(static_cast<int>(simulation_tick_ms)));
    if(elapsed_ms <= 0)
        return;

    // Flakes drift down at their own pace, so the shared falling offset is
    // advanced with the speed of whatever is actually falling: the drops and the
    // flakes then move at the rate their shapes suggest.
    const bool snowing = weather_precipitation == Weather::SnowPrecipitation;
    const int min_speed = snowing ? MinFlakeSpeed : MinFallSpeed;
    const int max_speed = snowing ? MaxFlakeSpeed : MaxFallSpeed;
    const int speed = min_speed + (weather_rain * (max_speed - min_speed)) / Weather::MaxIntensity;
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

    // Snow: single pixels drifting sideways instead of streaks. It is the same
    // machinery as the rain -- the same offset, the same screen space blend -- so
    // a shower costs the same and shows the weather that is actually falling.
    if(weather_precipitation == Weather::SnowPrecipitation)
    {
        const int flake_count = (weather_rain * MaxFlakes) / Weather::MaxIntensity;
        const int span = SCREEN_HEIGHT + 4;
        const unsigned short flake_color = colorRGB(GLFix(0.94f), GLFix(0.96f), GLFix(1.0f));

        for(int i = 0; i < flake_count; ++i)
        {
            const uint32_t hash = streakHash(static_cast<uint32_t>(i) + 1u);
            const int base_x = static_cast<int>(hash % static_cast<uint32_t>(SCREEN_WIDTH));
            // Two speeds, so the flakes do not fall as one sheet.
            const int flake_speed = 1 + static_cast<int>((hash >> 9) & 1u);

            int flake_y = static_cast<int>((hash >> 11) % static_cast<uint32_t>(span)) - 2;
            flake_y += rain_offset * flake_speed;
            flake_y %= span;
            if(flake_y < 0)
                flake_y += span;

            // The drift: the sideways offset runs off the same falling offset, so
            // a flake wanders as it descends and stands still when the snow stops.
            const int sway = static_cast<int>((rain_offset / FlakeSwayPixels + (hash >> 19))
                                              % static_cast<uint32_t>(FlakeSway * 2 + 1)) - FlakeSway;
            const int px = base_x + sway;
            const int py = flake_y;
            if(px < 0 || py < 0 || px >= SCREEN_WIDTH || py >= SCREEN_HEIGHT)
                continue;
            unsigned short &pixel = pixels[px + py * SCREEN_WIDTH];
            pixel = blend565(pixel, flake_color, FlakeOpacity);
        }

        return;
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
