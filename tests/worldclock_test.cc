// Host tests for the day/night clock.
//
// The clock is a pure tick counter, so the whole cycle (sun/moon geometry, sky
// light, sky colour, the celestial screen projection and the wall clock) can be
// pinned down on a development host with no engine or hardware.
//
// Build and run with `make -C tests`.

#include "worldclock.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

#define CHECK_NEAR(value, expected, tolerance) do { ++checks; \
    const double _v = (double)(value); const double _e = (double)(expected); \
    if(fabs(_v - _e) > (tolerance)) { \
        printf("FAIL %s:%d: %s = %.4f, expected %.4f +/- %.4f\n", __FILE__, __LINE__, #value, _v, _e, (double)(tolerance)); \
        ++failures; } } while(0)

using namespace WorldClock;

static void test_clock_basics()
{
    reset();
    CHECK(time() == TimeSunrise);
    CHECK(dayCount() == 0);

    setTime(1000);
    CHECK(time() == 1000);
    advance(500);
    CHECK(time() == 1500);
    CHECK(dayCount() == 0);

    // Wrapping past the end of the day counts a day.
    setTime(TicksPerDay - 10);
    advance(20);
    CHECK(time() == 10);
    CHECK(dayCount() == 1);
    advance(TicksPerDay);
    CHECK(time() == 10);
    CHECK(dayCount() == 2);

    // Out of range inputs are normalised rather than rejected.
    setTime(TicksPerDay + 123);
    CHECK(time() == 123);
    setTime(0);
    reset(TicksPerDay * 3 + 7);
    CHECK(time() == 7);
}

static void test_ticks_until()
{
    CHECK(ticksUntil(TimeSunrise, TimeNoon) == TimeNoon);
    CHECK(ticksUntil(TimeNoon, TimeSunrise) == TicksPerDay - TimeNoon);
    CHECK(ticksUntil(500, 500) == 0);
    // Wrapping through midnight.
    CHECK(ticksUntil(TicksPerDay - 100, 100) == 200);
    // Sleeping at dusk should land on the next sunrise, a short wait.
    CHECK(ticksUntil(TimeSunsetEnd, TimeSunrise) == TicksPerDay - TimeSunsetEnd);
}

static void test_sun_and_moon_geometry()
{
    // Sunrise: on the horizon at time 0, noon overhead, sunset on the horizon,
    // midnight straight down.
    setTime(TimeSunrise);
    CHECK_NEAR(sunElevationDegrees(), 0, 1);
    CHECK(!sunAboveHorizon()); // exactly on the horizon is not "above"

    setTime(TimeNoon);
    CHECK_NEAR(sunElevationDegrees(), 90, 1);
    CHECK(sunAboveHorizon());
    CHECK(!isNight());

    setTime(TimeSunset);
    CHECK_NEAR(sunElevationDegrees(), 0, 1);

    setTime(TimeMidnight);
    CHECK_NEAR(sunElevationDegrees(), -90, 1);
    CHECK(!sunAboveHorizon());
    CHECK(isNight());

    // The moon is always opposite the sun.
    for(unsigned int t = 0; t < TicksPerDay; t += 500)
    {
        setTime(t);
        CHECK(moonElevationDegrees() == -sunElevationDegrees());
        CHECK(moonAzimuthDegrees() == (sunAzimuthDegrees() + 180) % 360);
        CHECK(moonAboveHorizon() == (sunElevationDegrees() < 0));
    }

    // Azimuth sweeps a full turn over a day and stays in range.
    setTime(TimeSunrise);
    const int start_azimuth = sunAzimuthDegrees();
    setTime(TimeSunset);
    const int middle_azimuth = sunAzimuthDegrees();
    CHECK(start_azimuth != middle_azimuth);
    for(unsigned int t = 0; t < TicksPerDay; t += 137)
    {
        setTime(t);
        const int azimuth = sunAzimuthDegrees();
        CHECK(azimuth >= 0 && azimuth < 360);
        const int elevation = sunElevationDegrees();
        CHECK(elevation >= -90 && elevation <= 90);
    }
}

