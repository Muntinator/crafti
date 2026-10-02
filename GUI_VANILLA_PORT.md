# Porting the GUI and main menu to vanilla Minecraft 1.17.1

This records what the front-end used to look like, how it now compares with
Java Edition 1.17.1, and the plan that was worked through to make the **main
menu** look and behave like vanilla.

## Status

| Phase | State |
| --- | --- |
| 0 -- assets and generator | **done**: the 1.17.1 art is under `textures/gui/title/` and `textures/font/`, and `tools/textures/gen_gui_textures.py` cuts the wordmark, the edition strip, the 8-pixel font (both GUI scales) and the pre-rendered panorama |
| 1 -- vanilla font | **done**: `font.cpp` reads the 256x256/8-pixel atlas with derived advances on the calculator and the doubled one on the desktop |
| 2 -- the title screen (steps 8-13) | **done**: pre-rendered panorama, the real `minecraft.png` wordmark (the two vanilla halves joined) plus `edition.png` at vanilla's own y, the splash centred at `(width/2 + 90, 70)` and pulsing, vanilla's 3+2 button block, the version/credits at `height - 10`, the input hint and audio status dropped from the small print, and mouse hover/click on the desktop |
| 3 -- pause menu and options | **done**: the pause menu is vanilla's `PauseScreen` -- the world washed in the screen's dark gradient, the "Game Menu" heading at y 40, and vanilla's own grid (a full-width button, three rows of two, a full-width button) carrying vanilla's entries, with two stand-ins (`Help`, `Block List`) and this game's own three filling the rest; its child screens (`Options...`, `Help`, `Block List`) return to the pause menu. The options screen is rebuilt on vanilla's widget grid (dirt, sliders, checkboxes, list buttons, a GUI-scale entry) and the loading screen is ported (dirt, "Loading terrain...", a progress bar) |
| 4 -- verification | **partly**: `tests/menuui_test.cc` pins the new geometry, the font metrics, the splash fit and the death layout at both screen sizes; frame-by-frame comparison against vanilla references is still open |
| 5 -- the remaining screens | **done**: the death screen is rebuilt on vanilla's `DeathScreen` (red fade, doubled "You Died!", Respawn / Title Screen); the block list is rebuilt on vanilla's `CreativeScreen` (the tabbed creative inventory, scrollbar and all); and the screens vanilla has no counterpart for (help, sound test, graphing, the console) now use the same dirt, headings and widget buttons as the rest of the front-end |

