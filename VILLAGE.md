# Villages and villagers

Procedural villages and their inhabitants, added on top of the terrain, ore and
livestock systems. Everything here is deterministic from the world seed, uses
only blocks and audio the game already had, and is bounded so it stays
affordable on an original TI-Nspire CX (ARM926, 16-block view distance).

## Pieces

| File | Role |
| --- | --- |
| `villagegen.h/.cpp` | Dependency-free village layout + per-chunk emission + plan registry |
| `chunk.cpp` | Site search (flat, dry ground), per-chunk emission, plan cache, `registerVillagesNearColumn` |
| `villagerentity.h/.cpp` | Villager AI, schedules, boundaries, trading, spawning, hibernation |
| `settingstask.cpp` | `Villages` frequency setting (Off / Rare / Normal / Common) |
| `tests/villagegen_test.cc` | Host tests for layout, determinism, emission, registry |

## Generation model

The world is cut into cells of `Village::CellBlocks` (48) blocks. A cell either
hosts one village or nothing, decided by `Village::cellHasVillage(seed, cell)`
with the frequency weights `Off 0% / Rare 25% / Normal 60% / Common 90%`.

Inside a cell, up to `Village::CandidateCount` (4) origins are tried (cell
centre ± `CellJitter` = 6). A candidate is accepted when

* the sampled terrain height at the origin is within
  `[MinGroundY, MaxGroundY]` = `[14, 33]` (above sea level, room for a roof), and
* the terrain over the 3×3 grid at ±`Radius` (16) blocks is flat within
  `FlatnessTolerance` = 3 blocks.

So villages only appear on reasonably flat, dry ground. Because a village spans
`2*Radius` and the origin only jitters by 6, **two occupied neighbouring cells
can never overlap** — that is asserted by the host tests.

Overlap with terrain is otherwise limited to trees: a chunk generated *after* a
neighbouring village can still drop a canopy a few blocks over its edge, the same
way trees already overhang chunk borders. Everything inside the village's own
chunk is written by the village, so buildings are never half-buried.

Everything is a pure function of `(world seed, cell)`, so:

* chunks can stream in any order and still agree,
* re-resolving a cell later (after loading a save) reproduces the same village.

`Village::emitChunk` clips against the chunk being generated and returns early
unless the village could overlap it, so each chunk writes only its own slice
exactly once. `Chunk::generateVillages()` calls it after terrain and ores, so
village blocks replace terrain (and trees) where they sit.

The result of the search is cached per cell (`chunk.cpp`, 8 entries) and
published through `Village::registerPlan`, which is how the villager spawner
learns where the *real* villages are.

### Layout

Per cell: a cross of `BLOCK_DIRT` roads (2 wide, `Radius` long), a 9×9
cobblestone plaza, a cobblestone well (water centre, wood posts, plank roof),
four wood lamp posts with `BLOCK_GLOWSTONE`, and broken low walls on the east
and west edges (`BLOCK_WALL`, every 6th block skipped).

Houses use four non-overlapping slots per quadrant (7×7 or 5×5 houses), 4 to 7
of them per village, with a cobblestone foundation `FoundationDepth` (3) deep,
plank floor/walls, wood corner posts, glass windows, two-high `BLOCK_DOOR` with
side data, an overhanging plank roof, and a crafting table + furnace in the
larger ones. Each village also gets two farms (wall fence, central water trench,
`BLOCK_WHEAT` at growth 5/7), one shed (crafting table, bookshelf, furnace) and
a pumpkin patch plus flowers in the fourth quadrant. Plank colour is one of the
three vanilla variants, chosen from the layout seed.

`houseCentre` / `farmCentre` / `shedCentre` expose a structure's centre in world
block coordinates. They recompute the same layout from the plan seed, so they
always agree with the emitted blocks (the host tests assert a plank floor at
every house centre, the water trench at every farm centre and cobblestone at the
shed centre). Villagers use them to walk to real destinations.

