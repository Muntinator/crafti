/**
 * Deterministic weather. See weather.h for the contract.
 *
 * There is no state here on purpose: everything is re-derived from the world
 * seed and the number of ticks the world clock has counted. A world therefore
 * cannot end up with weather that does not match its clock, reloading a save
 * cannot change the weather, and the host test can walk thousands of game days
 * through the same code the calculator runs.
 */

#include "weather.h"

namespace
{
    constexpr uint32_t SaltWeather = 0x57454154u; // "WEAT"
    constexpr uint32_t SaltSeason = 0x53544F52u;  // "STOR"
    constexpr uint32_t SaltFlash = 0x464C4153u;   // "FLAS"
    constexpr uint32_t SaltStrike = 0x5354524Bu;  // "STRK"

    /** Percent of seasons that contain any weather at all. */
    constexpr uint32_t StormySeasonPercent = 55;
    /** Within a stormy season: chance of a dry window, then rain, then thunder. */
    constexpr uint32_t DryWindowRoll = 45;
    constexpr uint32_t RainWindowRoll = 85;

    uint32_t hashWindow(uint32_t seed, unsigned long long window, uint32_t salt)
    {
        uint32_t h = seed ^ salt
            ^ static_cast<uint32_t>(window)
            ^ static_cast<uint32_t>(window >> 32);
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        h *= 0x846ca68bu;
        h ^= h >> 16;
        return h;
    }

    int stateOfWindow(uint32_t seed, unsigned long long window)
    {
        // Seasons are what make dry spells long: a dry season cannot produce a
        // shower no matter how the per-window roll falls.
        const unsigned long long season = window / Weather::SeasonWindows;
        // Each season is either dry for its whole length or a storm cell that
        // rains on and off: that is what makes dry spells last, instead of the
        // weather flipping a coin every two minutes.
        if(hashWindow(seed, season, SaltSeason) % 100u >= StormySeasonPercent)
            return Weather::Clear;

        const uint32_t roll = hashWindow(seed, window, SaltWeather) % 100u;
        if(roll < DryWindowRoll)
            return Weather::Clear;
        if(roll < RainWindowRoll)
            return Weather::Rain;
        return Weather::Thunder;
    }
}

const char *Weather::stateName(int state)
{
    switch(state)
    {
    case Clear: return "Clear";
    case Rain: return "Rain";
    case Thunder: return "Thunder";
    case Snow: return "Snow";
    default: return "Unknown";
    }
}

void Weather::spellAt(uint32_t world_seed, unsigned long long total_ticks, Spell &out)
{
    const unsigned long long window = total_ticks / WindowTicks;
    const int offset = static_cast<int>(total_ticks % WindowTicks);

    out.state = stateOfWindow(world_seed, window);
    out.intensity = 0;
    out.flash_period = 0;

    if(out.state == Clear)
        return;

    // Ramp in unless the previous window was already this weather, and ramp out
    // unless the next one will be: a multi-window spell then has one arrival and
    // one departure instead of a dip at every boundary.
    // Before the first window the world counts as clear, so a brand new world
    // starts with the weather arriving instead of already in full swing.
    const int previous_state = window == 0 ? Clear : stateOfWindow(world_seed, window - 1);

    int intensity = MaxIntensity;
    if(offset < FadeTicks && previous_state != out.state)
        intensity = (offset * MaxIntensity) / FadeTicks;
    else if(offset >= WindowTicks - FadeTicks && stateOfWindow(world_seed, window + 1) != out.state)
        intensity = ((WindowTicks - offset) * MaxIntensity) / FadeTicks;

    if(intensity <= 0)
        return; // nothing to see in the first or last tick of a spell

    out.intensity = intensity;

    if(out.state == Thunder)
    {
        // The rhythm belongs to the season, not to the window: a storm spanning
        // several windows then keeps flashing instead of pausing every two
        // minutes while the next window builds up again.
        const uint32_t h = hashWindow(world_seed, window / FlashSeasonWindows, SaltFlash);
        out.flash_period = MinFlashPeriodTicks
            + static_cast<int>(h % static_cast<uint32_t>(MaxFlashPeriodTicks - MinFlashPeriodTicks + 1));
    }
}

Weather::Precipitation Weather::precipitation(const Spell &spell, bool freezing)
{
    if(spell.state == Clear)
        return NoPrecipitation;
    // A forced snow spell is snow even over a desert: it is how the flakes are
    // seen at all without walking to a cold forest first.
    if(spell.state == Snow)
        return SnowPrecipitation;
    return freezing ? SnowPrecipitation : RainPrecipitation;
}

int Weather::rainStrength(const Spell &spell)
{
    if(spell.state == Clear)
        return 0;
    // The peak belongs to the state: a thunderstorm is the heaviest, a snow
    // shower the lightest, because a flake is a dot and a drop is a streak.
    const int peak = spell.state == Thunder ? MaxIntensity
        : (spell.state == Snow ? SnowStrength : RainStrength);
    return (peak * spell.intensity) / MaxIntensity;
}

int Weather::darkness(const Spell &spell)
{
    if(spell.state == Clear)
        return 0;
    const int peak = spell.state == Thunder ? ThunderDarkness
        : (spell.state == Snow ? SnowDarkness : RainDarkness);
    return (peak * spell.intensity) / MaxIntensity;
}

bool Weather::lightningAt(const Spell &spell, unsigned long long total_ticks)
{
    if(spell.flash_period <= 0)
        return false;
    // Flashes repeat from the start of the window, so the rhythm is stable for
    // as long as the window lasts and the caller needs no state for it.
    return (total_ticks % static_cast<unsigned long long>(spell.flash_period))
        < static_cast<unsigned long long>(FlashTicks);
}

unsigned long long Weather::strikeIndex(const Spell &spell, unsigned long long total_ticks)
{
    if(spell.flash_period <= 0)
        return 0;
    return total_ticks / static_cast<unsigned long long>(spell.flash_period);
}

void Weather::strikeOffset(uint32_t world_seed, unsigned long long strike_index, int &dx, int &dz)
{
    // Both offsets come from the same 32-bit hash, split in half: three bits of
    // the low half would follow the divisor, and a strike that always landed on
    // the same diagonal would be worse than one that misses.
    const uint32_t h = hashWindow(world_seed, strike_index, SaltStrike);
    const int span = StrikeRadius * 2 + 1;
    dx = static_cast<int>(h % static_cast<uint32_t>(span)) - StrikeRadius;
    dz = static_cast<int>((h >> 12) % static_cast<uint32_t>(span)) - StrikeRadius;
}
