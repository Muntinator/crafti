// Host tests for the weather module (weather.cpp).
//
// The weather is a pure function of (world seed, game ticks), so the whole thing
// can be walked through on the host: game days of simulated weather, checked for
// the properties the game depends on -- that it changes, that it changes
// gradually, that rain is a minority of the time but not a rarity, that
// lightning only happens in a thunderstorm, and that the same world seed always
// gives the same weather.
//
// Build and run with `make -C tests`.

#include "weather.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

namespace
{
    constexpr unsigned long long DayTicks = 24000ull; // WorldClock::TicksPerDay

    struct Tally
    {
        unsigned long long clear = 0, rain = 0, thunder = 0;
        int transitions = 0;
        int longest_clear_run = 0;
        int current_clear_run = 0;
        int flash_ticks = 0;
        int thunder_ticks = 0;
        int rain_ticks = 0;
        int max_intensity_step = 0;
        int peak_rain_intensity = 0;
        int peak_thunder_intensity = 0;
    };

    /**
     * Walks the weather from the start of the world in `step`-tick strides.
     * A step of one tick is what makes "the intensity never jumps" meaningful,
     * so the tick-accurate walks cover a few days and the statistical ones
     * cover a hundred.
     */
    Tally walk(uint32_t seed, int step, unsigned long long total_ticks)
    {
        Tally tally;
        Weather::Spell spell, previous;
        bool have_previous = false;

        for(unsigned long long tick = 0; tick < total_ticks; tick += static_cast<unsigned long long>(step))
        {
            Weather::spellAt(seed, tick, spell);

            CHECK(spell.state >= Weather::Clear && spell.state < Weather::StateCount);
            // Nature rains and storms, but it never picks snow: whether water
            // freezes is a property of where the player is standing, and this
            // module does not know where that is (weather.h). Snow is the state
            // /weather snow forces, and precipitation() does the rest.
            CHECK(spell.state != Weather::Snow);
            CHECK(spell.intensity >= 0 && spell.intensity <= Weather::MaxIntensity);
            CHECK(Weather::rainStrength(spell) >= 0 && Weather::rainStrength(spell) <= Weather::MaxIntensity);
            CHECK(Weather::darkness(spell) >= 0 && Weather::darkness(spell) <= Weather::MaxIntensity);

            // The intensity is what the renderer scales by, and it must agree
            // with the state: clear weather can never be visible.
            if(spell.state == Weather::Clear)
            {
                CHECK(spell.intensity == 0);
                CHECK(Weather::rainStrength(spell) == 0);
                CHECK(Weather::darkness(spell) == 0);
                CHECK(!Weather::isPrecipitating(spell));
            }
            else
            {
                CHECK(Weather::isPrecipitating(spell));
                // Strength is a documented fraction of the state's peak, so pin
                // the mapping rather than merely bounding it. A spell in its
                // first or last tick is real but not visible yet: the state
                // changes when the shower starts arriving.
                const int rain_peak = spell.state == Weather::Rain ? Weather::RainStrength : Weather::MaxIntensity;
                const int dark_peak = spell.state == Weather::Rain ? Weather::RainDarkness : Weather::ThunderDarkness;
                CHECK(Weather::rainStrength(spell) == (rain_peak * spell.intensity) / Weather::MaxIntensity);
                CHECK(Weather::darkness(spell) == (dark_peak * spell.intensity) / Weather::MaxIntensity);
                // A storm that has fully arrived always has its flash rhythm.
                if(spell.state == Weather::Thunder && spell.intensity == Weather::MaxIntensity)
                    CHECK(spell.flash_period > 0);
            }

            if(spell.state == Weather::Clear)
            {
                ++tally.clear;
                ++tally.current_clear_run;
                if(tally.current_clear_run > tally.longest_clear_run)
                    tally.longest_clear_run = tally.current_clear_run;
            }
            else
            {
                tally.current_clear_run = 0;
                if(spell.state == Weather::Rain)
                {
                    ++tally.rain;
                    if(spell.intensity > tally.peak_rain_intensity)
                        tally.peak_rain_intensity = spell.intensity;
                }
                else
                {
                    ++tally.thunder;
                    ++tally.thunder_ticks;
                    if(spell.intensity > tally.peak_thunder_intensity)
                        tally.peak_thunder_intensity = spell.intensity;
                }
            }

            if(spell.state == Weather::Thunder)
            {
                if(Weather::lightningAt(spell, tick))
                    ++tally.flash_ticks;
            }
            else
            {
                // No lightning outside a thunderstorm, whatever the tick.
                CHECK(spell.flash_period == 0);
                CHECK(!Weather::lightningAt(spell, tick));
            }

            if(have_previous)
            {
                if(previous.state != spell.state)
                    ++tally.transitions;

                const int delta = spell.intensity - previous.intensity;
                const int magnitude = delta < 0 ? -delta : delta;
                if(magnitude > tally.max_intensity_step)
                    tally.max_intensity_step = magnitude;
            }

            previous = spell;
            have_previous = true;
        }

        return tally;
    }