static void test_night_and_twilight()
{
    setTime(TimeSunrise);
    CHECK(!isNight());
    setTime(TimeNoon);
    CHECK(!isNight());
    // Just after sunset it is still dusk, not night.
    setTime(TimeSunset);
    CHECK(!isNight());
    setTime(TimeDusk);
    CHECK(isNight());
    setTime(TimeMidnight);
    CHECK(isNight());
    // And dawn comes back.
    setTime(TimeSunsetEnd);
    CHECK(isNight());
    setTime(23000);
    CHECK(isNight()); // still deep night: dawn is at time 0
    setTime(TimeSunrise);
    CHECK(!isNight());

    setTime(TimeSunrise);
    CHECK(isTwilight());
    setTime(TimeNoon);
    CHECK(!isTwilight());
    setTime(TimeSunset);
    CHECK(isTwilight());
    setTime(TimeMidnight);
    CHECK(!isTwilight());
}

static void test_sky_light()
{
    setTime(TimeNoon);
    const int noon = skyLightLevel();
    CHECK(noon == MaxSkyLight);

    setTime(TimeMidnight);
    const int midnight = skyLightLevel();
    CHECK(midnight == MinSkyLight);

    // It gets darker as the sun goes down, without jumping.
    setTime(TimeSunrise);
    const int sunrise = skyLightLevel();
    setTime(TimeNoon);
    CHECK(skyLightLevel() >= sunrise);
    int previous = skyLightLevel();
    for(unsigned int t = TimeNoon; t <= TimeMidnight; t += 250)
    {
        setTime(t);
        const int light = skyLightLevel();
        CHECK(light >= MinSkyLight && light <= MaxSkyLight);
        CHECK(light <= previous); // monotonically darkening after noon
        previous = light;
    }

    // The middle of the day is flat, not a peak: a sun 64 degrees up at 09:00
    // and again at 15:00 both give full daylight, which is what stops the whole
    // afternoon from reading as a permanent dusk.
    setTime(3000); // 09:00
    CHECK(skyLightLevel() == MaxSkyLight);
    setTime(9000); // 15:00
    CHECK(skyLightLevel() == MaxSkyLight);

    // It brightens monotonically from dawn to noon as well.
    setTime(TimeSunrise);
    int morning = skyLightLevel();
    for(unsigned int t = TimeSunrise; t <= TimeNoon; t += 250)
    {
        setTime(t);
        CHECK(skyLightLevel() >= morning);
        morning = skyLightLevel();
    }

    // The golden hour is a smoothstep between the two plates, so it arrives at
    // full daylight with zero slope: the tick before the boundary is already
    // within a rounding of it, where a straight ramp would still be several
    // levels short and the plateau edge would show as a step.
    unsigned int first_full = 0;
    for(unsigned int t = TimeSunrise; t <= TimeNoon; t += 5)
    {
        setTime(t);
        if(skyLightLevel() == MaxSkyLight)
        {
            first_full = t;
            break;
        }
    }
    CHECK(first_full > TimeSunrise); // dawn is not full daylight
    CHECK(first_full < TimeNoon);
    setTime(first_full - 1);
    CHECK(skyLightLevel() >= MaxSkyLight - 2);

    // Night vision raises the floor but never lowers the level.
    setTime(TimeMidnight);
    CHECK(skyLightLevelAtLeast(150) == 150);
    setTime(TimeNoon);
    CHECK(skyLightLevelAtLeast(150) == skyLightLevel());
    CHECK(skyLightLevelAtLeast(0) == skyLightLevel());
}

