#include "worldclock.h"

#include <math.h>
#include <stdio.h>

namespace WorldClock
{
	namespace
	{
		unsigned int g_time = TimeSunrise;
		unsigned int g_days = 0;
		/**
		 * Elapsed time not yet converted into whole ticks, in units of
		 * (millisecond x TicksPerDay). Keeping the remainder in this scaled form
		 * means converting it back never rounds, so a frame can never leak time and
		 * the clock cannot drift at any frame rate. Always below msPerDay().
		 */
		unsigned long long g_carry = 0;
		unsigned int g_day_length_seconds = DefaultDayLengthSeconds;

		constexpr float Pi = 3.14159265f;

		/** sin of an angle in degrees. */
		float sinDeg(float degrees)
		{
			return sinf(degrees * (Pi / 180.0f));
		}

		/** Wraps to [0, 360). */
		int wrap360(int degrees)
		{
			degrees %= 360;
			if(degrees < 0)
				degrees += 360;
			return degrees;
		}

		/** Wraps to [-180, 180), the range the screen offset needs. */
		int wrap180(int degrees)
		{
			degrees = wrap360(degrees);
			if(degrees >= 180)
				degrees -= 360;
			return degrees;
		}

		float clamp01(float value)
		{
			if(value < 0.0f)
				return 0.0f;
			if(value > 1.0f)
				return 1.0f;
			return value;
		}

		/** Mixes two colours (linear, a in 0..1). */
		SkyColor mix(SkyColor a, SkyColor b, float amount)
		{
			SkyColor out;
			out.r = a.r + (b.r - a.r) * amount;
			out.g = a.g + (b.g - a.g) * amount;
			out.b = a.b + (b.b - a.b) * amount;
			return out;
		}
	}

	void reset(unsigned int time)
	{
		g_time = time % TicksPerDay;
		g_days = 0;
		g_carry = 0;
		// The day length is a setting, not world state, so it survives a reset.
	}

	void setDayLengthSeconds(unsigned int seconds)
	{
		if(seconds < MinDayLengthSeconds)
			seconds = MinDayLengthSeconds;
		else if(seconds > MaxDayLengthSeconds)
			seconds = MaxDayLengthSeconds;

		if(seconds == g_day_length_seconds)
			return;

		// Rescale the carry so the change neither jumps the clock nor loses the
		// part of the day that has already elapsed: the carry is a fraction of a
		// day, so it scales with the *new* length over the old one.
		g_carry = (g_carry * seconds) / g_day_length_seconds;
		g_day_length_seconds = seconds;
	}

	unsigned int dayLengthSeconds() { return g_day_length_seconds; }

	/** Milliseconds in one in-game day, the denominator of the carry. */
	static unsigned int msPerDay() { return g_day_length_seconds * 1000u; }

	unsigned int advanceElapsed(unsigned int elapsed_ms)
	{
		const unsigned int ms_per_day = msPerDay();
		if(ms_per_day == 0)
			return 0;

		g_carry += static_cast<unsigned long long>(elapsed_ms) * TicksPerDay;

		// 64-bit arithmetic is needed for one add and one divide per frame, which
		// is nothing next to the per-texel work in the renderer, and it is what
		// lets the remainder be carried without ever rounding a frame's time away.
		const unsigned int ticks = static_cast<unsigned int>(g_carry / ms_per_day);
		g_carry -= static_cast<unsigned long long>(ticks) * ms_per_day;

		if(ticks != 0)
			advance(ticks);
		return ticks;
	}

	unsigned int time() { return g_time; }

	void setTime(unsigned int new_time)
	{
		g_time = new_time % TicksPerDay;
	}

	void restore(unsigned int saved_time, unsigned int saved_days)
	{
		g_time = saved_time % TicksPerDay;
		g_days = saved_days;
		g_carry = 0;
	}

