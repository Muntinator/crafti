#ifndef WEATHER_H
#define WEATHER_H

#include <stdint.h>

/**
 * Deterministic weather: clear, rain and thunderstorms.
 *
 * Like worldclock.h, this is a pure function of the world and holds no state of
 * its own. The weather at a moment is derived from the world seed and the total
 * game ticks the world clock has counted, which buys three things:
 *
 *  - **nothing new to save.** The clock already stores its time and day count
 *    (save format v9), so a reloaded world gets exactly the weather it was
 *    having, with no extra fields and no migration,
 *  - **deterministic and order independent**, so it is unit tested on a host
 *    (tests/weather_test.cc) and agrees between the CX and the desktop build
 *    even though their frame rates differ,
 *  - **free to query.** Asking for the weather is a couple of hashes, so the
 *    renderer can ask again every frame instead of caching it.
 *
 * Time is divided into windows of `WindowTicks`. A window's weather is decided
 * by a hash of the seed and the window number, and the intensity ramps up and
 * down over `FadeTicks` so a change is a shower arriving rather than a switch.
 * The ramp is skipped on the side where the neighbouring window has the same
 * weather, so a rain spell spanning several windows reads as one long rain.
 *
 * Storms come in seasons: `SeasonWindows` consecutive windows share a slow
 * "storminess" roll, and most seasons are entirely dry. That is what makes dry
 * spells last tens of minutes instead of two.
 */
namespace Weather
{
    enum State
    {
        Clear = 0,
        Rain,
        Thunder,
        StateCount
    };

    const char *stateName(int state);

    /** Ticks one weather window lasts (2 minutes of game time at 24000 ticks/day). */
    constexpr int WindowTicks = 2400;
    /** Windows that share one storminess roll: a "season" of 12 minutes. */
    constexpr int SeasonWindows = 6;
    /** Ticks the intensity takes to ramp up from or down to nothing. */
    constexpr int FadeTicks = 300;
    /** Intensity at its peak, and the scale of the intensity field. */
    constexpr int MaxIntensity = 256;
    /**
     * How many ticks a lightning flash stays visible. The CX logic loop runs at
     * about three frames a second and one tick is 50 ms, so a flash shorter than
     * this could pass entirely between two frames and never be seen.
     */
    constexpr int FlashTicks = 5;
    /**
     * Bounds on how often a thunderstorm flashes. The period belongs to the
     * season rather than to the window, so the rhythm carries across a storm that
     * spans several windows instead of restarting at every boundary.
     */
    constexpr int MinFlashPeriodTicks = 400;
    constexpr int MaxFlashPeriodTicks = 1400;
    /** Windows that share one flash rhythm. Matches SeasonWindows in purpose. */
    constexpr int FlashSeasonWindows = 6;
    /** Peak sky darkening for rain and for a thunderstorm, out of MaxIntensity. */
    constexpr int RainDarkness = 96;
    constexpr int ThunderDarkness = 150;
    /** Peak rain strength for plain rain; a thunderstorm reaches MaxIntensity. */
    constexpr int RainStrength = 200;

    /** The weather at one moment. */
    struct Spell
    {
        int state = Clear;
        /** 0..MaxIntensity: how far into the spell the moment is. */
        int intensity = 0;
        /** Ticks between lightning flashes, or 0 when there is no lightning. */
        int flash_period = 0;
    };

    /** Resolves the weather at `total_ticks` (dayCount * TicksPerDay + time). */
    void spellAt(uint32_t world_seed, unsigned long long total_ticks, Spell &out);

    /** Rain strength for the renderer, 0..MaxIntensity (0 when clear). */
    int rainStrength(const Spell &spell);

    /** How much the sky and the terrain tint should darken, 0..MaxIntensity. */
    int darkness(const Spell &spell);

    /** True on the ticks a thunder flash is lit. */
    bool lightningAt(const Spell &spell, unsigned long long total_ticks);

    /** True when this spell puts water in the air. */
    inline bool isPrecipitating(const Spell &spell) { return spell.state != Clear; }
}

#endif // WEATHER_H
