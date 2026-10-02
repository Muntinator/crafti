# Weather (Stage 2)

`weather.h` / `weather.cpp` decide what the weather is. `worldweather.cpp` draws
it and picks the sound. `worldweathersnow.cpp` is where it touches the world, and
`snowcover.h` / `snowcover.cpp` are the rules for the snow it leaves behind.
`worldsky.cpp` darkens the sky for it, `worldtask.cpp` darkens the world, and
`terrain.cpp` maps the snow layer's texture.

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

### A flash is a strike

The tick a flash starts is the tick the bolt lands, and it is resolved on the
rising edge of the flash only — the five ticks the flash is lit are one bolt, not
five. Which bolt it is comes from the tick count itself:

```
strike_index = total_ticks / flash_period
```

and its target is a hash of the seed and that index, an offset of at most
`StrikeRadius` (24) blocks in x and z. Both are pure, so the frame that draws the
flash and the frame that resolves it cannot disagree about where the bolt went,
and a reloaded world puts the same bolt in the same place.

24 blocks rather than vanilla's 128 on purpose: past the CX's render distance a
strike is only a flash with no impact to see, so keeping it close means the
burst of particles, the burning and the thunder are all in view.

A strike then does three things:

* it spawns destruction particles where it hit — the bolt falls onto the first
  block it meets, starting above the player's head, so a strike over a tree hits
  the leaves rather than the ground under them,
* it plays the thunder cue at the distance to the bolt, through the same
  `distanceVolume()` scale the footsteps and mobs use (0–31 blocks),
* it strikes **the player**, when the bolt lands on the block they are standing
  on give or take a step: `Survival::LightningDamage` (5) through a new
  `Damage::Lightning` source, which bypasses armour the way vanilla's
  `lightning_bolt` does, and which sets the victim alight for
  `FireTicksFromFireBlock` (8 s). Creative mode is not hurt, because all damage
  funnels through `applyDamage()`.

What a strike does **not** do: set the ground on fire, or hit mobs. There is no
fire block in this game to ignite (see ITEMS.md), and mobs do not tick against
the weather — both are recorded under *Still open*.

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
* **Snow** is single pixels drifting sideways instead of streaks. It uses the
  same shared falling offset and the same screen-space blend as the rain, so a
  snowstorm is *cheaper* than a shower (at most 56 pixels a frame against 96
  streaks of up to nine) and, like the rain, it stops at a roof. The flakes fall
  at 64–116 px/s rather than the rain's 90–230, so they read as drifting; the
  slowest of those is above 62 px/s on purpose, because the offset is kept in
  whole pixels and a speed that moves less than a pixel per frame would leave the
  snow hanging still on the 60 Hz desktop build while it moved on the calculator.
* **A lightning flash** is one full-screen blend towards white. It is an overlay
  rather than a brighter global shade on purpose: `nglSetGlobalShade()` is
  already at its neutral ceiling of 256 in daylight and pushing it further would
  overflow into the colour channels.
* The weather name rides along on the **FPS line** when that setting is on, and
  a change is announced once through the normal message channel
  ("Rain" / "Thunderstorm" / "The rain stops").

## Snow

Whether the water that falls freezes does **not** depend on the storm. It depends
on where the player is standing: `BiomeGen::isColdAt()` reads the same
temperature field the biomes are chosen from, so a shower is rain over a plains
and snow over a cold forest — walk between them and the drops turn into flakes
without the spell changing state. The temperature field is one value-noise
evaluation per frame, which is nothing.

`weather.h` keeps `State::Snow` as a spell of its own anyway, and it is the one
spell the clock never picks: `/weather snow` forces it, and a forced snow spell is
snow anywhere, including over a desert. That is the only way to see the flakes on
purpose, and it is what the host tests lean on. Snow's own strength (150/256) and
sky darkening (64/256) are lighter than the rain's, because a flake is a dot
where a drop is a streak.

### Snow cover

Snow settles as an ordinary block, `BLOCK_SNOW`, whose **data byte is its depth**:
1 to `SnowCover::MaxLayers` (4) steps of 2/16 of a block each. Nothing about it is
saved beyond the block itself — no extra fields, no migration, and the depth is
travelling in the world like any other block's data.