    void testDeterminism()
    {
        const uint32_t seed = 0x0badf00d;
        Weather::Spell a, b, c;

        Weather::spellAt(seed, 1234567ull, a);
        // Other queries in between must not change the answer.
        Weather::spellAt(seed, 9999999ull, c);
        Weather::spellAt(seed, 1234567ull, b);

        CHECK(a.state == b.state);
        CHECK(a.intensity == b.intensity);
        CHECK(a.flash_period == b.flash_period);

        // A different seed has to give a different world's weather.
        int differences = 0;
        int samples = 0;
        for(unsigned long long tick = 0; tick < 400000ull; tick += 401)
        {
            Weather::Spell one, two;
            Weather::spellAt(0x11111111u, tick, one);
            Weather::spellAt(0x22222222u, tick, two);
            if(one.state != two.state)
                ++differences;
            ++samples;
        }
        printf("    spells differing between two seeds: %d of %d\n", differences, samples);
        CHECK(differences > samples / 10);
    }

    void testPrevalence()
    {
        // Weather has to be a minority of the time and it has to actually happen.
        const Tally tally = walk(0xc0ffee00u, 31, 100ull * DayTicks);
        const unsigned long long sampled = tally.clear + tally.rain + tally.thunder;

        const int rain_percent = static_cast<int>(tally.rain * 100 / sampled);
        const int thunder_percent = static_cast<int>(tally.thunder * 100 / sampled);
        const int clear_percent = static_cast<int>(tally.clear * 100 / sampled);

        printf("    100 game days: clear %d%%, rain %d%%, thunder %d%%, %d transitions\n",
               clear_percent, rain_percent, thunder_percent, tally.transitions);

        CHECK(clear_percent > 40);   // most of the time it is dry
        CHECK(clear_percent < 90);   // but not always
        CHECK(rain_percent > 3);
        CHECK(thunder_percent > 1);
        CHECK(rain_percent + thunder_percent < 45);
        CHECK(tally.transitions > 20);
    }

    void testCloudCoverComesInSpells()
    {
        // The point of the storm seasons: dry spells are long in wall-clock
        // terms, not a coin flip every two minutes.
        constexpr int Step = 31;
        const Tally tally = walk(0x5ee0b11u, Step, 100ull * DayTicks);
        const double minutes = static_cast<double>(tally.longest_clear_run) * Step
            * 20.0 / static_cast<double>(DayTicks);
        printf("    longest clear spell: %d ticks (%.1f minutes of game time)\n",
               tally.longest_clear_run * Step, minutes);
        // Dry spells have to last (that is what the seasons are for) without
        // swallowing whole game days of a hundred-day sample.
        CHECK(tally.longest_clear_run >= 3);
        CHECK(minutes <= 90.0);
    }