// The renderer multiplies every textured pixel by this factor, so it must stay
// inside (0, 1] and be 1.0 exactly in full daylight -- anything below 1 forces
// the per-pixel multiply, and anything above 1 would brighten the terrain.
static void test_sky_light_factor()
{
    setTime(TimeNoon);
    CHECK(skyLightFactor() == 1.0f);

    setTime(TimeMidnight);
    CHECK_NEAR(skyLightFactor(), MinSkyLightFactor, 0.001);
    CHECK(MinSkyLightFactor > 0.0f);
    CHECK(MinSkyLightFactor < 1.0f);

    // Never brighter than full daylight and never actually black, all cycle long.
    for(unsigned int t = 0; t < TicksPerDay; t += 61)
    {
        setTime(t);
        const float factor = skyLightFactor();
        CHECK(factor > 0.0f);
        CHECK(factor <= 1.0f);
        CHECK(factor >= MinSkyLightFactor - 0.001f);
    }

    // It tracks the light level: darker sky, smaller factor, and full daylight
    // only when the light level is at its maximum.
    setTime(TimeSunrise);
    const float dawn = skyLightFactor();
    setTime(TimeNoon);
    CHECK(skyLightFactor() >= dawn);
    setTime(TimeSunset);
    CHECK_NEAR(skyLightFactor(), dawn, 0.02);

    for(unsigned int t = 0; t < TicksPerDay; t += 61)
    {
        setTime(t);
        CHECK((skyLightFactor() >= 1.0f) == (skyLightLevel() >= MaxSkyLight));
    }

    // A light floor (night vision lifts the sky) brightens the terrain without
    // ever reaching past full daylight. Survival::NightVisionLightFloor is the
    // floor the gameplay code passes; its range is asserted in survival_test.
    setTime(TimeMidnight);
    CHECK(skyLightFactor(150) > skyLightFactor());
    CHECK(skyLightFactor(150) <= 1.0f);
    CHECK(skyLightFactor(0) == skyLightFactor());
    CHECK(skyLightFactor(MaxSkyLight) == 1.0f);
    // A floor above the maximum cannot push the factor past 1.0.
    CHECK(skyLightFactor(100000) == 1.0f);
}

static void test_sky_colour()
{
    setTime(TimeNoon);
    SkyColor day = skyColor();
    CHECK(day.b > day.r);                 // blue sky
    CHECK(day.b > 0.9f && day.r > 0.6f);  // bright

    setTime(TimeMidnight);
    SkyColor night = skyColor();
    CHECK(night.r < 0.2f && night.g < 0.2f && night.b < 0.3f); // dark
    CHECK(night.b >= night.r);                                 // and blue-ish

    // Sunrise and sunset are warm.
    setTime(TimeSunrise);
    SkyColor dawn = skyColor();
    CHECK(dawn.r > dawn.b);
    setTime(TimeSunset);
    SkyColor dusk = skyColor();
    CHECK(dusk.r > dusk.b);

    // The glow reaches further than the light does, so the sky is already warm
    // before the sun is up and still warm after it is down. Without that, dusk
    // is just an earlier midnight and the horizon flips colour on one frame.
    setTime(12500); // the sun about 12 degrees below the horizon
    CHECK(sunElevationDegrees() <= NightElevation);
    SkyColor after_sunset = skyColor();
    CHECK(after_sunset.r > night.r);
    CHECK(after_sunset.r < dusk.r);
    setTime(23500); // the same height on the other side, before sunrise
    CHECK(sunElevationDegrees() <= NightElevation);
    CHECK_NEAR(skyColor().r, after_sunset.r, 0.02);

    // Past the glow it is the plain night colour again, so the band does not
    // tint the whole night.
    setTime(TimeMidnight);
    CHECK_NEAR(skyColor().r, night.r, 0.001);

    // Every component stays inside the valid range all cycle long.
    for(unsigned int t = 0; t < TicksPerDay; t += 97)
    {
        setTime(t);
        SkyColor c = skyColor();
        CHECK(c.r >= 0.0f && c.r <= 1.0f);
        CHECK(c.g >= 0.0f && c.g <= 1.0f);
        CHECK(c.b >= 0.0f && c.b <= 1.0f);
    }
}