What was *not* known while the investigation was written, now settled from the
texture and from 1.17's own `TitleScreen`: `gui/title/minecraft.png` is a 256x256
sheet, and the wordmark the title screen shows is stored as **two halves stacked
in it** -- vanilla blits `(0,0,155,44)` at the logo's left edge and `(0,45,155,44)`
155 pixels to its right (`title.render()`: `blit(var7 + 0, 30, 0, 0, 155, 44)` and
`blit(var7 + 155, 30, 0, 45, 155, 44)`, with `var7 = width / 2 - 137`). The
generator joins the two halves (the right one's ink stops at x 118) into the single
274x44 image the engine draws, so it lands at `width / 2 - 137` exactly as vanilla's
two blits do. The sheet's lower copies (y ≥ 94) are a *different, larger* rendering
and are not what the title screen uses. The button sheet turned out to be ordered
greyed-out / plain / highlighted rather than plain / highlighted / greyed-out,
which the old `drawButton()` had backwards.

The reference is the 1.17.1 client: its asset tree
(`assets/minecraft/textures/gui/…`) and the behaviour of `TitleScreen`,
`PauseScreen`, `AbstractButton` and the font provider. Vanilla coordinates below
are stated "at GUI scale 1" unless noted; this engine's own scale is
`MenuUI::uiScale()` (1 on the CX's 320×240, 2 on the desktop's 640×480).

---

## 1. Scope and hard constraints

The engine is not a renderer that can simply load the vanilla screens. Anything
proposed has to survive these, which is why some gaps below are "faithful
approximation" rather than "exact copy":

* **Screen**: 320×240, RGB565 (and a 4-level greyscale CX variant). No GPU, no
  alpha blending — nGL blits opaque rects and copies pixels.
* **No font engine**: text is a bitmap atlas sampled cell by cell
  (`font.cpp`), so any vanilla font port means porting glyph metrics too.
* **Input**: Up/Down + `5` on the calculator; mouse only exists on the desktop
  harness. The port added what the menu needed — hover on the desktop and a
  scrolling options grid — but there is still no general widget toolkit, no text
  field and no focus ring beyond what each task implements.
* **Memory**: the title screen already caches two screen-sized textures and frees
  them on leave. Six 450–640 kB panorama faces (~3 MB) do not fit on a CX.
* **Licensing**: the art is Mojang's. This tree commits the converted headers
  next to their source sheets, as it already does for the HUD, the font and the
  container windows; `crafti.audp` is the one asset kept out of the repo (see
  `AUDIO_OUTPUT_TEST.md`).

---

## 2. Current state

### 2.1 Title screen — `starttask.cpp`, `menuui.*`

| piece | how it is drawn today |
| --- | --- |
| backdrop | the pre-rendered panorama (`textures/title_backdrop.h`): one cropped cube face, brightened and box-averaged to 320×240, blitted stretched — one texel copy per frame, not six projected faces |
| wordmark | the official `gui/title/minecraft.png` wordmark (`textures/title_logo.h`), the two 155x44 halves vanilla blits at `(0,0)` and `(0,45)` joined into one 274x44 image, centred (`width/2 - 137`) with its top at y 30; `edition.png` ("Java Edition") is centred with its top at y 67, over the wordmark's lower band |
| splash | vanilla's own lines (`texts/splashes.txt`, a hundred kept verbatim), rendered once into a small texture and blitted **tilted ≈ −20°** and pulsing, **centred** at `(width/2 + 90, 70)` (slid left only when a long line would overflow) |
| buttons | vanilla's block — three full-width (200) from `height / 4 + 48` at pitch 24, then two 98-wide on a shared row `72 + 12` below, at `width/2 - 100` and `width/2 + 2`; `Continue` disabled with no save, and disabled buttons cannot take the highlight |
| small print | bottom-left `Muntcraft 1.8.9` and bottom-right `Copyright Munt. Do not distribute!`, both at vanilla's `height - 10` (from x 2 and ending 2 inside the right edge); the old input-hint and audio-status lines are gone (the audio pack reports itself on the sound test screen) |
| input | `Up`/`Down`/`8`/`2` move focus, `5`/`Enter` activate, `Esc` resumes the last world or quits; on the desktop a mouse hovers to set focus and a click activates |

### 2.2 Pause menu — `menutask.cpp`

Vanilla's `PauseScreen`, geometry and all:

| piece | how it is drawn today |
| --- | --- |
| background | the last world frame, washed in `Screen.renderBackground`'s vertical gradient ARGB `0xC0101010` to `0xD0101010` (~75 % to ~81 % opacity) -- mixed into the framebuffer because nGL cannot blend (`MenuUI::drawPauseOverlay()`) |
| heading | **"Game Menu"**, centred at y 40 |
| grid | first button at `height / 4 + 8`, on a 24-pixel pitch: a **204**-wide button, three rows of two **98**-wide buttons, then a **204**-wide button |
| columns | `width / 2 - 102` and `width / 2 + 4` |
| entries | vanilla's own slots in vanilla's own order -- `Back to Game` / `Help`, `Block List` / `Save World`, `Sound Test...` / `Options...`, `Player Inventory` / `Save and Quit to Title` |
| children | `Help`, `Block List` and `Options...` open as children of the pause menu (`openFrom()`), so closing one returns here, not to the game |

The focus opens on `Back to Game`. `Save World` and the three stand-ins (`Help`
for "Advancements", `Block List` for "Statistics", `Player Inventory` in the
"Share to LAN" slot) fill vanilla's slots that this engine cannot back; a desktop
adds vanilla's focus-by-hover (the pointer lights the button and a click takes it).

### 2.3 Buttons — `MenuUI::drawButton` / `drawButtonLabel`

The official widget: `widgets.png`'s 200×60 block at (0, 46), rows **y = 46 /
66 / 86** — **disabled / plain / highlighted** — stretched to the box. (The old
`drawButton()` had this sheet's row order backwards; it now matches vanilla.)
Labels are white with a one-pixel shadow, grey `0xA0A0A0` when disabled.

### 2.4 HUD — `worldhud.cpp` (already vanilla)

`icons.png` sampled at the vanilla 1.17 coordinates (hearts y=0, armour y=9,
breath y=18, hunger y=27, xp bar y=64) and the hotbar from `widgets.png`
(0,0 182×22, selector 0,22 24×24). No gap.

### 2.5 Containers — `inventorytask.cpp`, `inventorychest.cpp` (already vanilla)

`container/inventory.png`, `crafting_table.png`, `furnace.png` and
`generic_54.png` crops, cut by `tools/textures/gen_gui_textures.py`.

Every container now draws `AbstractContainerScreen.renderLabels` the way vanilla
does: the screen's own title at `(titleLabelX, 6)` and the player's `Inventory`
label at `(8, imageHeight - 94)`, both in `0x404040`. The title indent is the one
vanilla gives each screen — `InventoryScreen` at 97, `CraftingScreen` at 29,
`ContainerScreen` (the chest) at the default 8, and `AbstractFurnaceScreen`
centred by `(imageWidth - font.width(title)) / 2`. The player's own window is the
exception it is in vanilla: `InventoryScreen` overrides `renderLabels` to draw
only its title, so it has no `Inventory` line.

The screens are also washed the way vanilla washes an in-game container:
`AbstractContainerScreen.render` reaches `Screen.renderBackground`, which lays
the pause screen's `0xC0101010`..`0xD0101010` gradient over the frozen world (not
the dirt the standalone menus tile). `InventoryTask::render()` and
`BlockListTask::render()` call `MenuUI::drawPauseOverlay()` for exactly this; the
background texture is the world frame every menu shares, so a container opened
from the pause menu re-washes the same frame rather than double-darkening it.

The player's own inventory also draws the **player model** in its right half,
the way vanilla's `InventoryScreen` does: `playermodel.{h,cpp}` transcribes
`PlayerModel.createMesh` (the classic Steve branch) into the Mob tables, unwrapped
against `textures/entity/steve.png`, and `renderEntityInInventory`'s camera is
reproduced by hanging the model off nGL's near plane and translating it to
`(51,75)` of the window at 30 px/block. The pointer drives vanilla's own
`atan(dx/40)` rule for the body turn, the doubled head turn and the lean. The
window's "Crafting" label is drawn with it.

The same window carries vanilla's **offhand slot** at `(77,62)` -- the one menu
that has it, since `CraftingMenu` and `FurnaceMenu` do not. It takes any item and
moves whole stacks, half stacks and swaps exactly as a plain `Slot` does; an
empty offhand draws the shield outline `InventoryMenu` overrides the slot's
`getNoItemIcon()` with. The world's `F` key is vanilla's swap-with-offhand. The
stack lives on `Inventory` beside the armour and is saved (format 14).

### 2.6 Options / Help / Death / Console / Block list

* **Options** (`settingstask.cpp`): the vanilla two-column widget grid on the
  official dirt -- sliders, checkboxes and list buttons -- with vanilla's
  "Options..." title and a wide "Done" button, plus a GUI-scale entry. The widget
  a row gets is decided by the entry's own shape (number, on/off, named list),
  which is vanilla's rule too. The `selection.h` cursor and the dimmed backdrop
  are gone.
* **Help**, **Death**, **Console**, **Block list**: each is a bespoke layout.

### 2.7 Font — `font.cpp`

The game's own 16×14 bitmap is gone. The front-end draws with vanilla's 8-pixel
font (`font/ascii.png`, a 16×16 grid of 8×8 cells), sampled and advanced by the
same cell-and-advance model. `font_bmp.h`/`font_dat.h` are the atlas and its
metrics — the advances derived by scanning each cell for its rightmost opaque
column, vanilla's own rule — and `font_bmp_wide.h`/`font_dat_wide.h` are the
doubled pair the desktop reads. `drawString`, `drawStringCenter`,
`drawStringScaled` and `measureString` all read it.

### 2.8 Loading screen — `main.cpp`, `chunk.cpp`

`loading.png` (320×240) is the boot splash. While a world is loaded or saved,
`chunk.cpp`'s `drawLoadingtext()` now draws vanilla's loading screen -- the dirt,
"Loading terrain..." and a progress bar -- instead of the game's own
`loadingtext.png`, and flushes it before the frame's render fills over it.

### 2.9 Assets on hand

`textures/gui/` (`icons`, `widgets`, `options_background`, `checkbox`,
`container/…`, `title/{minecraft,edition}`,
`title/background/panorama_0..5`) and `textures/font/ascii.png` — all official
1.17.1. `tools/textures/gen_gui_textures.py` cuts them into the RGB565 headers
under `textures/` (the buttons, the widgets, the wordmark, the panorama and the
font), so the screens blit the real art rather than painting it by hand.

---

## 3. Discrepancies vs vanilla 1.17.1

| # | Element | Before the port | Vanilla 1.17.1 | Gap, and how it was closed |
| --- | --- | --- | --- | --- |
| 1 | **Title background** | tiled `options_background.png` dirt at 25 % | rotating **panorama cubemap** (`gui/title/background/panorama_0..5.png`), dark gradient overlay | **fixed** (Phase 2): dirt is the *options* screen art; the panorama is pre-rendered into `title_backdrop.h` |
| 2 | **Logo** | text `MINECRAFT`-style wordmark drawn from the game font | `gui/title/minecraft.png`, its two 155×44 halves blitted at `(0,0)`/`(0,45)` to `width/2 - 137`, top y 30, with `edition.png` ("Java Edition") centred at top y 67 | **fixed** (Phase 2): the official halves are joined and blitted; `edition.png` sits at vanilla's own y |
| 3 | **Splash** | horizontal, 12 custom lines | rotated **≈ −20°**, pulsing scale ~1.8, **centred** at `(width/2 + 90, 70)`, sourced from `splashes.txt` (hundreds of lines, incl. the "Minceraft" easter egg) | **fixed** (Phase 2): tilted, pulsing and centred where vanilla puts it |
| 4 | **Button geometry** | width `SCREEN_WIDTH*2/5` (128 / 256), h 20, gap 4 | width **200** at scale 1, h 20, pitch **24** | **fixed** (Phase 2): width 200 (clamped to the screen) |
| 5 | **Button placement** | column centred at `H/2 + H/24` | three full-width from `H/4 + 48` on a 24-px pitch, then two 98-wide at `H/4 + 48 + 72 + 12` at `width/2 - 100` and `width/2 + 2` | **fixed** (Phase 2): vanilla's 3+2 block, at vanilla's own y and x |
| 6 | **Button art/label** | official rows, but `drawButton()` read them plain/highlighted/disabled | same | **fixed** (Phase 2): the sheet is *disabled / plain / highlighted*; `drawButton()` now matches |
| 7 | **Font** | 16×14 custom bitmap | `font/ascii.png`, **8-px** glyphs (16×16 grid of 8×8 cells), per-glyph advances from the provider | **fixed** (Phase 1): the vanilla 8-px font is used throughout |
| 8 | **Small print** | version + credits + input hint + audio status | version bottom-left, `Copyright Mojang AB…` bottom-right, **no hint** | **fixed** (Phase 2): the hint and audio-status lines are dropped |
| 9 | **Interaction** | keyboard only, even on desktop | mouse **hover** sets focus, click activates; keyboard arrows/Enter also work; tooltips on hover | **fixed** (Phase 2): mouse hover/click on the desktop |
| 10 | **Pause menu** | a dimmed world (a flat 50 % shade) + a centred column of 6 — `Back to Game`, `Options...`, `Help`, `Save World`, `Sound Test...`, `Save and Quit` — with **no heading** | `Screen.renderBackground`'s dark gradient, **"Game Menu"** at y 40, and vanilla's grid: a 204-wide button, three rows of two 98-wide, a 204-wide button, from `height/4 + 8` at `width/2 - 102` / `width/2 + 4` | **fixed** (Phase 3): the gradient wash, the heading, vanilla's grid, and vanilla's own order with `Help`/`Block List` for Advancements/Statistics and this game's `Save World`/`Sound Test...`/`Player Inventory` in the rest |
| 11 | **Options screen** | a bespoke text list with a `selection.h` cursor and a dimmed backdrop | vanilla dirt + a two-column grid of slider/checkbox/button rows, an "Options..." heading and a "Done" button | **fixed** (Phase 3): vanilla dirt and the two-column widget grid |
| 12 | **GUI scale** | no such option | user option Auto/1/2/3/4 | **fixed** (Phase 3): an Auto/1x/2x/3x/4x entry, applied to the front-end only — the HUD and inventory keep their own scaling |
| 13 | **Loading screen** | the game's own `loadingtext.png`, blitted straight to the framebuffer | dirt + `Loading terrain...` + a progress bar | **fixed** (Phase 3): dirt + `Loading terrain...` + a progress bar (the bar is drawn as colour, because the official `bars.png` sheet is not in this tree) |

Already aligned (no work): the button **texture** and its three rows, the HUD
sheet, and all four container windows.

---

## 4. Plan — porting the main menu to vanilla

The title screen is the target; phases 0–2 are the main-menu port, 3–4 are the
follow-on screens and verification.

### Phase 0 — assets and generator (prerequisite) — *done*

1. Add the missing vanilla art under `textures/gui/`, in the vanilla tree
   layout the other passes already use:
   `gui/title/minecraft.png`, `gui/title/edition.png`,
   `gui/title/background/panorama_0..5.png`, `gui/title/mojangstudios.png`, and
   `font/ascii.png` (+ `accented.png` if accented text is wanted).
2. Extend `tools/textures/gen_gui_textures.py` with panels for `title_logo`,
   `edition` and the **font atlas** (emit the atlas plus a table of per-glyph
   advances — derive the advances by scanning each cell's rightmost opaque
   column, so no hand table can drift).
3. Add a **`title_backdrop`** panel: a single pre-rendered panorama frame at
   320×240 and 640×480, produced once from vanilla's own panorama (see Phase 2,
   step 7). Keep it a generated header like the rest.
