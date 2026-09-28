# World generation (Stage 2)

`biomegen.h` / `biomegen.cpp` own biomes, mountains, rivers, caves and ravines.
`chunk.cpp` owns the terrain itself and calls into the module. The split exists
so that all of the new generation is testable on a host:
`tests/biomegen_test.cc` runs 1.4 million checks in well under a second and needs
neither nGL nor the block texture tables.

## The contract

Everything in the module is a pure function of `(world seed, world block
coordinates)`. There is no state, no cache and no ordering requirement, which
buys three things:

* **deterministic** – the same seed always produces the same world,
* **order independent** – a chunk can be generated alone, in any order, and
  still matches its neighbours exactly across the border,
* **save compatible** – the generator writes ordinary blocks into the ordinary
  chunk grid, so nothing new has to be stored or migrated. Saves stay at
  version 9 and old worlds keep loading.

The one visible consequence for an existing save is the usual one: terrain that
was already generated keeps its old shape, and terrain generated after the
update sits next to it with a seam. New worlds are seamless.

## Height

`chunk.cpp::terrainColumn()` computes the unchanged four-octave Perlin base
(measured: world Y 8..27, centred just above sea level, so about 12% of the
world is at or below the water line) and hands it to `BiomeGen::columnAt()`,
which:

1. applies the **mountain lift** – up to +16 blocks, ramped from zero at the
   edge of a mountain so a mountain rises out of its surroundings instead of
   starting with a cliff,
2. carves any **river** – a channel up to 4 blocks deep, deepest in the middle,
3. clamps to the world envelope (`BiomeGen::MinHeight` .. `MaxHeight`, which
   `chunk.cpp` static_asserts against `World::HEIGHT * Chunk::SIZE - 3`),
4. picks the biome and the two surface blocks.

`terrainSurfaceHeight()` and the village site search both go through the same
function, so terrain generation, village placement and the mob spawners can
never disagree about where the ground is or what it is made of.

## Biomes

Four independent value-noise fields (temperature, humidity, mountainness,
rivers), each two octaves, all on power-of-two lattices – so the hot path has no
integer division at all.

| Biome | Surface | How it is picked |
| --- | --- | --- |
| Ocean | sand | height below sea level (12) |
| Beach | sand | height 12..13 |
| Desert | sand | hot, dry |
| Plains | grass over dirt | temperate, dry |
| Forest | grass over dirt, dense trees | temperate, wet (and "cold") |
| Savanna | grass over dirt, sparse trees | hot, wet |
| Mountains | bare stone above Y 24, grass below | mountain field above its level |

Measured land shares with the current thresholds:

```
Desert   14%    Plains   40%    Forest   25%    Savanna  10%    Mountains 9%
```

The thresholds are z-scores against the measured spread of an interpolated value
noise field (standard deviation ~220 of the 0..1023 range, not the ~295 of a
uniform one), which is why they look arbitrary. They were calibrated by running
the host test, not guessed.

**On the block palette:** this game has no ice, sandstone, gravel, clay or cactus
block id, so the biome set is limited to what the existing ids can express
honestly. There is deliberately no "snowy" biome: the cold band is a dense
forest, because what makes it cold is the temperature field, and snow is
something the weather *adds* to it (WEATHER.md) rather than something terrain
generation paints on. That keeps this module free of the snow block, and it means
a cold forest is white after its first storm rather than from the moment it is
created.

## Caves and ravines

Both are carved by `Chunk::carveUnderground()` after the terrain and the water
have been placed and **before** the ore veins and the villages. That ordering
matters: an ore vein never ends up hanging in mid-air, and a village building is
never hollowed out from underneath.

**Caves** intersect two independent 3D fields – a carve happens only where both
are simultaneously near their centre. The intersection of two smooth sheets is a
curve, which is what makes a tunnel instead of a blob. Measured: 9.1% of
underground blocks, and 99.8% of carved blocks have a carved neighbour, i.e. the
tunnels really are connected enough to walk through.

**Ravines** are one deterministic line segment per cell of a 256-block grid
about a third of which carry a ravine. The carve has a lens-shaped cross
section: full height on the centre line, a sliver at the rim. Measured: 0.8% of
columns, reaching from Y 5 up to 25.

**The surface is never broken.** Both passes stop one block below the surface
block of the column, so neither can open the sky, drain a lake or the ocean into
the caves, or leave a tree standing over a hole. The test asserts this for every
column and for every surface height in the valid range.

## Trees

Tree density is a property of the biome (`BiomeGen::treeDensityPercent`): 7% per
column in a forest, 2% in plains, 1% in savanna, none in the desert, on a beach
or on a mountain. `chunk.cpp` keeps a hard cap of ten per chunk on top, because a
canopy is a 7x7x10 template and the CX has to build the mesh for every one.

## Cost

Per chunk column (five chunks, 8x8x40 blocks), the new work is roughly:

* 64 biome samples (~8 value-noise lookups each), once per column,
* ~900 cave samples in the column strip (~1.5 fields of 16 lattice hashes each),
* 64 ravine lookups, each a cheap hashed cell test plus at most nine segment
  distance tests,
* at most ten tree templates in a forest,
* up to four structure cells tested per chunk, of which the three in four that
  hold nothing cost one hash and no terrain sample at all (see STRUCTURES.md).

That is a few milliseconds on a 150 MHz CX, and only for a chunk that is being
generated for the first time. Nothing here runs per frame.

## Tests

```
make -C tests            # everything, including biomegen_test
./tests/build/biomegen_test
```

`biomegen_test` pins: determinism across call order, seed sensitivity, that
every biome is reachable and none of them swallows the world, the surface
material per biome, that mountains actually lift the ground, that rivers are a
minority and step by at most one block per column, that the module adds no
walls between neighbouring columns, cave coverage and connectivity, and the
ravine invariants (never at the surface, always a fissure).

## Still open

* **Weather** is implemented, but separately: see WEATHER.md.
* **Rare structures** (dungeons, ruins and desert temples) are implemented on top
  of the existing villages: see STRUCTURES.md.
* A structure chest's contents are not written to the save file until the chest is
  first touched, because they are a pure function of the world seed.
* Sandstone/gravel/ice block ids do not exist, which caps how distinct the desert
  and the cold biomes can look; snow itself does exist now, but as weather cover
  rather than as generated terrain (WEATHER.md).
* `BiomeGen::temperatureAt()` / `isColdAt()` expose the temperature field so the
  weather can ask the same question the biome is chosen from; the threshold is
  `BiomeGen::ColdTemperature`, shared rather than copied.
* Nothing here has been checked on real hardware. The numbers above come from
  the host test; how the biomes read on the 320x240 screen of a CX is unknown.