    void testRampsGradually()
    {
        // Tick by tick over three days: rain has to arrive, not appear.
        const Tally tally = walk(0xfeedfaceu, 1, 3ull * DayTicks);
        printf("    largest single-tick intensity change: %d\n", tally.max_intensity_step);
        CHECK(tally.max_intensity_step <= 1);

        // The first tick of a spell has no strength at all.
        int arrivals = 0;
        Weather::Spell spell, previous;
        for(unsigned long long tick = 0; tick < 3ull * DayTicks; ++tick)
        {
            Weather::spellAt(0xfeedfaceu, tick, spell);
            if(tick > 0 && previous.state == Weather::Clear && spell.state != Weather::Clear)
            {
                CHECK(spell.intensity == 0);
                ++arrivals;
            }
            previous = spell;
        }
        CHECK(arrivals > 0);

        // And a spell that is not cut short still reaches full strength.
        CHECK(tally.peak_rain_intensity == Weather::MaxIntensity);
        CHECK(tally.peak_thunder_intensity == Weather::MaxIntensity);
    }

    void testLightning()
    {
        const Tally tally = walk(0x7e11a11u, 1, 3ull * DayTicks);
        printf("    thunder ticks lit: %d of %d (%.2f%%)\n",
               tally.flash_ticks, tally.thunder_ticks,
               tally.thunder_ticks ? 100.0 * tally.flash_ticks / tally.thunder_ticks : 0.0);

        CHECK(tally.thunder_ticks > 0);
        CHECK(tally.flash_ticks > 0);
        // The number of lit ticks has to sit inside the band the flash period
        // allows: a flash is FlashTicks long and the period is between the two
        // documented bounds, so a storm is never a strobe and never silent.
        CHECK(tally.flash_ticks * Weather::MinFlashPeriodTicks <= tally.thunder_ticks * Weather::FlashTicks * 2);
        CHECK(tally.flash_ticks * Weather::MaxFlashPeriodTicks >= tally.thunder_ticks * Weather::FlashTicks);
    }

    void testPrecipitationType()
    {
        Weather::Spell spell;

        CHECK(Weather::precipitation(spell, false) == Weather::NoPrecipitation);
        CHECK(Weather::precipitation(spell, true) == Weather::NoPrecipitation);

        // A plain shower is rain where it is warm and snow where it is cold -- and
        // the spell is the same spell either way, which is what lets a player walk
        // from a plains into a cold forest and watch the drops turn to flakes.
        spell.state = Weather::Rain;
        spell.intensity = Weather::MaxIntensity;
        CHECK(Weather::precipitation(spell, false) == Weather::RainPrecipitation);
        CHECK(Weather::precipitation(spell, true) == Weather::SnowPrecipitation);

        spell.state = Weather::Thunder;
        CHECK(Weather::precipitation(spell, false) == Weather::RainPrecipitation);
        CHECK(Weather::precipitation(spell, true) == Weather::SnowPrecipitation);

        // A forced snow spell stays snow anywhere, so /weather snow works over a
        // desert; that is the only way to see the flakes without a cold biome.
        spell.state = Weather::Snow;
        spell.flash_period = 0;
        CHECK(Weather::precipitation(spell, false) == Weather::SnowPrecipitation);
        CHECK(Weather::precipitation(spell, true) == Weather::SnowPrecipitation);

        // Snow's strength and darkening are their own, and both lighter than rain:
        // a storm should not look like a blizzard.
        CHECK(Weather::rainStrength(spell) == Weather::SnowStrength);
        CHECK(Weather::darkness(spell) == Weather::SnowDarkness);
        CHECK(Weather::SnowStrength < Weather::RainStrength);
        CHECK(Weather::SnowDarkness < Weather::RainDarkness);
        CHECK(Weather::SnowDarkness < Weather::ThunderDarkness);

        // Snow never brings lightning, whatever tick it is asked about.
        CHECK(!Weather::lightningAt(spell, 0));
        CHECK(!Weather::lightningAt(spell, 123456));
        CHECK(Weather::strikeIndex(spell, 123456) == 0);

        // Every state has a name of its own: /weather prints them.
        const char *seen[Weather::StateCount];
        for(int state = 0; state < Weather::StateCount; ++state)
        {
            seen[state] = Weather::stateName(state);
            CHECK(strcmp(seen[state], "Unknown") != 0);
            for(int other = 0; other < state; ++other)
                CHECK(strcmp(seen[state], seen[other]) != 0);
        }
        CHECK(strcmp(Weather::stateName(Weather::Snow), "Snow") == 0);
        CHECK(strcmp(Weather::stateName(-1), "Unknown") == 0);
        CHECK(strcmp(Weather::stateName(Weather::StateCount + 5), "Unknown") == 0);
    }