4. Register every new header in `textures/Makefile`'s `OBJS`.
5. Follow the tree's existing asset convention: check the new source sheets and
   their generated headers in next to the others (the audio pack's `crafti.audp`
   stays out of the repo; the GUI art does not).

### Phase 1 — a vanilla font (do this before the title screen) — *done*

6. Port the font in `font.cpp` to the 8-px atlas: change `drawChar` to sample the
   new atlas and glyph advances, and make `fontHeight()` return the vanilla line
   height. `measureString()` keeps its signature.
7. Fix the layout assumptions that assumed 14-px glyphs — settings row spacing,
   the HUD item-name line, the title's `logoScaleFor` (which will be replaced by
   the real logo image, see below) — and re-run `tests/menuui_test.cc`.

### Phase 2 — the title screen itself — *done*

8. **Background**: replace the dirt backdrop with the panorama.
   * *Recommended:* blit a single pre-rendered panorama frame (Phase 0 step 3)
     tiled 1:1, with the vanilla dark gradient overlay, cached into a screen
     texture exactly as the dirt is today. Matches the look, costs one texture,
     fits the CX.
   * *Optional (desktop only):* a true 6-face cubemap with vanilla's isometric
     projection, if the memory budget is ever raised.
   * Move the **dirt** `options_background.png` to the options/other screens,
     which is where vanilla uses it.