## Frequency setting

`Settings → Villages` maps onto `Village::setFrequency`. The value is stored in
the settings block of the save file like every other setting. Villages are baked
into chunk data as blocks, so changing the setting only affects chunks generated
afterwards; already loaded terrain keeps the village it was generated with.

## Villagers

Villagers are spawned per village, up to `Village::villagerBudget` = `1 +
houses/2`, capped at 4 per village and by a global `villagerEntityLimit()` of 8
on the CX (20 on desktop). Their profession (Farmer / Blacksmith / Librarian) is
`Village::villagerProfession(plan, index)`.

* **Movement / boundaries** – they walk to a target point and never stray
  further than `Village::Radius` from their village centre; outside that radius
  they always steer back. Collision reuses the axis-separated AABB sweep used by
  the other mobs.
* **Schedule** – a shared tick counter alternates a 1600-tick working phase and
  an 800-tick resting phase (there is no sky in this build, so the phase comes
  from that counter). While working, a farmer walks to one of the two farms, a
  blacksmith to the shed and a librarian to a house; at night everyone gathers at
  the doorstep of "their" house (assigned by spawn slot, so the crowd spreads out
  instead of piling onto the well). Destinations are the real structure centres,
  approached from the village side so nobody stands in a wall or in the farm's
  water trench, and the arrival distance is fanned out per villager.
* **Activation / hibernation** – beyond `villagerActivationDistanceBlocks()`
  (24) they are not updated at all: no AI, physics or sounds. Beyond
  `villagerDespawnDistanceBlocks()` (40 on the CX, 64 on desktop) they are
  removed, and they re-spawn from the village plan when the player comes back.
* **Sounds** – idle chatter through `GameAudio::mobSound(MobVillager, …)` on the
  `Mobs` volume slider, hurt sounds on melee, and the villager "yes" cue on a
  completed trade.
* **Trading** – right-click/activate while looking at a villager:
  Farmer 1× wheat seeds → 1× apple, Blacksmith 2× coal → 2× arrows,
  Librarian 3× wheat seeds → 1× bread. A short cooldown stops repeats. A trade
  that cannot be paid for or is on cooldown consumes the interaction.

  Traded item ids must stay below 128: `getBLOCKDATA()` masks the id to 7 bits
  because bit 15 doubles as the redstone power flag, so larger ids (iron ingot
  is 145) would be handed over as a different item. `villagerentity.cpp`
  `static_assert`s the ids it uses.
* **Combat** – villagers can be punched (they flee); they do not fight back.

They render with the existing humanoid model and the shared 64×64 skin plus a
per-profession tint and an oversized nose, so no new sprite asset is needed and
the skin data still exists only once in the binary.

## Saving

Nothing village-specific is added to the save file:

* the village blocks are normal chunk data,
* the world seed is already stored, so the same villages regenerate,
* villagers are transient like the other mobs.

After loading, chunks come from the file and never run generation, so the plan
registry would be empty. `registerVillagesNearColumn` re-runs the same site
search for the cells around the player the first time the villager spawner finds
no plan nearby, which repopulates the registry without touching the save format.

## Tests

```
make -C tests            # all host tests
make -C tests build/villagegen_test && ./tests/build/villagegen_test
```

`villagegen_test` covers seed mixing, frequency gating and ordering, plan
determinism, candidate bounds, non-overlap of neighbouring villages, emission
staying inside the requested chunk, order-independent emission, that all the
expected structures are present and bounded by `MaxHeightAboveGround` /
`FoundationDepth`, that the house/farm/shed centres line up with the blocks
those structures emit, house counts, villager budgets and professions, and the
plan registry.

## On-device status

The CX build (`make`) compiles and links, and the host tests pass. Villages and
villagers have **not** been verified on real hardware: visuals, frame cost with
the villager cap, and the audible result of the idle sounds need a calculator
and an audio pack (`crafti.audp`) copied to `/documents/ndless/`.
