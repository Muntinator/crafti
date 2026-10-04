# Asset loading architecture

How Muntcraft loads, references and renders textures, sounds and models today,
with the file paths, formats and API calls each one goes through.

This is the baseline for the vanilla-asset port. It describes what the code
*does*, verified against the tree; where it disagrees with
`VANILLA_ASSETS_PLAN.md`, this document is the current one and the differences
are called out in §5.

---

## 0. The shape of the system

There is no asset pipeline at runtime and no asset registry. The three asset
classes are handled in three completely different ways, and that difference is
the single most important thing to know before porting:

| class | when it is loaded | how it is referenced | dynamic? |
| --- | --- | --- | --- |
| textures | **build time**, embedded in the `.tns` | compile-time C arrays + atlas tables | no |
| sounds | **runtime**, one pack file | numeric `Sound::Id` enum, generated | one file open |
| models | **build time**, as C tables | `Mob::MobModel` struct literals | no |

Textures and models are *compiled into the binary*. Sounds are the only assets
read from a filesystem at runtime.

---

## 1. Textures

### 1.1 The on-disk form

nGL's `TEXTURE` (`nGL/gl.h:74`) is a flat RGB565 bitmap:

```cpp
struct TEXTURE {
    uint16_t width; uint16_t height;
    bool has_transparency; COLOR transparent_color;
    COLOR *bitmap;          // COLOR == uint16_t
};
```

So every texture is 16-bit-per-pixel, and transparency is a single
colour-key (`transparent_color`), not per-pixel alpha. This is the hardest
constraint on any vanilla port: vanilla PNGs are RGBA, and the converters
collapse alpha to "transparent below a threshold, opaque above it" — the
`a < 16` test at `nGL/texturetools.cpp:143` (PNG via lodepng) and `:211` (PAM).

### 1.2 Generated headers

Each texture becomes a header in `textures/` holding two symbols — the pixel
array and the `TEXTURE` literal:

```c
// textures/terrain.h  (header line is generated too)
static uint16_t terrain_data[] = { 0x9cd3, 0x94b2, ... };
static TEXTURE terrain{
.width = 512, .height = 512,
.has_transparency = true, .transparent_color = 0,
.bitmap = terrain_data };
```

Both are `static`, so each header is single-translation-unit; it is `#include`d
by the one module that owns that texture.

### 1.3 The pipeline

`textures/Makefile` drives five generators plus one generic rule. All of them
live in `tools/textures/` and share `pngio.py` (a hand-rolled PNG reader —
there is no PIL on the build host).

| generator | outputs | source | contract |
| --- | --- | --- | --- |
| `gen_block_textures.py` | `terrain3.h` **+** `terrain3.png` | `textures/block/*.png` | composes the 16×16 block atlas |
| `gen_item_textures.py` | `items_texture.h` **+** `items_texture.png` | `textures/item/*.png` | atlas laid out by the `ItemTexture` enum |
| `gen_gui_textures.py` | **26** headers: 19 `PANELS` + `title_logo` + `armor_slots` + `font_bmp{,_wide}` + `font_dat{,_wide}` + `title_backdrop` + `world_icon{,_overlay}` | `textures/gui/**`, `font/`, `particle/`, `item/empty_armor_slot_*.png` | vanilla 1.17.1 GUI sheets cut into blittable pieces |
| `gen_entity_textures.py` | **11** skins (`creeper`, `cow`, `pig`, `sheep`, `chicken`, `horse`, `villager`, `wolf`, `mooshroom`, `donkey`, `steve`) | `textures/entity/*` | official mob skins |
| `gen_loading_textures.py` | `loading.h` + `loading.png` | `textures/gui/options_background.png`, `gui/title/minecraft.png` | the 320×240 boot frame: the vanilla dirt tile behind the vanilla wordmark |
| `%.h: %.png` / `%.h: %.bmp` → `ConvertImg --format=ngl` | `terrain.h`, `terrain2.h` | the game's own art | one-to-one conversion |

`loading.h` is the one header that is a whole 320×240 framebuffer rather than a
sprite: `main.cpp` hands `loading.bitmap` to nGL before any task exists, and on
a 4-bit greyscale LCD runs `greyscaleTexture()` over it in place. It used to be
a hand-painted splash of our own, converted by `ConvertImg`; it is now vanilla's
own loading screen, composed by `gen_loading_textures.py` from the same two GUI
files the menu already cuts up — `options_background.png` tiled, with the two ink
runs of `title/minecraft.png` joined and placed at `width / 2 - logo_w / 2`,
`height / 4`. The generator emits the `.h` itself rather than leaving it to the
`%.h: %.png` rule, because `ConvertImg` is not present on the build host and the
frame has to stay opaque (alpha 255 everywhere) to stay a framebuffer.