static void test_celestial_projection()
{
    const int ppd = 4;

    // Straight ahead: body exactly where the camera points.
    int ox = 999, oy = 999;
    celestialScreenOffset(90, 0, 90, 0, ppd, ox, oy);
    CHECK(ox == 0 && oy == 0);

    // Camera looking up by 30 degrees, body on the horizon: it appears below.
    celestialScreenOffset(0, -30, 0, 0, ppd, ox, oy);
    CHECK(ox == 0);
    CHECK(oy == 120); // 30 degrees * 4 px, downward (positive y is down)

    // Body 30 degrees above the horizon, camera level: it appears above.
    celestialScreenOffset(0, 0, 0, 30, ppd, ox, oy);
    CHECK(oy == -120);

    // Camera right is yaw + 90, so a body there is to the right of centre.
    celestialScreenOffset(0, 0, 90, 0, ppd, ox, oy);
    CHECK(ox == 90 * ppd);
    celestialScreenOffset(0, 0, 270, 0, ppd, ox, oy);
    CHECK(ox == -90 * ppd);

    // Wrapping: a body just behind the camera must not jump to the far side.
    celestialScreenOffset(0, 0, 350, 0, ppd, ox, oy);
    CHECK(ox == -10 * ppd);
    celestialScreenOffset(0, 0, 10, 0, ppd, ox, oy);
    CHECK(ox == 10 * ppd);

    // Pitch adds to apparent elevation: looking down lifts the horizon up.
    celestialScreenOffset(0, 45, 0, 0, ppd, ox, oy);
    CHECK(oy == -45 * ppd);

    CHECK(pixelsPerDegree(320) == 4);
    CHECK(pixelsPerDegree(160) == 2);
    CHECK(pixelsPerDegree(1) == 1);  // never zero
    CHECK(pixelsPerDegree(0) == 1);
}

static void test_camera_pitch_sign()
{
    // xr is the engine's pitch: 0 on the horizon, growing as the camera looks
    // *down*, wrapping through 270 at straight up (the world task decreases it
    // for "look up", and crosshairRay's forward y is -sin(xr)). The sky offset
    // function wants the same angle in [-180, 180), positive looking down.
    CHECK(cameraPitch(0) == 0);
    CHECK(cameraPitch(30) == 30);
    CHECK(cameraPitch(90) == 90);
    CHECK(cameraPitch(330) == -30);
    CHECK(cameraPitch(270) == -90);
    CHECK(cameraPitch(359) == -1);
    CHECK(cameraPitch(181) == -179);
    // An xr that has not been normalised yet, or a negative one, still lands in
    // the range rather than throwing the sky to the far side of the screen.
    CHECK(cameraPitch(400) == 40);
    CHECK(cameraPitch(-30) == -30);
    CHECK(cameraPitch(-330) == 30);

    // The sky has to sweep past the player as they pitch: a body on the horizon
    // is *below* the centre while the camera looks up, and *above* it while the
    // camera looks down. Handing the offset function the raw xr, or -xr, is what
    // breaks this, and it is invisible until a player looks up at night.
    const int ppd = 4;
    int ox = 0, oy = 0;

    celestialScreenOffset(0, cameraPitch(330), 0, 0, ppd, ox, oy);
    CHECK(oy == 30 * ppd);

    celestialScreenOffset(0, cameraPitch(30), 0, 0, ppd, ox, oy);
    CHECK(oy == -30 * ppd);

    // A star 30 degrees up lands in the middle of the screen when the camera
    // looks up by 30, and the noon sun is centred when the camera looks straight
    // up -- the two cases that make the sun and moon feel attached to the sky.
    celestialScreenOffset(0, cameraPitch(330), 0, 30, ppd, ox, oy);
    CHECK(oy == 0);

    celestialScreenOffset(0, cameraPitch(270), 0, 90, ppd, ox, oy);
    CHECK(oy == 0);

    // The same body 90 degrees to the side of the view stays off to that side
    // whatever the pitch is: the two axes are independent.
    celestialScreenOffset(0, cameraPitch(270), 90, 90, ppd, ox, oy);
    CHECK(ox == 90 * ppd);
}