Once a second (`SnowCover::LayerTicks`) a **step** touches
`SnowCover::ColumnsPerStep` (3) columns within `Radius` (12) blocks of the
player. The step index is `total_ticks / LayerTicks`, so the snowfall is a
function of the clock: a reloaded world carries on rather than replaying, a
paused world has no snowfall, and if several steps are missed at once (a long
load) only the one in progress is applied — the snow cannot appear in one jump
and the cost of a step never multiplies.

The whole of that per-column decision is `SnowCover::columnStep()` in
`snowcover.h` — a template that takes the column's blocks through a lambda, so
the calculator passes one over `World::getBlock` and the host test passes one over
a table, and there is exactly one copy of the rule. What it decides:

1. the ground is the first block below the player's feet that can carry snow
   (within `MaxGroundDrop` (6) blocks) — precipitation falls through air, water,
   lava, torches, flowers, mushrooms, wheat, spiderwebs, redstone wire, pressure
   plates and dropped items, so none of those is a surface,
2. **a snow layer is skipped as ground**, so an existing drift deepens *in place*
   rather than stacking a new block on top of itself one step at a time,
3. the cell above the ground must be air or existing snow; a torch or a block the
   player put there is never overwritten,
4. the column must be **open to the sky** (no cover within `MaxSkyScan` (48)
   blocks, capped by the top of the world), which is what keeps caves and the
   insides of houses bare. The scan starts *above* the cell the layer occupies and
   ignores snow on the way up: otherwise a drift would be its own roof and could
   neither grow nor melt again — the bug the host test pins down,
5. then `SnowCover::step()` decides the new depth:

| weather at the column | depth |
| --- | --- |
| snowing and freezing | +1 step, up to 4 |
| freezing and dry | unchanged (a drift keeps) |
| above freezing, whatever is falling | −1 step, down to 0 |

Melting is unconditional on the weather on purpose: in a warm place the sun does
that, and it is what keeps a desert bare and clears a drift off a roof. It is also
what melts a layer that was placed by hand in a warm biome. In a cold biome the
snow never melts and simply piles up to its cap, so a first storm leaves the cold
regions white rather than merely wet.

Three columns a second over a 25×25 block neighbourhood means a real snowfall
takes a couple of minutes to touch every column once. Drifts therefore spread out
of the player rather than appearing everywhere at once, which is what snow
settling looks like.

### The snow block

* It is a plain block (`BLOCK_SNOW`, id 47, the last of `BLOCK_NORMAL_LAST`), so
  the block name table, `/give snow layer`, the inventory and the particles all
  work on it with no special cases.
* `snowrenderer.cpp` draws it as a hand-built slab, the same way the bed is drawn,
  with one texture for every face: the packs' own light grey tile at atlas (5,7),
  which no block used before. No art was authored for this — all three texture
  packs already have the tile.
* It is **not an obstacle**. This engine has no auto step-up, so a block that
  quietly appeared under a standing player and collided would be a trap; the AABB
  is the real slab, so it can still be aimed at and broken.
* It is **not opaque**, so the grass under it keeps its face and a buried layer
  costs nothing extra to draw.
* Its deepest step is half a block (`MaxLayers * 2/16`), so every depth is
  walk-through. Vanilla's eighth step is a full cube you stand on; that is
  deliberately not copied, and `tests/snowcover_test.cc` pins the half-block
  ceiling.
* Footsteps and digging on it use the audio pack's **snow** material, so the
  ground sounds different once it has settled.
* It is listed last on the Blocks page. The page only draws the first
  `fields_x * fields_y` entries and the bed filled the last visible slot, so snow
  is reachable through `/give snow layer` and sits with the other `/give`-only
  blocks rather than pushing a visible block off the page. Placing it by hand
  works, and a layer placed in a warm biome melts on its own.

## Sounds

The looping weather bed is started **once per change**, not once per frame:
starting a stream rewinds it, so asking for the same bed every frame would play
the first moment of the loop forever (`weather_audio` remembers what is playing).

| weather | bed |
| --- | --- |
| rain or snow | `AmbientWeatherRain1` |
| thunderstorm | `AmbientWeatherThunder1` |
| clear | silence |