`textures/Makefile`'s `OBJS` lists the **40** headers the engine embeds; the other
six files in `textures/` are `items.h` (the hand-written `ItemTexture` enum),
`selection.h`, `language_icon.h`, `accessibility_icon.h`, and `world_icon.h` /
`world_icon_overlay.h`, which the world-select screen `#include`s but which the
Makefile only gets as a side effect of the GUI rule.

`blockselection.h` was the 41st until G2: the block outline is now a textureless
`glBegin(GL_LINES)` wireframe in `worldtask.cpp`, because vanilla draws it with
`DrawMode.LINES` over a POSITION-only vertex format and binds no texture at all.
There is no sheet to convert, so the `.h`, `.png` and `.kra` are all deleted.

Note the block and item generators each emit **both** a header and a PNG. The
PNG exists so the desktop build can load the same art through the runtime path
(§1.5) and so the arrangement can be eyeballed.

### 1.4 The atlas tables — the real ABI

The atlas is a fixed 16×16 grid of 16-pixel tiles. `terrainInit()`
(`terrain.cpp:163`) builds:

```cpp
extern TerrainAtlasEntry terrain_atlas[16][16];                                  // terrain.h:178
extern TerrainAtlasEntry block_textures[BLOCK_NORMAL_LAST+1][BLOCK_SIDE_LAST+1]; // terrain.h:177
extern TerrainQuadEntry    quad_block_textures[...][...];                        // terrain.h:206
```

`TerrainAtlasEntry` (`terrain.h:171`) holds **two** UV rects — `.current` for
world geometry, `.resized` for GUI/inventory blits that must not scale with the
sheet:

```cpp
struct TerrainAtlasEntry {
    TextureAtlasEntry current;  // blocks
    TextureAtlasEntry resized;  // GUI
};
```

Which tile belongs to which block is decided by `texture_atlas[y][x]`,a `BLOCK_TEXTURE` table carrying a `sides` bitmask; the loop at `terrain.cpp:218`
copies each tile into every side that bitmask names. Grass bottom is then
patched to share dirt's (`terrain.cpp:244`).

**Consequence for the port:** the atlas grid is an ABI. Renderers hardcode
their tile by coordinate — `terrain_atlas[x][y]` appears in `torchrenderer.cpp`,
`cakerenderer.cpp`, `bedrenderer.cpp` and others — so coordinates may be
**extended, never reshuffled**. Adding a block means adding a *file* and a *tile
at the end of the grid*, not renumbering.

Items use the same idea with the enum as the spec (`textures/items.h`):

> Each item is a `uint8_t` index… the tile an index names lives at
> `(x, y) = (index % 16, index / 16)`. This enum *is* the layout.

`itemicons.cpp` samples at that coordinate, with a `switch` for the exceptions.

### 1.5 Runtime loading — and the only filesystem path

`loadTextureFromFile()` (`nGL/texturetools.cpp:162`) is the one function that
reads an image off disk. It sniffs magic bytes:

- **PNG** (`\x89PNG…`) → decoded by `lodepng` (vendored, `nGL/lodepng.h`), then
  RGBA→RGB565.
- **P7 (PAM)** → `loadTextureFromFile_P7()`, requires `DEPTH 4`, `MAXVAL 255`,
  `TUPLTYPE RGB_ALPHA`.
- **P6 (PPM)** → binary RGB path.

It is called from exactly one place, `terrainInit()` (`terrain.cpp:163`):

```cpp
terrain_current = loadTextureFromFile(texture_path);
if(!terrain_current) {
    terrain_current = &tex_terrain3;   // embedded fallback
    if(!terrain_current) terrain_current = &terrain2;
}
else puts("External texture loaded!");
```

and the path is chosen per platform in `main.cpp:57-62`:

```cpp
#ifndef _TINSPIRE
    terrainInit("textures/terrain3.png");        // desktop: the repo tree
#else
    terrainInit("/documents/ndless/crafti.ppm.tns");  // calculator
#endif
```

Two things follow. First, **the calculator's runtime texture path is a PAM/PPM
file that this project never ships** — there is no `crafti.ppm.tns` in the
repo, so on the CX this always misses and falls back to the embedded
`terrain3`. It is a real extension point, currently unused. Second, when an
external sheet *is* loaded it must be 16×16 tiles or the arithmetic at
`terrain.cpp:180-182` (which just divides width and height by 16) silently
produces wrong UVs.

`terrainInit()` also does runtime tinting of what vanilla tints per-biome
(`terrain.cpp:188-206`): grass tops and leaves ship grey and are multiplied by
the default biome colours (`0x91bd59`, `0x77ab2f`), plus the redstone dust and
switch states it composes by drawing tiles onto each other.

### 1.6 Rendering

