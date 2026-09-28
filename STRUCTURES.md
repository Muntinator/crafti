# Structures: dungeons, ruins and desert temples

Rare procedurally placed structures, generated after the terrain, the ores and the
villages. Everything is deterministic from the world seed, uses only blocks and
items the game already had, and is bounded so it stays affordable on an original
TI-Nspire CX (ARM926, 16-block view distance).

## Pieces

| File | Role |
| --- | --- |
| `structuregen.h/.cpp` | Dependency-free cell grid, site rules, the three layouts and their loot tables |
| `chunk.cpp` | `generateStructures()` per-chunk emission, `structureChestAt()` |
| `cheststore.h/.cpp` | The container the chests hold their items in |
| `worlditems.cpp` | `ensureChestAt()`: creates a chest's container and seeds its loot |
| `tests/structuregen_test.cc` | Host tests for the grid, the layouts, emission, the ids and the loot |

## Generation model

The world is cut into cells of `Structures::CellBlocks` (128) blocks — a power of
two, so the hot path has no integer division. A cell hosts at most one structure,
decided by `Structures::cellHasStructure(seed, cell)`; **25%** of cells hold one.

Inside a cell, up to `Structures::CandidateCount` (2) origins are tried at the cell
centre ± `CellJitter` (24) blocks. Because the largest structure reaches
`MaxReach` (6) blocks and the jitter leaves 40 blocks of margin,
**two occupied neighbouring cells can never touch** — the host tests assert it.

A candidate is accepted depending on its kind, using the *same* column function the
terrain uses (`terrainColumn`), so the generator and the structure always agree on
where the ground is and what it is made of:

| Kind | Rarity | Site rule |
| --- | --- | --- |
| Dungeon | 45% of structures | Ground at least `DungeonMinGroundY` (18), so there is rock above the ceiling |
| Ruin | 35% | Ground in `[15, 30]`: dry land, above the beaches, below the hills |
| Temple | 20% | Dry land **and** a desert biome, with room for the steps under the ceiling |

A ruin and a temple also have to stand on ground that is flat to within
`surface_flatness_tolerance` (2) blocks over a 3x3 grid across their footprint,
otherwise a corner would hang over a cliff. That check needs the terrain
function, so it lives in `chunk.cpp::structureSiteIsFlat()` rather than in the
module — and because generation *and* the chest lookup both go through
`resolveStructurePlan()`, they can never disagree about whether a site was
accepted. A dungeon is buried, so its surface shape does not matter.

Everything is a pure function of `(world seed, cell)`, so chunks can stream in any
order, alone, with no cross-chunk bookkeeping and no pending-change queue.
`Structures::emitChunk` clips to the chunk it was asked for and writes only its own
slice of the layout, so each block has exactly one writer. The pass runs **after**
`generateVillages()`, so where the two overlap the structure wins instead of being
partly overwritten.

### Layouts

The palette is limited by what the game actually has: there is no mossy
cobblestone, no stone bricks and no sandstone, so nothing pretends to be.

* **Dungeon** — a 9×9×3 cobblestone room buried 11 blocks below the surface, with
  a cobblestone floor and ceiling, a single `BLOCK_GLOWSTONE` in the middle of the
  ceiling, `BLOCK_SPIDERWEB` in the corners, and one chest in a far corner. The
  shell is solid on all four sides of all three open layers, so it can only be
  found by digging or by a cave breaking into it.
* **Ruin** — a 7×7 hut that has already fallen apart: cobblestone walls three high
  with a doorway on the `-z` side and gaps where they collapsed, a plank-dark roof
  with holes in it, rubble on the floor, and a chest in a corner. One ruin in four
  (by layout hash) has a second chest in the opposite corner.
* **Temple** — a four-layer step pyramid of `BLOCK_SAND` (9×9, 7×7, 5×5, 3×3) over
  a cobblestone shell holding a 3×3×2 chamber, with the chest on the chamber floor
  and the way in blocked by the steps, exactly like the real thing.

## Chests

A structure chest is an ordinary `BLOCK_CHEST`: the world only stores the block,
and the contents live in `cheststore.h`, keyed by position. What makes a structure
chest different is where its items come from.

`Chunk::generate()` writes the chest block; it deliberately does **not** create a
container or a save record. The first time a chest is touched —
`InventoryTask::openChest()` (opened) or `breakChestAt()` (broken, including by a
TNT blast) — `ensureChestAt()` asks `structureChestAt()` whether the position
belongs to a structure. If it does, the loot is generated from that structure's
plan (`Structures::chestLoot`) and put into the container.

So an untouched chest costs no memory and no save space, and a chest that is only
ever broken still spills its loot. The loot is a pure function of the plan seed,
which means a chest holds the same things however the chunk was reached, and
however many times the world is saved and loaded. When two chests are placed side
by side they pair into a 54-slot double chest — a player chest next to a structure
chest keeps the structure's items in the half it owns.

| Table | Rolls | Character |
| --- | --- | --- |
| Dungeon | 3 stacks | coal, bread, apples, arrows, string, gunpowder, redstone, rotten flesh, bone meal, iron, gold, spider eyes, lapis, a little diamond, rarely a golden apple or an iron pickaxe |
| Ruin | 2 stacks | food, seeds, sticks, coal, paper, bone meal, a little iron and redstone, rarely a golden apple |
| Temple | 3 stacks | gold nuggets and ingots, iron, bone meal, arrows, lapis, glowstone dust, books, compass, clock, saddle, diamonds, golden apples |

## Verification

* `tests/structuregen_test.cc` — the cell density and the kind split, the candidate
  margin, site rejections (water, shallow rock, a temple outside a desert, a roof
  above the ceiling), plan purity, per-chunk emission (nothing outside the chunk,
  no block written twice, the union of all chunks equals the layout exactly), the
  three layouts block for block (a sealed dungeon, the ruin's doorway and holes,
  the temple's steps and chamber), and the loot (always an item, always a real
  atlas id, always reproducible, always within the stack limits). It also checks
  every block and item id against `terrain.h` and `textures/items.h`, since the
  module keeps numeric copies so it can be tested without the engine.
* `make -C tests` — all 14 host test binaries pass.
* `make -j2 NDLSSDK=… ZLIB_PREFIX=…` — the CX build compiles cleanly; `crafti.tns`
  is produced.

## Known gaps

* Nothing here has been seen on a CX: no original hardware is available, and
  `freebuff-preview` cannot drive a `.tns`. The layout, the chest screen and the
  loot have only been exercised on the host.
* No monster spawner in dungeons — the game has no spawner block, so a dungeon is
  currently a quiet room with a chest in it.
* Only three kinds, and only the blocks the game already has. Adding stone bricks,
  mossy cobblestone or sandstone would let the layouts match vanilla more closely.