9. **Logo**: draw `gui/title/minecraft.png` centred (x = width/2, top ≈ 30 scaled)
   and `edition.png` beneath it. Delete the text wordmark path (`drawLogo`,
   `buildTitleLogo`, the `logoScaleFor` sizing) once nothing else uses it.
10. **Splash**: render the splash into a small texture and **rotate it ≈ −20°**
    when blitting (nGL has no rotated blit, so add a small rotated-sprite helper
    or pre-rotate the rendered line). Anchor it at the logo's lower right and
    keep the scale pulse. *Done:* the lines are vanilla's, sampled out of
    `assets/minecraft/texts/splashes.txt` (the full file's 427 lines are 9 kB of
    text, so a hundred of them are kept verbatim, and the ones written in
    characters this font has no glyphs for are dropped). The 0.1 % "Minceraft"
    easter egg is **not** kept: it swaps the wordmark image as well as the line,
    and the wordmark here is the single-line crop the screen always shows.
11. **Buttons**: set the column to vanilla geometry — width 200 (`* uiScale`,
    clamped to the screen), height 20, pitch 24, `x = (W - 200)/2` — and place
    the first button at vanilla's `H/4 + 48` (or the closest y that still fits
    five buttons on 240 px). **The art and label styling need no change.**
12. **Small print**: keep the version bottom-left and a copyright bottom-right
    (rewritten to the project's own), and **drop the input-hint and audio-status
    lines from the title**; move the audio status into the options screen where
    vanilla keeps its diagnostics.
13. **Interaction**: on the desktop build, hit-test the mouse against the button
    rects in `StartTask::logic()` and set `selected_item` (hover row highlight),
    with click to activate; keep `Up`/`Down`/`5`/`Enter` for the CX. The click
    cue already exists (`GameAudio::uiClick()`).

### Phase 3 — pause menu and options — *done*

14. **Pause**: add the vanilla **"Game Menu"** heading and relabel/reorder to
    `Back to Game`, `Advancements`, `Statistics`, `Options...`,
    `Save and Quit to Title` — substituting this game's own entries
    (`Help`, `Sound Test…`) only where a vanilla screen cannot be backed.
    *Done:* the "Game Menu" heading at y 40 over vanilla's `renderBackground`
    gradient (`0xC0101010`→`0xD0101010`, mixed in because nGL cannot blend), and
    the eight entries in vanilla's own grid -- a full-width button, three rows of
    two at `width/2 - 102` / `width/2 + 4`, then a full-width button, every one 20
    high on a 24-pixel pitch from `height/4 + 8`. `Help` stands in for
    Advancements, `Block List` for Statistics, and this game's `Save World`,
    `Sound Test...` and `Player Inventory` fill the remaining slots. The grid is
    built by `MenuUI::pauseMenuLayout()`, so the drawing and the click hit test
    read the same boxes; the focus opens on `Back to Game`. `Options...`, `Help`
    and `Block List` open as children (`openFrom()`) and close back to the pause
    menu, as they do in vanilla. *Captured by `tools/pcsim/pausemenu.txt`.*
15. **Options**: rebuild `settingstask.cpp` from vanilla widgets — dirt
    background, a title, and button/slider/checkbox rows drawn from
    `widgets.png`/`checkbox.png` (confirm the 1.17 sheet offsets while
    implementing) — and add a **GUI scale** entry. *Done:* the two-column
    `optionsLayout()` grid, `drawSlider()`, `drawCheckbox()` (its sheet's column
    is the tick, its row the highlight — confirmed against `checkbox.png`), the
    cycling list buttons, the "Done" button and scrolling. The GUI scale is
    appended last so old save files keep loading.
16. **Loading**: dirt + `Loading terrain...` + a progress bar. *Done:*
    `MenuUI::drawLoadingScreen()`, drawn by `drawLoadingtext()` and rendered on
    the host in `tests/menuui_test.cc`; `drawLoadingtext()` flushes the buffer
    itself so the screen is actually seen.

### Phase 4 — verification (layout pinned; reference comparison open)

17. Extend `tests/menuui_test.cc` to pin the new geometry (logo rect, button
    width/positions, splash anchor, small-print corners) and the glyph advances;
    keep it building under both `_TINSPIRE` and desktop. *Done:* 870 checks at
    320×240 and 870 at 640×480, all passing (the pause grid is pinned alongside
    the title, options, loading and death layouts).
18. Use the pcsim harness to capture the title, pause, options and loading
    screens (`sh tools/pcsim/play.sh`) and compare them against vanilla
    references. *Partly:* the harness captures the title screen
    (`tools/pcsim/titlemenu.txt`), the pause menu with its button interactions
    (`tools/pcsim/pausemenu.txt`), the screens behind the pause menu
    (`tools/pcsim/ui.txt`) and the title/pause/options/loading set
    (`tools/pcsim/gui.txt`, 14 frames), and individual frames are verified
    numerically against the source art (the pause buttons against `menu_button`,
    the loading bar against its own colours, the pause overlay's gradient and
    the grid's focus states against the layout); a frame-for-frame comparison
    against rendered vanilla references is the remaining piece, and it needs
    reference images.

### Phase 5 — every other screen

19. **Death screen**: rebuild `deathtask.cpp` on vanilla's `DeathScreen` -- the
    red gradient (alpha 0x60/red 0x50 at the top to alpha 0xA0/red 0x80 at the
    bottom, mixed in because nGL cannot blend), "You Died!" doubled by the new
    `font.cpp` scaled draws at vanilla's y 60, the cause-of-death and score
    lines, and the two widget buttons from `height / 4 + 72` labelled with
    1.17.1's own `deathScreen.respawn` / `deathScreen.titleScreen` strings.
    *Done:* `MenuUI::deathLayout()`, `drawDeathOverlay()`, `drawHeadingScaled()`,
    `drawStringScaled()`. The engine has no damage log, so the message line names
    the respawn point (vanilla's cause-of-death slot). The score line shows the
    player's **score** (`WorldTask::score()`), which is vanilla's
    `Player.getScore()` -- a value separate from experience, zero without a
    scoreboard, kept across a respawn and settable with `/score`.
20. **No-counterpart screens**: the help, sound test and graphing screens and the
    command console are not recreated from a vanilla screen (there is not one),
    but they now share the front-end's own look -- the dirt backdrop,
    `drawHeading()`, the official widget buttons, and a vanilla text-field box for
    the graph expression / a translucent chat panel for the console. *Done;
    captured by `tools/pcsim/ui.txt`.*
20b. **Block list = creative inventory**: the block list is now vanilla's
    `CreativeScreen` itself -- the 195x136 `creative_inventory/tab_items` window
    (grid, hotbar and scrollbar track baked in), the `tabs` strip at
    `x = guiLeft + 29*i`, 28 px above the window, unselected from the sheet's
    first band and selected from the second, each tab carrying its category item
    at (+6, +9), and the `tabs` scrollbar handle sweeping its track by
    `(112 - 17)` px when a page outgrows the five rows. The pages
    (`Blocks`/`Items`/`Tools`) are the tabs, so the page keys switch tabs too, and
    a page longer than 45 slots scrolls the way the item list does. *Done; new
    sheets `textures/creative_window.h` / `creative_tabs.h` / `creative_scroll.h`;
    captured by `tools/pcsim/ui.txt` (frames 04-06: the first tab, the second tab,
    and the first tab scrolled to its last row).*

---

## 5. Effort and risk

* **Font (Phase 1)** — highest leverage (every screen) and highest risk: glyph
  metrics feed all layout maths and `menuui_test`. Do it first, on its own.
* **Panorama (Phase 2 step 8)** — largest asset decision; the pre-rendered still
  is the only option that fits the CX.
* **Splash rotation (step 10)** — needs a new rotated-blit helper; small but new.
* **Options rebuild (Phase 3)** — the biggest single rewrite, and the least
  "main menu"; defer until the title screen is done.

### Recommended first change

Do **Phase 0 + Phase 1 + Phase 2, steps 8–13** as one pass: the pre-rendered
panorama, the real `minecraft.png` logo + edition strip, the rotated
vanilla-sourced splash, the 200-px buttons, and the 8-px font — with the
desktop mouse hover wired in. That is the visible "main menu looks like
Minecraft" milestone, and it is verifiable in the existing harness. *(This was the
pass that was taken; phases 0–3 are all complete now — see the status table, and
only Phase 4's reference comparison remains.)*

---

## Sources

* Vanilla 1.17.1 asset tree (`InventivetalentDev/minecraft-assets`, ref
  `1.17.1`): `textures/gui/title/{minecraft,edition,mojangstudios}.png`,
  `textures/gui/title/background/panorama_0..5.png`,
  `textures/gui/{widgets,icons,options_background,checkbox}.png`,
  `textures/gui/container/{inventory,crafting_table,furnace,generic_54}.png`,
  `textures/gui/container/creative_inventory/*.png`,
  `textures/entity/steve.png`,
  `textures/item/empty_armor_slot_{helmet,chestplate,leggings,boots,shield}.png`,
  `textures/font/{ascii,accented}.png`.
* Vanilla client behaviour: `TitleScreen` (logo at width/2, y ≈ 30; splash
  rotated −20° at ≈ width/2 + 90, y 70; buttons from `height/4 + 48` on a 24-px
  pitch), `PauseScreen` ("Game Menu" at y 40, its first button at `height/4 + 8`,
  a 204-wide button then three rows of two 98-wide at `width/2 - 102` and
  `width/2 + 4` then a 204-wide button, its button labels, and a `DemoScreen`
  only when the world is a demo), `Screen.renderBackground` (the vertical
  gradient `0xC0101010` to `0xD0101010` vanilla blends over the world),
  `AbstractButton` (200×20, white + shadow text, grey when disabled, hover row),
  `DeathScreen` (red `fillGradient` from (0x60,0x50,0,0) to (0xA0,0x80,0,0);
  title drawn at twice the GUI scale, centred, at y 30 inside the 2x matrix i.e.
  60 GUI pixels down; `deathScreen.respawn` / `deathScreen.titleScreen` buttons
  from `height/4 + 72` on the usual pitch), `TextFieldWidget` (a black field in
  a light grey border), the chat overlay (a translucent black panel).
* Vanilla 1.17.1 language file (`assets/minecraft/lang/en_us.json`):
  `deathScreen.title` = "You Died!", `deathScreen.respawn` = "Respawn",
  `deathScreen.titleScreen` = "Title Screen".
* This repository: `starttask.cpp`, `menutask.cpp`, `settingstask.cpp`,
  `menuui.{h,cpp}`, `font.cpp`, `worldhud.cpp`, `inventorytask.cpp`,
  `tools/textures/gen_gui_textures.py`, `textures/Makefile`, `MENU.md`,
  `AUDIO_OUTPUT_TEST.md`.