	void advance(unsigned int ticks)
	{
		// Count whole days first, then the wrap caused by the remainder, so that
		// advancing exactly one day (or a multiple) counts correctly rather than
		// being swallowed by the wrap check.
		g_days += ticks / TicksPerDay;

		const unsigned int wrapped = ticks % TicksPerDay;
		if(g_time + wrapped >= TicksPerDay)
			++g_days;
		g_time = (g_time + wrapped) % TicksPerDay;
	}

	unsigned int dayCount() { return g_days; }

	int moonPhase()
	{
		// The first day of a world gets a full moon (4), and the phase then walks
		// one step per day, so a night never looks like the one before it and the
		// whole month is over in eight days -- as fast as the week is here.
		return static_cast<int>((static_cast<unsigned int>(MoonPhases / 2) + g_days) % static_cast<unsigned int>(MoonPhases));
	}

	bool discPixel(int radius, int dx, int dy)
	{
		if(radius < 0)
			return false;
		return dx * dx + dy * dy <= radius * radius;
	}

	bool moonPixel(int radius, int dx, int dy, int phase)
	{
		if(!discPixel(radius, dx, dy))
			return false;
		if(phase <= 0 || phase >= MoonPhases)
			return false; // new moon: the whole disc is dark
		if(phase == MoonPhases / 2)
			return true; // full moon: nothing is in the way

		// How far the shadow has slid, as a percentage of the way from "the shadow
		// covers the disc" to "the shadow has cleared it": 0 at new, 100 at full.
		const int distance_from_full = phase < MoonPhases / 2 ? phase : MoonPhases - phase;
		const int progress = distance_from_full * 100 / (MoonPhases / 2);

		// The shadow is a second disc of the same size, its centre sliding two radii
		// out from the moon's centre. It covers the whole moon at new, half of it at
		// the quarters and nothing at full, and it comes from the left while the moon
		// is waxing and from the right while it is waning.
		const int slide = (phase < MoonPhases / 2 ? -1 : 1) * (2 * radius * progress) / 100;
		const int shadow_x = dx - slide;
		return shadow_x * shadow_x + dy * dy > radius * radius;
	}

	unsigned int ticksUntil(unsigned int from, unsigned int target)
	{
		const unsigned int a = from % TicksPerDay;
		const unsigned int b = target % TicksPerDay;
		return (b >= a) ? (b - a) : (TicksPerDay - a + b);
	}

	float phase()
	{
		return static_cast<float>(g_time) / static_cast<float>(TicksPerDay);
	}

	int sunElevationDegrees()
	{
		const float elevation = 90.0f * sinDeg(360.0f * phase());
		return static_cast<int>(elevation + (elevation < 0.0f ? -0.5f : 0.5f));
	}

	int sunAzimuthDegrees()
	{
		return wrap360(90 + static_cast<int>(360.0f * phase()));
	}

	int moonElevationDegrees() { return -sunElevationDegrees(); }

	int moonAzimuthDegrees() { return wrap360(sunAzimuthDegrees() + 180); }

	bool sunAboveHorizon() { return sunElevationDegrees() > 0; }

	bool moonAboveHorizon() { return moonElevationDegrees() > 0; }

	bool isNight() { return sunElevationDegrees() <= NightElevation; }

	bool isTwilight(int degrees)
	{
		const int elevation = sunElevationDegrees();
		return elevation > -degrees && elevation < degrees;
	}

	int skyRotationDegrees()
	{
		// Sunrise (azimuth 90) is the zero of the rotation, so the star field lines
		// up with the sun instead of turning at a different rate.
		return wrap360(sunAzimuthDegrees() - 90);
	}

	int skyLightLevel()
	{
		// Normalised sun height: -1 at midnight, +1 at noon.
		const float height = static_cast<float>(sunElevationDegrees()) / 90.0f;
		// Full brightness from well above the horizon, sliding to the floor once
		// the sun is a little below it so dusk is a gradual fade.
		const float lit = clamp01((height + 0.15f) / 0.65f);
		return MinSkyLight + static_cast<int>(lit * static_cast<float>(MaxSkyLight - MinSkyLight) + 0.5f);
	}

