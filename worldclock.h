#ifndef WORLDCLOCK_H
#define WORLDCLOCK_H

#include <stdint.h>

/**
 * Deterministic day/night cycle.
 *
 * The clock is a plain tick counter, so it is a pure function of the world and
 * can be saved, restored, advanced by /time or by sleeping, and unit tested on
 * a host without any engine dependency.
 *
 * Geometry of the sky (angles in degrees, azimuth increasing eastwards from the
 * engine's +z axis, matching the camera convention where yaw 0 looks along +z
 * and yaw +90 looks along +x):
 *
 *   phase        = time / TicksPerDay                     in [0, 1)
 *   sunElevation = 90 * sin(360 * phase)                  +90 at noon, -90 at midnight
 *   sunAzimuth   = 90 + 360 * phase
 *   moon         = sun + 180 degrees, negated elevation
 *
 * skyLightLevel() is the 0..255 exposure the renderer uses for the global light
 * tint, so the world darkens smoothly through dusk rather than switching.
 *
 * The clock counts ticks (24000 per day) but is advanced from *elapsed wall-clock
 * time*, not from simulation steps. That matters on the CX, whose logic loop runs
 * at only a few hertz: advancing one tick per simulation step would stretch a day
 * to hours, and the step rate differs per platform. `advanceElapsed` takes the
 * same `dt * tick_ms` milliseconds the main loop already computes, so a day lasts
 * `dayLengthSeconds()` regardless of frame rate or how the caller batches steps.
 *
 * A "static" world (ticks disabled) simply does not advance the clock.
 */
namespace WorldClock
{
	/** Ticks in one full day/night cycle (Minecraft's 24000). */
	constexpr unsigned int TicksPerDay = 24000;

	/** Named times, in ticks since dawn (time 0 is sunrise). */
	constexpr unsigned int TimeSunrise = 0;
	constexpr unsigned int TimeNoon = 6000;
	constexpr unsigned int TimeSunset = 12000;
	constexpr unsigned int TimeDusk = 13000;
	constexpr unsigned int TimeMidnight = 18000;
	constexpr unsigned int TimeSunsetEnd = 22000;

	/** Sun elevation below which the sky counts as night (spawns, stars). */
	constexpr int NightElevation = -6;

	/** Darkest sky light level; keeps the screen readable at midnight. */
	constexpr int MinSkyLight = 40;
	/** Brightest sky light level. */
	constexpr int MaxSkyLight = 255;
	/**
	 * How dark the terrain tint is allowed to get. Full darkness would be
	 * unplayable on a reflective LCD in daylight, and the CX has no backlight
	 * setting to compensate with.
	 */
	constexpr float MinSkyLightFactor = 0.45f;

	struct SkyColor
	{
		/** Component order matches glColor3f(r, g, b). */
		float r, g, b;
	};

	/** Default day length: 20 real minutes, like Minecraft. **/
	constexpr unsigned int DefaultDayLengthSeconds = 20 * 60;
	/** Clamp for the setting, so a day cannot become a slideshow or a blink. */
	constexpr unsigned int MinDayLengthSeconds = 60;
	constexpr unsigned int MaxDayLengthSeconds = 2 * 60 * 60;

	/**
	 * Resets the clock for a new world: time, day counter and the sub-tick carry.
	 * The day length is a setting rather than world state, so it is left alone.
	 */
	void reset(unsigned int time = TimeSunrise);

	unsigned int time();
	void setTime(unsigned int new_time);
	/**
	 * Puts a saved world back where it left off. The sub-tick carry is dropped on
	 * purpose: it is at most one tick of a day, and starting a loaded world with a
	 * clean clock keeps loading deterministic.
	 */
	void restore(unsigned int saved_time, unsigned int saved_days);
	/** Adds ticks, wrapping at TicksPerDay. */
	void advance(unsigned int ticks);
	/**
	 * Advances by real elapsed milliseconds (the caller's `dt * tick_ms`),
	 * converting through the configured day length. The sub-tick remainder is
	 * carried, so a fast frame rate cannot lose time. Returns the whole ticks
	 * the clock moved by.
	 */
	unsigned int advanceElapsed(unsigned int elapsed_ms);
	/** Whole days elapsed since the clock started (for the debug screen). */
	unsigned int dayCount();

	/** Real seconds one in-game day lasts, clamped to the documented range. */
	void setDayLengthSeconds(unsigned int seconds);
	unsigned int dayLengthSeconds();

	/**
	 * Ticks to wait from `from` until `target`, wrapping once. Used by /time and
	 * by sleeping in a bed, where only the next occurrence of the target matters.
	 */
	unsigned int ticksUntil(unsigned int from, unsigned int target);

	float phase(); ///< 0..1 through the current day
	int sunElevationDegrees();
	int sunAzimuthDegrees();
	int moonElevationDegrees();
	int moonAzimuthDegrees();
	bool sunAboveHorizon();
	bool moonAboveHorizon();
	bool isNight();

	/** True while the sun is within `degrees` of the horizon (dawn or dusk). */
	bool isTwilight(int degrees = 12);

	/**
	 * How far the whole celestial sphere has turned, so the star field rotates
	 * with the sun and moon instead of standing still. 0 at sunrise.
	 */
	int skyRotationDegrees();

	/** 0..255 global sky exposure for the terrain light tint. */
	int skyLightLevel();
	/** Same as skyLightLevel but never falling below `floor_level` (night vision). */
	int skyLightLevelAtLeast(int floor_level);
	/**
	 * Texture-tint factor for the terrain, from MinSkyLightFactor at midnight to
	 * 1.0 in full daylight, so the renderer can multiply every texel by it. A
	 * floor of `1.0` would make it a no-op, which is what the caller wants when
	 * the day/night cycle is switched off.
	 */
	float skyLightFactor(int floor_level = 0);

	/** Background/sky colour for the current time, including dawn/dusk tint. */
	SkyColor skyColor();

	/** "06:00" style wall clock, for the debug screen and messages. */
	void formatClock(char *out, unsigned int size);

	/**
	 * Where a celestial body at (azimuth, elevation) lands on screen, in pixels
	 * relative to the screen centre, for a camera looking at (yaw, pitch).
	 *
	 * `pixels_per_degree` is a linear approximation of the projection: the real
	 * projection is perspective, but a body that far away is nearly at infinity
	 * and stretching it linearly keeps the math allocation-free and testable.
	 */
	void celestialScreenOffset(int camera_yaw, int camera_pitch, int azimuth, int elevation,
	                           int pixels_per_degree, int &out_x, int &out_y);

	/** Degrees to pixels for a screen `width`, so the sky matches the viewport. */
	int pixelsPerDegree(int screen_width);

	/** Deterministic star field (azimuth/elevation pairs), drawn at night. */
	struct Star
	{
		int azimuth;
		int elevation;
	};
	constexpr int StarCount = 48;
	const Star &star(int index);
}

#endif // WORLDCLOCK_H
