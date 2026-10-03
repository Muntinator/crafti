# Vanilla Minecraft assets: integration plan

Goal: every block, item, GUI element and audible cue in Muntcraft should come
from official Minecraft art/audio (Java Edition **1.17.1**, the version the
existing port targets), replacing the remaining placeholders and game-owned
images, without breaking the calculators hard constraints (no dynamic
allocation, RGB565 embeds, 8 kHz mono audio, ~750 KB `.tns` budget).

Much of this is already built — the GUI port is documented in
`GUI_VANILLA_PORT.md` and the pack build in `BUILD_TNS.md`. This plan records
how the pipeline works, what is *still* non-vanilla, and exactly which files
change to close the gap.

---

## 1. How assets are handled today

### 1.1 Textures — build time

The checked-in art lives under `textures/` in the **vanilla asset tree's own
layout**, one authentic file per vanilla name:

    textures/block/<name>.png          assets/minecraft/textures/block/<name>.png   (93 files)
    textures/item/<name>.png           assets/minecraft/textures/item/<name>.png    (172 files)
    textures/gui/*.png                 assets/minecraft/textures/gui/…              (icons.png, widgets.png,
                                          gui/container/, gui/title/, gui/title/background/)
    textures/font/ascii.png            assets/minecraft/textures/font/ascii.png
    textures/entity/<mob>/…            assets/minecraft/textures/entity/…           (11 entries)
    textures/particle/flame.png        assets/minecraft/textures/particle/flame.png

`textures/Makefile` plus the generators in `tools/textures/` (sharing
`tools/textures/pngio.py`) convert these into **RGB565 C headers** that are
embedded in the `.tns`:

| generator | outputs | contract |
| --- | --- | --- |
| `gen_block_textures.py` | `terrain3.h` + `terrain3.png` | block atlas: one 16x16 tile per block face in a 16x16 grid; **which file lands on which coordinate follows the block table in `terrain.cpp`** |
| `gen_item_textures.py` | `items_texture.h` + `items_texture.png` | item atlas: **the `ItemTexture` enum in `textures/items.h` is the spec**; enum value `i` must sit at `(i % 16, i / 16)` |
| `gen_gui_textures.py` | `icons.h`, `inventory.h`, `inv_selection.h`, `menu_button.h`, `menu_background.h`, `checkbox.h`, `slider_handle.h`, `title_logo.h`, `edition.h`, `title_backdrop.h`, `inventory2.h`, `crafting_table.h`, `furnace.h`, `chest_top.h`, `chest_player.h`, `creative_window.h`, `creative_tabs.h`, `creative_scroll.h`, `armor_slots.h`, `part_fire.h`, `font_bmp.h`, `font_dat.h`, `font_bmp_wide.h`, `font_dat_wide.h` | vanilla GUI sheets cut into the pieces the engine blits; the font atlas + metrics; the title panorama pre-rendered |
| `gen_entity_textures.py` | `creeper.h`, `cow.h`, `pig.h`, `sheep.h`, `chicken.h`, `horse.h`, `villager.h`, `wolf.h`, `mooshroom.h`, `donkey.h`, `steve.h` | official mob skins |

The remaining headers are the **game's own** images converted one-to-one with
`ConvertImg --format=ngl` (`%.h: %.png` rule): `loading.h` (boot splash),
`blockselection.h` (block outline), `terrain.h` + `terrain2.h` (pre-vanilla
512x512 sheets, still compiled in and included from `terrain.cpp`).

### 1.2 Textures — runtime

- nGL's `TEXTURE` is a flat RGB565 bitmap. The live block sheet is
  `terrain3.h`; root `terrain.h` exposes
  `TerrainAtlasEntry terrain_atlas[16][16]` (`nGL/texturetools.h`
  `TextureAtlasEntry`, `.current` for chunk geometry, `.resized` for
  inventory-scale blits) and `terrainInit(const char *texture_path)`
  (`terrain.cpp:163`) tints what vanilla tints at render time: grass tops and
  leaves ship grey and are turned green, water is tinted the biome blue.
- Renderers hardcode their tile by position (`terrain_atlas[x][y]` in
  `torchrenderer.cpp`, `cakerenderer.cpp`, `bedrenderer.cpp`, …), so the atlas
  grid is a fixed ABI: coordinates may be **extended, never reshuffled**.
- `itemicons.cpp` samples the item atlas at `(index % 16, index / 16)` of the
  `ItemTexture` enum value; a `switch` handles the exceptions.
- `menuui.cpp` blits the generated GUI headers and `font.cpp` renders the
  vanilla 8-pixel font (normal + wide/2x variant). GUI scale is
  `MenuUI::uiScale()` (1 on the CX's 320x240, 2 on the desktop's 640x480).

### 1.3 Sounds

- `tools/audio/build_audio_pack.py` builds `crafti.audp` + `audio_sounds.h`
  from extracted vanilla OGGs (`sounds_trimmed.zip` -> `extracted/`, vanilla
  paths like `block/amethyst/break1.ogg`) and music
  (`minecraft-essentials-music.zip`, `minecraft-vanilla-music.zip` -> `music/`).
  ~15.0 MB, **1119 sounds** (1103 effects + 16 music).
- Pack format (`audio_pack.h`): `AUD1` header, 16-byte entry table
  (`offset, length, category, flags, gain`), then 8-bit unsigned mono PCM at
  8 kHz. Flags: loop / music / stream. Category maps 1:1 to
  `GameAudio::Category` (UI, Music, Ambience, Weather, Blocks, Footsteps, Mobs,
  Player, Combat).
- `audio_sounds.h` is generated: `Sound::Id` numbered in exactly pack order, so
  the engine needs no runtime lookup.
- Runtime (`audio_pack.cpp` / `audio_manager.cpp`): 8 cache slots x 9 KB for
  short sounds, 4 stream rings x 2048 frames for music/ambience/weather, no
  dynamic allocation; an 8-voice mixer; and a **vanilla cue API** —
  `footstep(Material)`, `digBlock`, `placeBlock`, `mobSound(MobKind, hurt)`,
  `uiClick`, `playerHurt`, `playerAttack`, `playerFall`, `chestOpen/Close`,
  `doorOpen/Close`, `itemPickup`, `weatherThunder`, `ambienceCue`,
  `startMusic`, … Every cue plays the official sample when a pack is installed
  and falls back to a procedural tone when it is not.
- Deployment: `crafti.audp` next to the program, searched at
  `/documents/ndless/crafti.audp` and `/documents/crafti.audp` (desktop:
  `crafti.audp`, `../crafti.audp`). The pack is **gitignored** — the source
  assets are not redistributable and only the local archives
  (`sounds_trimmed.zip`, `minecraft-*-music.zip`) hold them.

### 1.4 UI

Already vanilla 1.17.1 (see `GUI_VANILLA_PORT.md` for the phase log): title,
pause, options, loading, death and creative screens on vanilla's widget grid
and coordinates; `tests/menuui_test.cc` pins geometry at both screen sizes.

---

## 2. What is still placeholder / non-vanilla

| # | Asset | Today | Target |
| --- | --- | --- | --- |
| G1 | Boot splash (`textures/loading.png`, `loading.h`, used by `main.cpp`) | game's own art | keep as Muntcraft branding (vanilla has no boot splash) *or* restyle on vanilla dirt + wordmark; decision item |
| G2 | Block outline (`textures/blockselection.png`, drawn by `worldtask.cpp`) | hand-drawn sheet | match vanilla's selection box look (thin translucent black wireframe, `getBlock` outline colour), keep the engine's quad-based drawing |
| G3 | `textures/selection.png/.kra` | hand-made, **not in the embedded set** (absent from `textures/Makefile` `OBJS`) | audit and delete if unreferenced |
| G4 | `textures/terrain.h` + `terrain2.h` (pre-vanilla 512x512 sheets) | compiled in, included from `terrain.cpp` | audit the includes, then drop from `OBJS` and delete — `terrain3` is the live atlas; frees two large embedded arrays |
| G5 | Blocks drawn from stand-in tiles | the block table in `terrain.cpp` maps only what exists in `textures/block/` | full audit table; missing faces get their vanilla PNG (see 3.1) |
| G6 | Animated textures (water/lava/fire/portal) | first frame only (`gen_block_textures.py` composes water; `part_fire.h` is the torch flame) | frame 0 is acceptable now; optional phase adds vanilla `.mcmeta` frame strips (see 3.3) |
| G7 | Items without a true vanilla icon | bed icon composed from `entity/bed/red.png` (vanilla has no 2D bed icon); compass/clock use frame 0 of 32/64 | keep the documented exceptions; animate compass/clock later if wanted |
| G8 | Sound events | cues hand-pick sample ids (`family[nextRandom() % 3]` tables in `audio_manager.cpp`); `sounds.json` semantics unused | generated event→variants tables (see 3.4) |
| G9 | Procedural fallback cues (`GameAudio::Event`) | used only when no pack is installed | keep — documented behaviour ("quiet rather than wrong, never silent"), not an asset |

---

## 3. Strategy

### 3.1 Source tree and pipeline (file structure)

Keep the one-rule pipeline: **authentic vanilla files under vanilla names,
generators only arrange them**. Extend the tree as needed:

    textures/                          # checked in (already established)
      block/ item/ gui/ font/ entity/ particle/
    sounds/                            # NEW: mirror assets/minecraft/
      sounds.json                      #   the vanilla event manifest (events -> files, weights)
      block/ ambient/ mob/ entity/ random/ step/ ui/ music/ …
      music/                           #   soundtrack tracks (menu1..4, nuance, calm, …)
    tools/textures/gen_*.py            # arrangement only (extend maps, never bake art)
    tools/audio/build_audio_pack.py    # + sounds.json parsing (3.4)

Build outputs are unchanged in shape: committed generated headers
(`textures/*.h`), plus the gitignored `crafti.audp`. The local zip archives
stay the provenance record for licensing; the extracted `sounds/` tree and the
pack remain uncommitted until licensing is resolved (same rule as
`BUILD_TNS.md` documents today).

### 3.2 Blocks (code-change areas)

Adding or replacing a block texture is a four-step, fully offline change:

1. Drop the authentic file at `textures/block/<name>.png`.
2. Map it in `tools/textures/gen_block_textures.py` (the explicit source table
   for non-block sources: chest model unwrap, bed unwrap, cake icon, water).
   Grey + runtime tint stays the rule for grass/leaves/water
   (`terrainInit()` in `terrain.cpp`).
3. Point the block's faces at the tile in the **block table in `terrain.cpp`**
   (the generator reads this table; a name with no source, or two names on one
   tile, must stay a hard error in the generator).
4. `make -C textures`, then the pixel-compare scenarios in `tools/pcsim/` and
   `make -C tests` (the enchanting/block tests pin tile coordinates).

Never move existing tiles: the `terrain_atlas[x][y]` callsites (bed, cake,
torch, door, fluid, lamp, leaves renderers) treat coordinates as an ABI.
New blocks take free cells or a deliberate, renderer-reviewed reshuffle.

### 3.3 Items (code-change areas)

The enum is the spec and is append-only:

1. Append the new value to `enum class ItemTexture : uint8_t` in
   `textures/items.h` (never renumber — item data stores the byte).
2. Drop `textures/item/<name>.png`; extend the source map in
   `gen_item_textures.py` (bed/compass/clock style exceptions live there and
   must keep failing loudly on collisions).
3. `make -C textures`; review `items_texture.png` side by side with the enum.
4. `itemicons.cpp` only changes for new *special* icons (glint overlays etc. are
   out of scope at 16x16 RGB565).

Optional phase: compass/clock animation would need a second icon row and a
frame picker in `itemicons.cpp`; frame 0 remains the accepted stand-in.

### 3.4 GUI and other UI elements

- **No new GUI art pipeline is needed** — `gen_gui_textures.py` already cuts the
  1.17.1 sheets. Remaining work is replacing the game-owned pixels:
  - G2 block outline: keep `blockselection`'s role but redraw/recolour to
    vanilla's outline; the drawing code is `worldtask.cpp` +
    `BlockRenderer::drawTextureAtlasEntry`, so the change is asset + constants.
  - G1 boot splash: decision item (branding vs vanilla-style).
  - G3/G4 cleanup: remove dead art from the build (`textures/Makefile` `OBJS`,
    `terrain.cpp` includes) and from the repo.
- Screens vanilla has no counterpart for (help, sound test, graphing console)
  already share the vanilla dirt/widgets styling; they stay.
- Verification: `tests/menuui_test.cc` geometry pins plus the `tools/pcsim/`
  menu scenarios (`titlemenu`, `pausemenu`, `controls`, `inventory`, `gui`).

### 3.5 Sounds (code-change areas)

**Phase 1 — event semantics.** Teach `tools/audio/build_audio_pack.py` to parse
`sounds/sounds.json` and emit, alongside `audio_sounds.h`, a generated variant
table: one `Sound::Event` per vanilla event with its file list and weights
(e.g. `EventBlockStoneBreak -> { id 311, count 4 }`). Then replace the hand
rolled `family[nextRandom() % 3]` pickers in `audio_manager.cpp`
(`footstep`, `digBlock`, `placeBlock`, `mobSound`, `playerAttack`,
`playerEat`, …) with lookups into the generated ranges. Keep the pack format
(`AUD1`) unchanged — the variant grouping lives in generated C++, not in the
pack, so no format bump and no runtime parser.

**Phase 2 — cue coverage.** Every gameplay sound the game raises maps to a
named vanilla event (audit list: per-mob say/hurt/step for the 12 `MobKind`s,
the 11 `Material` footstep/break/place families, UI click/levelup/pickup,
chest/door, weather thunder + rain loop, cave ambience). Cues that exist as
`playSound(id)` call sites move to the named API so the pack stays swappable.

**Phase 3 — music and beds.** The two music archives already feed the pack;
formalise the track list from vanilla's soundtrack (menu tracks on the title,
world tracks in game — `startMusic()`/`stopMusic()` already rotate the pack's
music entries) and keep rain/cave beds as streamed loops (`FlagStream`).

Files touched: `tools/audio/build_audio_pack.py`, generated `audio_sounds.h`,
`audio_manager.{h,cpp}`, `audiotesttask.cpp` (test rows),
`tests/audio_manager_test.cc` (cue coverage), `BUILD_TNS.md` (build command and
counts). `audio_pack.{h,cpp}` and the output backends do not change.

### 3.6 Verification and acceptance

- Regenerate: `make -C textures`, `python3 tools/audio/build_audio_pack.py …`;
  the generators keep failing loudly on unmapped names/collisions.
- `make -C tests` (all suites) and the `tools/pcsim/` screenshot scenarios with
  pixel comparison against the checked-in baselines; update baselines only for
  reviewed art changes.
- Size budget: watch `crafti.tns` (~750 KB today) after every atlas change;
  the header embeds are the main ROM cost.
- Pack check (from `BUILD_TNS.md`): the AUD1 sound count must match the
  generated `audio_sounds.h` enum.
- **Acceptance:** with a pack installed, every audible cue resolves to a
  vanilla event sample; every block face and item icon is the official 1.17.1
  pixel art; the only remaining game-owned images are the documented
  exceptions (G1 boot splash if kept, G2 outline geometry, the composed
  chest/bed/cake pieces, compass/clock frame 0).

---

## 4. File change list

| action | file | why |
| --- | --- | --- |
| modify | `tools/textures/gen_block_textures.py` | source-map entries for newly added block faces |
| modify | `terrain.cpp` (block table, `terrainInit` tints) | tile mapping per block; drop `textures/terrain*.h` includes |
| modify | `textures/items.h`, `tools/textures/gen_item_textures.py` | append-only enum + item source map |
| modify | `itemicons.cpp` | only for new special-case icons |
| modify | `textures/Makefile` | drop G4 legacy headers from `OBJS` |
| modify | `tools/audio/build_audio_pack.py` | `sounds.json` parsing, generated event/variant table |
| modify | `audio_manager.{h,cpp}` | variant-range pickers, cue coverage |
| modify | `audiotesttask.cpp`, `tests/audio_manager_test.cc`, `tests/menuui_test.cc` | test rows and pins |
| modify | `BUILD_TNS.md`, `GUI_VANILLA_PORT.md` | keep the docs in step |
| add | `sounds/sounds.json`, `sounds/**` (local only, gitignored) | vanilla event manifest + extracted audio tree |
| add | `textures/block/*.png`, `textures/item/*.png` | the missing vanilla art as gaps G5/G6 are closed |
| delete | `textures/terrain.h`, `textures/terrain2.h`, `textures/selection.png/.kra` (after audit) | dead non-vanilla art |

## 5. Risks and constraints

- **Licensing**: vanilla art/audio must not be redistributed. The repo keeps
  only the *arrangement* code and the documented file names; the extracted
  trees and `crafti.audp` stay gitignored (current practice in `BUILD_TNS.md`).
- **Atlas ABI**: hardcoded `terrain_atlas[x][y]` callsites and saved item data
  (`ItemTexture` bytes) make reshuffles/renumbering save-breaking; extend-only.
- **Size**: headers are the ROM budget; trim G4 first if a change needs room.
- **Audio fidelity**: 8 kHz unsigned mono and the 1-bit UART output are fixed
  product decisions (`AUDIO_OUTPUT_TEST.md`); vanilla pitch/weight variation
  collapses to file choice + gain, which is fine for the medium.