    void testStrikes()
    {
        // A fully arrived thunderstorm with a period of its own, so the strike
        // schedule can be walked tick by tick.
        Weather::Spell storm;
        storm.state = Weather::Thunder;
        storm.intensity = Weather::MaxIntensity;
        storm.flash_period = 600;

        // The flash is lit for FlashTicks and no longer, and every lit tick belongs
        // to the strike whose index it names: the frame that draws the bolt and the
        // frame that resolves it therefore agree on which bolt it is.
        int flashes = 0;
        for(unsigned long long tick = 0; tick < 10000ull; ++tick)
        {
            const bool lit = Weather::lightningAt(storm, tick);
            CHECK(lit == (tick % 600ull < static_cast<unsigned long long>(Weather::FlashTicks)));
            if(lit)
                ++flashes;
            CHECK(Weather::strikeIndex(storm, tick) == tick / 600ull);
        }
        // One flash per period, FlashTicks long, plus the part of the last one the
        // walk reached.
        const int full_flashes = (10000 / 600) * Weather::FlashTicks;
        const int trailing = (10000 % 600) < Weather::FlashTicks ? (10000 % 600) : Weather::FlashTicks;
        CHECK(flashes == full_flashes + trailing);

        // The index only ever moves forward, so a storm cannot send a bolt back to
        // where an earlier one landed.
        unsigned long long previous_index = 0;
        for(unsigned long long tick = 0; tick < 10000ull; tick += 7)
        {
            const unsigned long long index = Weather::strikeIndex(storm, tick);
            CHECK(index >= previous_index);
            previous_index = index;
        }

        // A storm with no lightning has no strikes at all.
        Weather::Spell calm;
        CHECK(Weather::strikeIndex(calm, 99999) == 0);

        // Where a bolt lands: deterministic, inside the advertised radius, spread
        // over every block of it, and not parked on the player's own column.
        const uint32_t seed = 0x5708c9u;
        const int span = Weather::StrikeRadius * 2 + 1;
        int dx_hits[64] = {0}, dz_hits[64] = {0};
        int positives = 0, negatives = 0, same = 0, on_player = 0, distinct = 0;
        const int samples = 6000;

        for(int i = 0; i < samples; ++i)
        {
            int dx = 0, dz = 0;
            Weather::strikeOffset(seed, static_cast<unsigned long long>(i), dx, dz);

            int again_dx = 0, again_dz = 0;
            Weather::strikeOffset(seed, static_cast<unsigned long long>(i), again_dx, again_dz);
            CHECK(dx == again_dx && dz == again_dz);

            CHECK(dx >= -Weather::StrikeRadius && dx <= Weather::StrikeRadius);
            CHECK(dz >= -Weather::StrikeRadius && dz <= Weather::StrikeRadius);
            CHECK(dx + Weather::StrikeRadius < 64 && dz + Weather::StrikeRadius < 64);
            ++dx_hits[dx + Weather::StrikeRadius];
            ++dz_hits[dz + Weather::StrikeRadius];

            if(dx > 0 || dz > 0)
                ++positives;
            if(dx < 0 || dz < 0)
                ++negatives;
            if(dx == dz)
                ++same;
            if(dx == 0 && dz == 0)
                ++on_player;

            int next_dx = 0, next_dz = 0;
            Weather::strikeOffset(seed, static_cast<unsigned long long>(i) + 1ull, next_dx, next_dz);
            if(next_dx != dx || next_dz != dz)
                ++distinct;
        }

        for(int i = 0; i < span; ++i)
        {
            CHECK(dx_hits[i] > 0);
            CHECK(dz_hits[i] > 0);
        }

        printf("    strikes: %d targets, %d positive, %d negative, %d diagonal, %d straight on the player\n",
               samples, positives, negatives, same, on_player);

        CHECK(positives > samples / 4);
        CHECK(negatives > samples / 4);
        // The two offsets come from different halves of one hash, so they are not
        // correlated: landing on the diagonal happens about once per span.
        CHECK(same < samples / 8);
        // A bolt straight onto the player's own column is rare (about one in
        // span^2), which is what keeps being struck feeling like bad luck rather
        // than like a schedule.
        CHECK(on_player < samples / 16);
        // Consecutive strikes go to different places: a storm does not hammer one
        // spot for its whole length.
        CHECK(distinct > samples * 9 / 10);

        // A different seed moves the storm's bolts.
        int a_dx = 0, a_dz = 0, b_dx = 0, b_dz = 0;
        Weather::strikeOffset(0x11111111u, 3, a_dx, a_dz);
        Weather::strikeOffset(0x22222222u, 3, b_dx, b_dz);
        CHECK(a_dx != b_dx || a_dz != b_dz);
    }