static void test_star_field()
{
    // Deterministic, in range, and spread out rather than clumped.
    int azimuths[StarCount];
    for(int i = 0; i < StarCount; ++i)
    {
        const Star &s = star(i);
        CHECK(s.azimuth >= 0 && s.azimuth < 360);
        CHECK(s.elevation >= 0 && s.elevation <= 90);
        azimuths[i] = s.azimuth;
    }
    for(int i = 0; i < StarCount; ++i)
        for(int j = i + 1; j < StarCount; ++j)
            CHECK(azimuths[i] != azimuths[j]);

    // Same index always gives the same star (no rebuild drift).
    CHECK(star(7).azimuth == star(7).azimuth);
    CHECK(star(7).elevation == star(7).elevation);
    // Out of range indices wrap.
    CHECK(star(StarCount).azimuth == star(0).azimuth);
    CHECK(star(-1).azimuth == star(StarCount - 1).azimuth);
}

static void test_wall_clock()
{
    char buffer[16];

    setTime(TimeSunrise);
    formatClock(buffer, sizeof(buffer));
    CHECK(strcmp(buffer, "06:00") == 0);

    setTime(TimeNoon);
    formatClock(buffer, sizeof(buffer));
    CHECK(strcmp(buffer, "12:00") == 0);

    setTime(TimeSunset);
    formatClock(buffer, sizeof(buffer));
    CHECK(strcmp(buffer, "18:00") == 0);

    setTime(TimeMidnight);
    formatClock(buffer, sizeof(buffer));
    CHECK(strcmp(buffer, "00:00") == 0);

    // Always four characters plus the terminator, all cycle long.
    for(unsigned int t = 0; t < TicksPerDay; t += 313)
    {
        setTime(t);
        formatClock(buffer, sizeof(buffer));
        CHECK(strlen(buffer) == 5);
        CHECK(buffer[2] == ':');
    }

    // A too-small buffer must not be overrun.
    char tiny[3] = { 'x', 'x', 'x' };
    setTime(0);
    formatClock(tiny, sizeof(tiny));
    CHECK(strlen(tiny) < sizeof(tiny));

    formatClock(nullptr, 8); // must not crash
}

// The clock is advanced from elapsed wall-clock milliseconds, not from frames or
// simulation steps, so the day lasts the same real time on the CX (a few logic
// steps per second) as on the desktop (tens per second).
static void test_moon_phases()
{
    // The phase walks one step per day and wraps over the month, so two nights in
    // a row never look the same and the first night is a full moon.
    reset();
    CHECK(moonPhase() == MoonPhases / 2);
    CHECK(dayCount() == 0);

    int seen[MoonPhases] = {};
    for(unsigned int day = 0; day < static_cast<unsigned int>(MoonPhases); ++day)
    {
        CHECK(moonPhase() >= 0 && moonPhase() < MoonPhases);
        ++seen[moonPhase()];
        setTime(TicksPerDay - 1);
        advance(1); // one day later
    }

    for(int phase = 0; phase < MoonPhases; ++phase)
        CHECK(seen[phase] == 1); // every phase exactly once per month

    CHECK(moonPhase() == MoonPhases / 2); // and back to a full moon
}

