// The sky for WorldTask: the time-of-day background colour, the sun, moon and
// stars, and the advance of the day/night clock.
//
// The sky is painted straight into the framebuffer rather than as geometry:
// a celestial body is a small square of pixels and a star is one pixel, which on
// the CX is free next to a single textured triangle. Terrain drawn afterwards
// simply covers the part of the sky in front of it, so there is no depth or
// matrix work involved either.
//
// The clock itself lives in worldclock.h, which is pure and host-tested; this
// file only feeds it real elapsed time and paints the result.

#include "worldtask.h"

#include "settingstask.h"
#include "weather.h"
#include "worldclock.h"

namespace
{
    /**
     * Applies the weather's darkening to a sky colour. Rain and cloud dim the
     * background out of step with the terrain tint on purpose: the tint is
     * clamped so the world stays readable, while a darker sky is what makes the
     * rain visible against it.
     */
    float darken(float channel, int darkness)
    {
        if(darkness <= 0)
            return channel;
        return channel * static_cast<float>(Weather::MaxIntensity - darkness)
            / static_cast<float>(Weather::MaxIntensity);
    }
}

void WorldTask::skyPixel(int x, int y, unsigned short color)
{
    // Same screen-space convention as crosshairPixel, but clipped: a celestial
    // body only half inside the view must not wrap onto the other edge.
    const int px = SCREEN_WIDTH/2 + x;
    const int py = SCREEN_HEIGHT/2 + y;
    if(px < 0 || py < 0 || px >= SCREEN_WIDTH || py >= SCREEN_HEIGHT)
        return;
    screen->bitmap[px + py*SCREEN_WIDTH] = color;
}

void WorldTask::skyBody(int azimuth, int elevation, int radius, unsigned short color)
{
    // The view direction is (sin yr*cos xr, -sin xr, cos yr*cos xr), so the view
    // elevation in degrees is -xr mapped into [-180, 180] (positive = up), and
    // the yaw frame is the same one the clock's azimuths are measured in.
    int pitch = -static_cast<int>(xr);
    while(pitch > 180)
        pitch -= 360;
    while(pitch < -180)
        pitch += 360;

    int ox = 0, oy = 0;
    WorldClock::celestialScreenOffset(static_cast<int>(yr), pitch, azimuth, elevation,
                                      WorldClock::pixelsPerDegree(SCREEN_WIDTH), ox, oy);

    for(int dy = -radius; dy <= radius; ++dy)
        for(int dx = -radius; dx <= radius; ++dx)
            skyPixel(ox + dx, oy + dy, color);
}

void WorldTask::renderSky()
{
    // With the cycle switched off the game keeps the flat daytime sky it always
    // had, and the terrain is left untinted (the cheapest possible frame).
    if(settings_task.getValue(SettingsTask::DAY_NIGHT) == 0)
    {
        glColor3f(0.75f, 0.85f, 1.0f); //c0d8ff background
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return;
    }

    const WorldClock::SkyColor sky = WorldClock::skyColor();
    glColor3f(darken(sky.r, weather_darkness), darken(sky.g, weather_darkness), darken(sky.b, weather_darkness));
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // A graph is a plot, not a place: it gets the time-of-day background colour
    // but no sun, moon or stars over the curve.
    if(world.worldType() == World::WorldType::Graph)
        return;

    // Everything below is written straight into the framebuffer, so it needs no
    // texture, no matrix work and no depth test: the terrain drawn afterwards
    // simply covers the part of the sky it should.
    const int rotation = WorldClock::skyRotationDegrees();

    if(WorldClock::sunElevationDegrees() < WorldClock::NightElevation)
    {
        const unsigned short star_color = colorRGB(GLFix(0.55f), GLFix(0.55f), GLFix(0.7f));
        for(int i = 0; i < WorldClock::StarCount; ++i)
        {
            const WorldClock::Star &s = WorldClock::star(i);
            skyBody((s.azimuth + rotation) % 360, s.elevation, 0, star_color);
        }
    }

    // A body is still drawn a little below the horizon so it appears to sink
    // into the ground rather than blink out.
    constexpr int sink = 6;
    const int radius = SCREEN_WIDTH / 40;

    if(WorldClock::sunElevationDegrees() > -sink)
        skyBody(WorldClock::sunAzimuthDegrees(), WorldClock::sunElevationDegrees(), radius,
                colorRGB(GLFix(1), GLFix(0.96f), GLFix(0.72f)));

    if(WorldClock::moonElevationDegrees() > -sink)
        skyBody(WorldClock::moonAzimuthDegrees(), WorldClock::moonElevationDegrees(), radius,
                colorRGB(GLFix(0.82f), GLFix(0.85f), GLFix(0.95f)));
}

void WorldTask::updateClock(GLFix dt)
{
    if(settings_task.getValue(SettingsTask::DAY_NIGHT) == 0)
        return;
    // A static world (ticks disabled) keeps the time of day it was paused at.
    if(settings_task.getValue(SettingsTask::TICKS_ENABLED) == 0)
        return;

    // dt is in units of the nominal tick, so multiplying gives the frame's real
    // elapsed milliseconds: the cycle then lasts the same on the CX (~3 steps a
    // second) as on a desktop, and a slow frame advances the clock by more.
    const GLFix elapsed_ms = dt * GLFix(static_cast<int>(simulation_tick_ms));
    if(elapsed_ms <= GLFix(0))
        return;

    WorldClock::advanceElapsed(static_cast<unsigned int>(elapsed_ms));
}