    void testCost()
    {
        // The weather is asked once a frame, and the CX runs about three frames a
        // second, so the per-call cost of the pure module is the whole of the
        // weather's frame budget. Timed rather than asserted (host timings vary),
        // so a regression in the module shows up as a number that changed.
        const int rounds = 200000;
        Weather::Spell spell;
        volatile int sink = 0;

        clock_t start = clock();
        for(int i = 0; i < rounds; ++i)
            Weather::spellAt(0x1234567u, static_cast<unsigned long long>(i) * 37ull, spell);
        const double spell_us = clock() - start;

        start = clock();
        for(int i = 0; i < rounds; ++i)
            sink += Weather::precipitation(spell, i & 1) + Weather::rainStrength(spell) + Weather::darkness(spell);
        const double parts_us = clock() - start;

        start = clock();
        for(int i = 0; i < rounds; ++i)
        {
            int dx = 0, dz = 0;
            Weather::strikeOffset(0x1234567u, static_cast<unsigned long long>(i), dx, dz);
            sink += Weather::strikeIndex(spell, static_cast<unsigned long long>(i) * 97ull) + dx + dz;
        }
        const double strike_us = clock() - start;

        const double per_call = 1000000.0 / (static_cast<double>(CLOCKS_PER_SEC) * rounds);
        printf("    cost: spellAt %.3f us, precipitation+strength %.3f us, strike %.3f us per call\n",
               spell_us * per_call, parts_us * per_call, strike_us * per_call);
        CHECK(sink != 0);
    }

    void testSaveCompatibility()
    {
        // The weather is derived from the clock, so a reloaded world must get the
        // same weather. This mirrors exactly what the game does after loading: it
        // only has the seed, the day count and the time of day.
        const uint32_t seed = 0x5a7e0f11u;
        int checked = 0;
        for(int day = 0; day < 50; ++day)
            for(unsigned int time = 0; time < 24000; time += 977)
            {
                const unsigned long long ticks = static_cast<unsigned long long>(day) * DayTicks + time;
                Weather::Spell a, b;
                Weather::spellAt(seed, ticks, a);
                Weather::spellAt(seed, ticks, b);
                CHECK(a.state == b.state);
                CHECK(a.intensity == b.intensity);
                ++checked;
            }
        CHECK(checked > 1000);

        // The very first moment of a world is never mid-storm.
        Weather::Spell opening;
        Weather::spellAt(seed, 0, opening);
        CHECK(opening.intensity == 0);
        CHECK(opening.flash_period == 0);
    }
}

int main()
{
    printf("weather_test\n");

    testDeterminism();
    testPrevalence();
    testCloudCoverComesInSpells();
    testRampsGradually();
    testLightning();
    testPrecipitationType();
    testStrikes();
    testCost();
    testSaveCompatibility();

    printf("weather_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