static void test_celestial_discs()
{
    // A body is a round disc of pixels, not a square.
    CHECK(discPixel(4, 0, 0));
    CHECK(discPixel(4, 4, 0));
    CHECK(discPixel(4, 0, -4));
    CHECK(!discPixel(4, 5, 0));
    CHECK(!discPixel(4, 3, 3));
    CHECK(!discPixel(4, -4, -4));
    CHECK(!discPixel(-1, 0, 0));

    const int radius = 8;

    // A new moon is dark, a full moon is whole, and nothing outside the disc is
    // ever lit whatever the phase says.
    int counts[MoonPhases] = {};
    for(int phase = 0; phase < MoonPhases; ++phase)
    {
        int lit = 0;
        for(int dy = -radius - 2; dy <= radius + 2; ++dy)
            for(int dx = -radius - 2; dx <= radius + 2; ++dx)
            {
                if(moonPixel(radius, dx, dy, phase))
                {
                    ++lit;
                    CHECK(discPixel(radius, dx, dy)); // never outside the disc
                }
            }
        counts[phase] = lit;
    }

    CHECK(counts[0] == 0);
    int full = 0;
    for(int dy = -radius; dy <= radius; ++dy)
        for(int dx = -radius; dx <= radius; ++dx)
            if(discPixel(radius, dx, dy))
                ++full;
    CHECK(counts[MoonPhases / 2] == full);

    // Crescent to full to crescent: the lit area grows to the full moon and then
    // shrinks again, and the month is symmetric around it.
    for(int phase = 1; phase < MoonPhases / 2; ++phase)
        CHECK(counts[phase] > counts[phase - 1]);
    for(int phase = MoonPhases / 2 + 1; phase < MoonPhases; ++phase)
        CHECK(counts[phase] < counts[phase - 1]);
    for(int phase = 0; phase < MoonPhases; ++phase)
        CHECK(counts[phase] == counts[(MoonPhases - phase) % MoonPhases]);

    // The second half of the month is the mirror image of the first, which is
    // what makes the moon wax on one side and wane on the other.
    for(int phase = 0; phase < MoonPhases; ++phase)
        for(int dy = -radius; dy <= radius; ++dy)
            for(int dx = -radius; dx <= radius; ++dx)
                CHECK(moonPixel(radius, dx, dy, phase) == moonPixel(radius, -dx, dy, (MoonPhases - phase) % MoonPhases));

    // ...and at the quarters the lit side really is the other side: the limb is
    // lit while its mirror pixel is in shadow, and the other way round a week later.
    const int waxing = MoonPhases / 4;
    const int waning = MoonPhases - waxing;
    for(int dy = -radius; dy <= radius; ++dy)
    {
        if(!discPixel(radius, radius - 1, dy))
            continue; // its mirror pixel is outside the disc, so there is nothing to compare

        CHECK(moonPixel(radius, radius - 1, dy, waxing));
        CHECK(!moonPixel(radius, -(radius - 1), dy, waxing));
        CHECK(!moonPixel(radius, radius - 1, dy, waning));
        CHECK(moonPixel(radius, -(radius - 1), dy, waning));
    }

    // A phase outside the month is treated as a new moon rather than as junk.
    CHECK(!moonPixel(radius, 0, 0, -1));
    CHECK(!moonPixel(radius, 0, 0, MoonPhases));
}