	int skyLightLevelAtLeast(int floor_level)
	{
		const int light = skyLightLevel();
		return light < floor_level ? floor_level : light;
	}

	float skyLightFactor(int floor_level)
	{
		int light = skyLightLevelAtLeast(floor_level);
		if(light < MinSkyLight)
			light = MinSkyLight;
		else if(light > MaxSkyLight)
			light = MaxSkyLight;

		const float t = static_cast<float>(light - MinSkyLight) / static_cast<float>(MaxSkyLight - MinSkyLight);
		return MinSkyLightFactor + (1.0f - MinSkyLightFactor) * t;
	}

	SkyColor skyColor()
	{
		// Vanilla-ish daytime blue, and the dark blue of a moonlit night.
		const SkyColor day = { 0.75f, 0.85f, 1.00f };
		const SkyColor night = { 0.04f, 0.05f, 0.13f };
		// Sunrise/sunset red-orange.
		const SkyColor twilight = { 0.95f, 0.55f, 0.25f };

		const float height = static_cast<float>(sunElevationDegrees()) / 90.0f;
		const float lit = clamp01((height + 0.15f) / 0.65f);

		SkyColor out = mix(night, day, lit);
		// Lay the horizon glow over the day/night blend near dawn and dusk.
		const int elevation = sunElevationDegrees();
		if(elevation > -14 && elevation < 14)
		{
			const float fade = 1.0f - (static_cast<float>(elevation < 0 ? -elevation : elevation) / 14.0f);
			out = mix(out, twilight, fade * 0.7f);
		}
		return out;
	}

	void formatClock(char *out, unsigned int size)
	{
		if(out == nullptr || size == 0)
			return;
		// Report time 0 (sunrise) as 06:00 so the clock reads like a real one.
		const unsigned int shifted = (g_time + 6000) % TicksPerDay;
		const unsigned int minutes_of_day = (shifted * 24 * 60) / TicksPerDay;
		const unsigned int hours = minutes_of_day / 60;
		const unsigned int minutes = minutes_of_day % 60;
		snprintf(out, size, "%02u:%02u", hours, minutes);
	}

	int cameraPitch(int xr_degrees)
	{
		// Nothing more than the wrap, but the wrap is the whole point: 0 is the
		// horizon, 90 straight down and 270 straight up, so "straight up" has to
		// arrive at the offset function as -90 rather than as +270 or -270.
		return wrap180(xr_degrees);
	}

	void celestialScreenOffset(int camera_yaw, int camera_pitch, int azimuth, int elevation,
	                           int pixels_per_degree, int &out_x, int &out_y)
	{
		// Horizontal: the engine's camera right vector is yaw + 90, so a body at
		// azimuth (yaw + 90) sits on the right edge direction (positive offset).
		out_x = wrap180(azimuth - camera_yaw) * pixels_per_degree;
		// Vertical: positive pitch looks down, which moves the horizon up the
		// screen, so the body's apparent elevation gains the pitch.
		const int apparent = elevation + camera_pitch;
		out_y = -apparent * pixels_per_degree;
	}

	int pixelsPerDegree(int screen_width)
	{
		// The renderer has a fixed, fairly wide field of view; ~70 degrees across
		// the width keeps the sun and moon at a believable size and speed.
		const int per_degree = screen_width / 70;
		return per_degree > 1 ? per_degree : 1;
	}

	const Star &star(int index)
	{
		// A fixed, deterministic field: two coprime strides spread 48 stars over
		// the hemisphere without any table to maintain by hand.
		static Star stars[StarCount];
		static bool built = false;
		if(!built)
		{
			for(int i = 0; i < StarCount; ++i)
			{
				stars[i].azimuth = wrap360(i * 137);
				stars[i].elevation = 8 + ((i * 53) % 74);
			}
			built = true;
		}

		// Make out-of-range indices wrap like an index into a cycle, so a caller
		// can offset the star field by the time of day without bounds checks.
		int wrapped = index % StarCount;
		if(wrapped < 0)
			wrapped += StarCount;
		return stars[wrapped];
	}
}