Chunks bind `terrain_current` / `terrain_quad` (`chunk.cpp:547-550`).The `terrain_quad` sheet is a 4×-duplicated copy of eight common tiles
(`terrain.cpp:254`) that collapses 8 triangles into 2; `TerrainQuadEntry`
(`terrain.h:193`) additionally carries `colors[5]`, the pre-shaded average
colours used by "fast mode".

---

## 2. Sounds

The only runtime-loaded asset class, and the only one with a binary container
of this project's own design.

### 2.1 The pack: `crafti.audp`

Built by `tools/audio/build_audio_pack.py` (or
`tools/audio/build_licensed_pack.py` for the current, licensed contents). Format
is defined by `audio_pack.h` and validated in `GameAudioPack::tryOpen()`
(`audio_pack.cpp:101-179`, checks at `:129-137` and `:158-165`):

```
offset  size  field
0       4     magic "AUD1"          (PackMagic, audio_pack.cpp:10)
4       2     version (must equal PackVersion = 1)
6       2     sample rate (the builder's default is 8000)
8       2     flags (reserved; the writer emits 0, the reader ignores it)
10      2     sound count (1..MaxSounds = 2048)
12      4     index offset (must equal HeaderBytes = 24)
16      4     data offset (must equal index_offset + sounds*EntryBytes)
20      4     data size
24      16×N  Entry table
...     M     8-bit unsigned mono PCM
```

Note the header is **24 bytes** (`HeaderBytes`, `audio_pack.cpp:13`) and that
every count and offset field is little-endian; `rate` and `sounds` are 16-bit,
not 32-bit.

Each `Entry` (`audio_pack.h:39`) is `{offset, length, category, flags, gain,
reserved}` — 16 bytes, `EntryBytes`. Flags are `FlagLoop`, `FlagMusic`,
`FlagStream`. Category maps 1:1 onto `GameAudio::Category` (UI, Music,
Ambience, Weather, Blocks, Footsteps, Mobs, Player, Combat).

The reader validates every one of those invariants and refuses the pack with
`"unsupported audio pack"` rather than reading past the end.

### 2.2 The id enum is generated from the pack

`audio_sounds.h` is emitted by the builder, numbered in exactly pack order:

```cpp
namespace GameAudio { namespace Sound { enum Id {
    None = 0,
    AmbientCaveCave1 = 1,   // ambient/cave/cave1.ogg (Ambience, loop)
    ...
    MusicTrack8 = 169,      // music/track8.ogg (Music, loop)
    Count = 169
}; } }
```

Current pack: **169 sounds, 6,300,356 bytes**. Because the header comment says
"do not edit by hand" and the engine uses these as **compile-time constants**,
a name the pack lacks is a *build failure*, not silence. The builder parses
`audio_sounds.h` (`engine_names()`, `tools/audio/build_licensed_pack.py:254`)
and only emits ids that exist, which is why regenerating the header and the
pack together is mandatory.

Ids are derived from the path: `mob/pig/say1.ogg` → `MobPigSay1`. `--sounds` and
`--music` are **separate roots**, so a pack path is relative to whichever root
it came from — a stray `sounds/` prefix silently breaks matching.

### 2.3 Runtime

`GameAudioPack::open()` (`audio_pack.cpp:181`) searches, in order:

| platform | candidates |
| --- | --- |
| calculator | `/documents/ndless/crafti.audp`, `/documents/crafti.audp` |
| desktop | `crafti.audp`, `../crafti.audp` |

Two access strategies, both allocation-free:

- **cache** — short repeated sounds into 8 slots × 9216 bytes with refcounts
  (`CacheSlots`, `CacheSlotBytes`, `audio_pack.h:50-51`);
- **stream** — music/ambience/weather through 4 rings × 2048 frames
  (`StreamSlots`, `StreamFrames`, `audio_pack.h:56-57`), where `streamPump()`
  runs on the game loop and `streamRead()` on the audio clock, so no file IO
  happens mid-sample.

Above that sits `audio_manager.cpp`: an 8-voice mixer, per-category volumes, and
a **semantic cue API** — `footstep(Material)`, `digBlock`, `placeBlock`,
`mobSound(Mob, hurt)`, `uiClick`, `playerFall`, `weatherThunder`, `ambienceCue`,
`setMusicDesired(scene)`, `updateMusic(ms)`. Gameplay never names a sound id;
it names a cue, and the cue picks an id. Each cue falls back to a procedural tone
when no pack is installed.

### 2.4 Licensing

`crafti.audp` is **gitignored** (`.gitignore:6`) and is a build product.
`AUDIO_LICENSES.md` and `tools/audio/licensed_pack_mapping.tsv` (170 lines = a
header plus 169 rows) record per-sound provenance: **74** rows name
`(generated placeholder)`, leaving **95** real clips — OpenGameArt's `sfx100`
set, CC0 chiptunes, and the vanilla music titles. Minecraft's own assets are
proprietary and are never downloaded.

