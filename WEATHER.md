# Weather (Stage 2)

`weather.h` / `weather.cpp` decide what the weather is. `worldweather.cpp` draws
it. `worldsky.cpp` darkens the sky for it, and `worldtask.cpp` darkens the world.

## No new state, no new save fields

The weather at any moment is a pure function of the **world seed** and the
**total game ticks the world clock has counted**:

```
total_ticks = dayCount() * TicksPerDay + time()
```

Both of those are already in the save file (format v9), so:

* a reloaded world resumes exactly the weather it was having, with nothing new
  stored and no migration,
* a new world gets its own weather from its own seed,
* the CX (~3 logic steps a second) and the desktop build agree, because the
  clock is advanced from real elapsed time rather than from frames.

`tests/weather_test.cc` walks hundreds of game days through the same code the
calculator runs and pins the behaviour down (2.9 million checks).

## How time is divided

* **Window** – 2400 ticks, two minutes of game time. A window's weather is a
  hash of the seed and the window number.
* **Season** – six windows. A season shares one slow "storminess" roll, and 55%
  of seasons are stormy. That is what makes dry spells last: without it the
  weather would flip a coin every two minutes.
* Within a stormy season a window is 45% dry, 40% rain and 15% thunder.

Measured over a hundred game days: **clear 69%, rain 22%, thunder 8%**, with the
longest dry spell about 74 minutes and a weather change every few minutes.

The intensity **ramps** over 300 ticks at the start and end of a spell, and the
ramp is skipped on a side where the neighbouring window has the same weather, so
a rain spell spanning several windows reads as one long rain. Tick by tick the
intensity never jumps by more than one step (asserted by the test).

## Thunder

A thunderstorm flashes every 400–1400 ticks. The period belongs to the *season*
rather than the window, so the rhythm carries across a storm that spans several
windows instead of restarting every two minutes.

A flash lasts `FlashTicks` (5 ticks = 0.25 s). That is deliberately longer than
the "correct" instant: the CX logic loop runs at about three frames a second, so
a shorter flash could pass entirely between two frames and never be seen.

## Rendering

* **Rain** is screen-space streaks blended straight into the framebuffer, in
  front of the 3D scene and behind the HUD. This is the mirror image of what
  `worldsky.cpp` does with the sun and the stars. Blending rather than
  overwriting keeps the rain visible both against the bright sky and over dark
  terrain; a streak is 3–9 pixels and heaviest rain is at most 96 streaks, so
  even a downpour is under a thousand pixel writes per frame. The fall speed is
  in pixels per second, converted through the same `dt * tick_ms` the clock uses.
* **Indoors there is no rain.** One column scan from the player's head upwards:
  any block above stops the streaks. Rain through a roof is the classic tell
  that a weather system is faked.
* **The sky darkens** by up to 96/256 for rain and 150/256 for a thunderstorm
  (`worldsky.cpp`), and the terrain tint darkens by the same fraction
  (`worldtask.cpp`).
* **A lightning flash** is one full-screen blend towards white. It is an overlay
  rather than a brighter global shade on purpose: `nglSetGlobalShade()` is
  already at its neutral ceiling of 256 in daylight and pushing it further would
  overflow into the colour channels.
* The weather name rides along on the **FPS line** when that setting is on, and
  a change is announced once through the normal message channel
  ("Rain" / "Thunderstorm" / "The rain stops").

## Settings

**Weather** (off/on, default on) turns the whole thing off. It requires the
day/night cycle to be on as well, because the weather is derived from that
clock; with the cycle off the clock does not advance and the weather would be
frozen, so the frame stays dry instead. The setting is appended last in the
settings table, so older save files keep loading.

## Cost

Per frame, only when it is raining: one column scan (at most 40 block lookups),
the streak blits, and — on a flash — one blend over all 76,800 pixels. Nothing
runs at all when it is clear, and nothing is allocated.

## Still open

* No rain or thunder **sound**. The audio pack has no verified rain bed, and
  inventing one was out of scope for this pass.
* Rain does not yet **extinguish fires** or **fill cauldrons / irrigate farms**,
  and lightning does not start fires. Those need the block tick systems to be
  revisited and cannot be verified without hardware.
* Snow is not possible: there is no snow or ice block id (see WORLDGEN.md).
* Nothing here has been seen on real hardware — the behaviour is host-tested,
  the look is not.
