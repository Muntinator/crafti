# Day/night cycle and lighting

The clock, the sky (sun, moon with phases, stars) and the light that the world is
drawn with. Everything is deterministic, bounded for an original TI-Nspire CX
(ARM926, 16-block view distance), and split so that a day passing costs no
per-frame work at all.

## Pieces

| File | Role |
| --- | --- |
| `worldclock.h/.cpp` | The pure clock: time, sun/moon geometry, sky light level, sky colour, moon phases, the star field |
| `blocklight.h/.cpp` | The pure per-block light: depth falloff, shading curve, the emitter table, the light lattice |
| `chunklight.cpp` | The chunk's side of it: the emitter lattice, and the shade baked into each vertex |
| `chunk.cpp` | The sky map (highest sky-blocking block per column) and the hooks that invalidate both caches |
| `worldsky.cpp` | Paints the sky, the sun, the moon and the stars |
| `worldtask.cpp` | Feeds the clock real elapsed time and sets the per-frame day/night tint |
| `settingstask.cpp` | `Day/night cycle` (on/off) and `Day length` (10/20/40 min) |
| `tests/worldclock_test.cc`, `tests/blocklight_test.cc` | Host tests for both pure modules |

## The clock

A day is `WorldClock::TicksPerDay` (24000) ticks, with named times
(`TimeSunrise`, `TimeNoon`, `TimeSunset`, `TimeMidnight`, ...). The clock is
advanced from **real elapsed milliseconds**, not from simulation steps: the CX
logic loop runs at a few hertz against the desktop's ~30, so advancing one tick
per step would stretch a day from 20 minutes to hours, and differently on each
platform. `advanceElapsed(dt * simulation_tick_ms)` carries the sub-tick
remainder, so a day lasts `dayLengthSeconds()` whatever the frame rate is.

Time and day count are saved (save format v9); the day length is a setting.

## The sky

* `sunElevationDegrees()` = `90 * sin(360 * phase)`, so the sun is overhead at
  noon and under the world at midnight; the **moon is the antipode** of the sun
  (azimuth + 180°, elevation negated), so it rises as the sun sets.
* `skyColor()` ramps between night, dawn/dusk and day, and the weather darkens it
  on top (`worldsky.cpp`).
* 48 deterministic stars are drawn while the sun is below
  `NightElevation` (-6°), rotating with `skyRotationDegrees()`.
* The sun and the **moon are round pixel discs**, painted straight into the
  framebuffer rather than as geometry: a body is a small disc and a star is one
  pixel, which on the CX is free next to one textured triangle. Terrain drawn
  afterwards simply covers whatever is behind it, so no depth or matrix work is
  involved.
* The moon has **8 phases**, one per day (`moonPhase()`, the first night of a
  world is a full moon). The shadow is a second disc of the same size sliding
  across it, which is a few integer ops per pixel and gives the crescent, the
  half and the gibbous moon from one shape; the lit side swaps over between the
  waxing and the waning half of the month.
* A rising or setting sun gets a two-disc warm **halo**, so dawn and dusk read as
  dawn and dusk and not just as a dimmer noon.
* Where a body lands on screen comes from `WorldClock::celestialScreenOffset`, and
  the camera's own pitch from `WorldClock::cameraPitch`. The engine's `xr` is 0 on
  the horizon, 90 straight **down** and 270 straight up (see `crosshairRay` in
  `worldtask.cpp`), while the offset function takes the same angle wrapped into
  [-180, 180) with a positive value looking down. Those two spellings of the same
  angle are the trap: passing the raw `xr`, or `-xr`, mirrors the entire sky, and
  the symptom is the sun dropping out of view as the player looks **up**. The
  conversion is therefore one function rather than an expression at each of the
  two call sites, and `worldclock_test` pins both it and the composition with the
  offset function (a body on the horizon is below the centre when the camera
  looks up, above it when the camera looks down, and the noon sun is centred when
  the camera looks straight up).

## Lighting

The light a face is drawn with is a product of three terms:

```
shade = face direction  x  sky depth  x  emitters nearby
```

| Term | Where it is computed | When |
| --- | --- | --- |
| Face direction (top brightest, bottom darkest) | `blockrenderer.cpp::computeLighting` | mesh build (as before) |
| **Sky depth** (how far below the sky the block sits) | `Chunk::lightLevel` + `blocklight.cpp` | mesh build (**new**) |
| **Emitters** (glowstone, lava, torches, redstone torches) | `Chunk::rebuildLightField` | mesh build (**new**) |
| Time of day, weather and night vision | `nglSetGlobalShade` (one `worldtask.cpp` call per frame) | every frame (as before) |

Splitting it this way is the whole point: the **spatial** part never changes as
the sun moves, so it is baked into the vertex once, and the **time** part is a
single multiply the rasteriser already does for every vertex it draws. A day
passing re-meshes nothing and costs nothing.

**Sky depth.** `Chunk::columnSkyHeight()` finds the world Y of the highest
sky-blocking block of a column — the terrain surface from the same column
function the terrain and the villages use (so they always agree), raised by
anything this chunk put above it (a tree, a house, a ruin). `BlockLight::skyLevel`
then gives the block at that surface full light and takes one level off per block
below it, until it runs out 15 blocks down. A cave, a tunnel or the space under an
overhang is therefore dark at noon while the ground above it is bright.