static void test_elapsed_advance()
{
    setDayLengthSeconds(DefaultDayLengthSeconds);
    const unsigned int ms_per_day = DefaultDayLengthSeconds * 1000u;

    reset();
    CHECK(dayLengthSeconds() == DefaultDayLengthSeconds);
    CHECK(advanceElapsed(0) == 0);
    CHECK(time() == TimeSunrise);

    // A whole day of elapsed time is a whole day of ticks, and wraps.
    CHECK(advanceElapsed(ms_per_day) == TicksPerDay);
    CHECK(time() == TimeSunrise);
    CHECK(dayCount() == 1);

    // More than a day at once still counts every day.
    CHECK(advanceElapsed(ms_per_day * 2 + ms_per_day / 2) == TicksPerDay * 2 + TicksPerDay / 2);
    CHECK(time() == TimeSunrise + TicksPerDay / 2);
    CHECK(dayCount() == 3);

    // A quarter day of elapsed time is a quarter of the ticks.
    reset();
    CHECK(advanceElapsed(ms_per_day / 4) == TicksPerDay / 4);

    // The sub-tick remainder is carried, so stepping in small frames advances the
    // clock by exactly the same amount as one large step: no time is lost and no
    // time is invented. This is what makes the cycle frame-rate independent.
    reset();
    unsigned long long stepped = 0;
    for(int i = 0; i < 40000; ++i)
        stepped += advanceElapsed(7); // 7 ms frames, a deliberately awkward size
    const unsigned int fancy_total = 40000 * 7;
    reset();
    const unsigned int single = advanceElapsed(fancy_total);
    CHECK(stepped == single);
    CHECK(single == static_cast<unsigned int>(
        (static_cast<unsigned long long>(fancy_total) * TicksPerDay) / ms_per_day));

    // The CX steps its logic about three times a second and the desktop about
    // thirty; the elapsed-time path must not care.
    reset();
    unsigned long long cx_ticks = 0;
    unsigned long long desktop_ticks = 0;
    const unsigned int shared_elapsed = 60000; // one minute of play
    for(unsigned int ms = 0; ms < shared_elapsed; ms += 300)
        cx_ticks += advanceElapsed(300);
    reset();
    for(unsigned int ms = 0; ms < shared_elapsed; ms += 33)
        desktop_ticks += advanceElapsed(33);
    CHECK(cx_ticks == desktop_ticks);
    CHECK(cx_ticks == static_cast<unsigned int>(
        (static_cast<unsigned long long>(shared_elapsed) * TicksPerDay) / ms_per_day));

    // Day length is a setting: shorter days advance the clock faster, and the
    // clamp keeps it inside the documented range.
    setDayLengthSeconds(MinDayLengthSeconds);
    CHECK(dayLengthSeconds() == MinDayLengthSeconds);
    setDayLengthSeconds(1);
    CHECK(dayLengthSeconds() == MinDayLengthSeconds); // clamped up
    setDayLengthSeconds(0);
    CHECK(dayLengthSeconds() == MinDayLengthSeconds);
    setDayLengthSeconds(999999);
    CHECK(dayLengthSeconds() == MaxDayLengthSeconds);
    setDayLengthSeconds(DefaultDayLengthSeconds);

    // Changing the length mid-day must not jump the clock: the part of the day
    // already elapsed is preserved.
    reset();
    advanceElapsed(DefaultDayLengthSeconds * 1000u / 2); // half a day
    CHECK(time() == TicksPerDay / 2);
    setDayLengthSeconds(DefaultDayLengthSeconds / 2); // now a day is half as long
    CHECK(time() == TicksPerDay / 2); // unchanged by the switch itself
    // The rest of the day is now half of the *new* length, i.e. a quarter of the
    // old one in real seconds: the clock keeps the fraction of the day it had.
    const unsigned int finished = advanceElapsed(DefaultDayLengthSeconds / 4 * 1000u);
    CHECK(finished == TicksPerDay / 2);
    CHECK(time() == TimeSunrise);
    CHECK(dayCount() == 1);

    setDayLengthSeconds(DefaultDayLengthSeconds);
}

static void test_sky_rotation()
{
    setTime(TimeSunrise);
    CHECK(skyRotationDegrees() == 0);

    // The sphere turns a full circle over one day, in step with the sun.
    for(unsigned int t = 0; t < TicksPerDay; t += 193)
    {
        setTime(t);
        const int rotation = skyRotationDegrees();
        CHECK(rotation >= 0 && rotation < 360);
        const int expected = (sunAzimuthDegrees() + 270) % 360;
        CHECK(rotation == expected);
    }

    setTime(TimeNoon);
    CHECK(skyRotationDegrees() == 90);
    setTime(TimeMidnight);
    CHECK(skyRotationDegrees() == 270);
}

int main()
{
    printf("worldclock_test\n");

    test_clock_basics();
    test_ticks_until();
    test_sun_and_moon_geometry();
    test_night_and_twilight();
    test_sky_light();
    test_sky_light_factor();
    test_sky_colour();
    test_celestial_projection();
    test_camera_pitch_sign();
    test_star_field();
    test_wall_clock();
    test_moon_phases();
    test_celestial_discs();
    test_elapsed_advance();
    test_sky_rotation();

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