Snow shares the rain's loop, which is what vanilla does too: this pack has no
separate snowfall loop, and the flakes are heard as snow underfoot instead.
Switching the Weather setting off stops the bed with it, so a shower cannot keep
playing over a clear sky.

Each lightning strike fires `GameAudio::weatherThunder(distance)` — a one-shot cue
at the distance to the bolt — on top of the bed. All of this needs the audio pack
(`crafti.audp`, see AUDIO_OUTPUT_TEST.md); without it these calls are no-ops and
the game is silent as before.

## The weather touching the world

Both world effects live in `worldweathersnow.cpp` and run off the clock:

* **snow cover** as described above (one step a second, three columns, and it only
  writes when a depth actually changed),
* **lightning strikes** (one every 20–70 seconds of a storm: one column scan, one
  burst of particles, one sound, and possibly one `applyDamage()`),
* and **rain puts the player's fire out**: `worldsurvival.cpp` clears `fire_ticks`
  while precipitation is falling on the player *and* the roofline check says they
  are outdoors — under a roof you keep burning, which is the point of a roof.
  Snow counts as well, since a flake is frozen water.

Rain cannot extinguish a *block* fire because the game has no fire block, and it
does not put out a lit furnace (a furnace has no burn state of its own to
interrupt). Both are recorded below.

## Settings

**Weather** (off/on, default on) turns the whole thing off. It requires the
day/night cycle to be on as well, because the weather is derived from that
clock; with the cycle off the clock does not advance and the weather would be
frozen, so the frame stays dry instead. The setting is appended last in the
settings table, so older save files keep loading.

## Cost

Per frame, only when it is precipitating: one column scan for the roofline (at
most 40 block lookups), the streak or flake blits, and — on a flash — one blend
over all 76,800 pixels. Nothing runs at all when it is clear, and nothing is
allocated.

Once a second while the weather is doing anything: one snow step — three columns,
at most `MaxGroundDrop + world height` lookups each (the world is 40 blocks tall,
so that is the real cap, not `MaxSkyScan`), and no write at all when no depth
changed.

Once every 20–70 seconds of a thunderstorm: one strike — one column scan of about
30 lookups, four particles and one sound.

## Tests

* `tests/weather_test.cc` — 3.2 million checks: the spell schedule over a hundred
game days, the ramp, the flash rhythm, the precipitation type (`rain` that turns
into `snow` over a cold column, a forced snow spell that stays snow), the state
names behind `/weather`, and the strike schedule and target distribution (every
offset in the radius reachable, both signs, no diagonal bias, consecutive strikes
in different places).
* `tests/snowcover_test.cc` — 31,000 checks: growth, the cap, melting, the
  half-block ceiling, the block ids (and the packing this header reads the world
  through) against `terrain.h`, what snow can rest on, the column picks, and
  `columnStep()` over synthetic columns — open ground, a drift, a hand-stacked
  drift, a roof, a torch in the cell, water, lava, a player in the air, and the
  bound on how much of a column a step may read.
* `tests/biomegen_test.cc` — the temperature field against the biome it describes:
  no cold column is a desert or savanna, the field uses its range, and 13% of
  columns are cold enough to snow.
* `tests/command_test.cc` — `/weather snow`, and that the console's weather names
  and `weather.h`'s agree state for state.

## Still open

* **Nothing here has been seen on real hardware.** The CX is not available, so the
  flakes, the drifts, the strike damage and the thunder bed are host-tested and
  typechecked only; `freebuff-preview` cannot drive a `.tns`.
* Slash **fire**: a strike does not set the ground alight and rain cannot
  extinguish a block, because there is no fire block (see ITEMS.md). Adding one is
  its own pass.
* A strike does not hit **mobs or animals**, only the player; entities do not tick
  against the weather.
* A lit **furnace** is not put out by rain, and snow does not sit on water as ice
  (there is no ice block).
* Snow cover is not generated with the terrain: a cold biome is white after its
  first storm, not when it is created. The cold forest's palette in
  `biomegen.cpp` is grass and dirt, and giving it a permanent cover would change
  what world generation produces, not just what weather adds.