**Emitters.** Everything that glows is planted into a chunk-local lattice of
levels and relaxed until nothing changes (each cell takes the brightest of its six
neighbours minus one). A torch therefore lights the blocks around it — which is
what makes a cave usable at night — and a chunk with no light in it stops after a
single pass, so the cost is proportional to what is actually lit.

**The curve.** Light 15 is full brightness and light 0 is `BlockLight::MinShade`
(88/256 ≈ 0.34), never black: full darkness would be unreadable on a reflective
LCD, and the day/night tint multiplies on top, so the floor keeps a cave
navigable at midnight. A fully lit face is shaded by its direction alone, which
means the surface of the world looks exactly as it did before per-block light
existed.

**Night vision** is the one thing that lifts the *time* term: the floor
`survival.h` keeps for the effect (`Survival::lightFloorFor(NightVision)`, 150 of
255) is handed to `WorldClock::skyLightFactor(floor)`, so a player under it keeps
the dusk they drank the potion in instead of sinking to the usual night floor.
Without the effect the floor is 0 and the factor is exactly what it was; the
floor's value and range are pinned in `survival_test`, its effect on the tint in
`worldclock_test`, and the three lines that connect them are in `render()`.

**Flat and graph worlds are untouched**: their sky is always open
(`BlockLight::SkyAlwaysOpen`) and the graph view keeps its fixed full-brightness
shade, so a plot stays readable at midnight.

**Night vision is not obtainable in game yet** (there is no brewing and no food
grants it), so the floor is wired but dormant: it is what an effect that already
exists is *for*, and it costs a comparison when the effect is not active.

### Cost

Nothing here runs per frame. Per chunk:

* the sky map is one terrain sample per column (64 per chunk), once, the first
  time the chunk is meshed — and a single column is patched when a block in it
  changes,
* the emitter lattice is one pass over the chunk's 512 cells plus (only where a
  light is in or next to the chunk) a relaxation of at most 15 passes,
* per vertex, two array reads and an integer multiply.

Measured on the host by `tests/blocklight_test.cc`, which rebuilds the lattice
500 times and prints the cost: **around 60-90 µs** for a chunk with a torch in it
and **about 5 µs** for a chunk with no light in or next to it. The CX is roughly a
hundred times slower, so a lit chunk pays about a millisecond of relaxation when
it is rebuilt, and an unlit one about 50 µs — against the mesh rebuild that
happens at the same moment and costs orders of magnitude more. Nothing of it
happens per frame.

Both caches are dropped when the chunk is generated or loaded and patched when a
block changes — a block change already triggers a mesh rebuild, so there is no
extra rebuild anywhere.

## Save compatibility

This feature adds **no save fields of its own**: the light is a function of the
blocks and the world seed, so it is rebuilt on load, and the clock's two fields
(`time`, `days`) are what save format v9 already carried. Later passes took the
format further (v10 item wear, armour and chests; v11 the play mode; v12 the
respawn bed) without touching any of this, so a v9 world and a v12 one are lit the
same way. The one visible consequence is that terrain generated before the biome
work (Stage 2) may be a few blocks off the height the sky map predicts, which
shifts the light a little in those old chunks.

## Tests

```
make -C tests
./tests/build/worldclock_test    # clock, sun/moon geometry, phases, discs, stars
./tests/build/blocklight_test    # falloff, curve, emitter ids, light lattice
```

`worldclock_test` pins the time arithmetic, the sun/moon geometry, the sky light
level and its floor, the sky colour ramp, the celestial projection **and the
camera-pitch convention the sky is placed with**, the star field, the wall clock,
elapsed-time advancement and the sky rotation, plus the
moon: one phase per day, a dark new moon, a whole full moon, a lit area that grows
and shrinks symmetrically, and the two halves of the month as mirror images.

`blocklight_test` pins the depth falloff (never brighter as you go deeper, full
light at the surface, nothing 15 blocks down), the shading curve (monotone,
bounded, exactly full at level 15), the emitter table against the real `BLOCK_*`
ids from `terrain.h` (and that nothing else glows), and the lattice (index layout,
distance falloff from one or two sources, clipping, clearing, the null field).

## Known gaps

* The emitter lattice is **chunk-local**: a light reaches one block across a chunk
  border (the border cell is seeded from the block just outside it), but not
  further. With an 8-block chunk and a 16-block view distance that is not visible.
* The sky map ignores blocks in *other* chunks of the same column, so a canopy two
  chunks up costs a few levels of light rather than darkening the block below it.
* Only four block ids glow (glowstone, lava, torch, redstone torch). A lit furnace
  and a redstone lamp are not in the table yet, and entities are not lit by this
  at all — they are drawn by their own renderer.
* **Nothing here has been seen on a CX.** No original hardware is available and
  `freebuff-preview` cannot drive a `.tns`, so the phases, the twilight halo and
  the cave darkening have only been exercised on the host and in the typechecked
  build.