---

## 3. Models

**There are no model files.** This is the single biggest difference from vanilla
and the reason §4 is mostly a transcription exercise.

Minecraft Java ships no entity models in its assets — the box models are
hardcoded in client model classes. So there is nothing to load. `mobmodel.h`
transcribes them into data:

```cpp
namespace Mob {
  struct MobBox  { int8_t u,v; int8_t w,h,d; int8_t ox,oy,oz; int8_t grow; uint8_t mirror; };  // :31
  enum class Pose : uint8_t { Static, Head, LegFrontLeft, ... };                             // :54
  struct MobPart { int8_t px,py,pz; int8_t rot_x; uint8_t first_box, box_count; Pose pose; };  // :64
  struct MobModel{ uint8_t tex_width, tex_height, part_count; const MobPart *parts;           // :73
                   uint8_t box_count; const MobBox *boxes; };
  void draw(const MobModel &model, GLFix scale, GLFix leg_swing, GLFix head_yaw);            // :118
}
```

Coordinates are vanilla model units (1/16 block), **y grows downward**, feet at
`y = 24`. Texture origins are vanilla texels, so a box lines up with the
official skin pixel for pixel.

Where the tables live:

| file | contents |
| --- | --- |
| `livestockspecies.cpp/.h` | cow, pig, sheep, chicken, horse, and friends |
| `villagerentity.cpp` | villager |
| `creeperentity.cpp` | creeper |
| `playermodel.cpp/.h` | the player ("Steve"), drawn inside GUI screens |

The skin is a normal `TEXTURE` bound before `Mob::draw()`:
`glBindTexture(&creeper_tex)` (`creeperentity.cpp:470`),
`glBindTexture(&villager_tex)` (`villagerentity.cpp:701`),
`glBindTexture(&steve_tex)` (`playermodel.cpp:135`).

### 3.1 The frame flip — a real trap

nGL draws y-up; the tables are vanilla's y-down. `Mob::draw()` negates the box
coordinates, but flipping one axis is a *mirror*, and under a mirror a rotation
`R` becomes `M·R·M` — the same rotation by the **opposite** angle. So table
angles must be negated on the way in, which is why this exists as a named
function both the renderer and the host test go through:

```cpp
// mobmodel.h:101
inline GLFix angleInDrawnFrame(GLFix vanilla_degrees) { return -vanilla_degrees; }
```

It applies to angles that come *out of a table* only. `head_yaw` comes from the
caller already in the drawn frame and is used as given — `playermodel.cpp`
rotates the whole model and then passes the same yaw again, reproducing
vanilla's double-turn of the head.

`tests/livestock_test.cc::test_drawn_models_are_the_right_way_up()` computes
where each torso actually lands through this same call; removing the negation
produces 10 failures across cow/pig/sheep/horse. Any new mob must be added to
that test.

---

## 4. What this means for a vanilla port

- **Textures** are the cheapest to port: drop a PNG at the vanilla path under
  `textures/`, add or extend a tile, regenerate. The blockers are RGB565
  (16-bit, colour-keyed, no real alpha), the fixed 16×16 atlas as an ABI, and
  vanilla's 3-D item models having no 2-D equivalent here.
- **Sounds** are the best-isolated: nothing about the engine changes to add a
  sound, but `audio_sounds.h` and the pack must be regenerated *together* or the
  build breaks. Licensing is already solved for 95 of 169; the other 74 are
  placeholders and are the real remaining work.
- **Models** cannot be ported as files at all — they are transcribed tables. A
  new mob is a hand-written `MobModel`, a skin PNG, and a test in
  `livestock_test.cc`. There is no loader to write.
- **The GUI is already vanilla 1.17.1**; see `GUI_VANILLA_PORT.md`.

## 5. Where this disagrees with `VANILLA_ASSETS_PLAN.md`

`VANILLA_ASSETS_PLAN.md` §1.3 is stale. It describes the pack as
"~15.0 MB, **1119 sounds** (1103 effects + 16 music)" built from
`sounds_trimmed.zip` and `minecraft-*-music.zip`. That was true before the
licensed rebuild (v1.13.0, commit `8037da4`). The pack in the tree today is
**6,300,356 bytes / 169 sounds**, built by `build_licensed_pack.py` from
OpenGameArt sources. Its §3.1 strategy text proposes a `sounds/` tree fed by
`sounds.json`; that tree was never created — the current provenance record is
`tools/audio/licensed_pack_mapping.tsv`. Everything else in that document's
§1.1/§1.2/§1.4 still matches the code.
