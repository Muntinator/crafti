# The front-end: the title screen and the pause menu

Muntcraft's title screen is meant to be recognised as Minecraft's: dirt behind
everything, a stone wordmark across the top, a yellow splash line tucked under it,
grey bevel buttons with a white border on the one you are on, and the version and
credits along the bottom. The pause menu is the same widgets over the world it
paused, with no heading — vanilla's pause screen is buttons and nothing else.

## Muntcraft

The game is called Muntcraft everywhere a player can read the name: the title
screen's wordmark, the version line (`Muntcraft 1.8.9`), the help heading and the
file. The save file keeps the name `crafti.map.tns` it has always had, because that
is the name the calculator's file association is registered under and renaming it
would orphan every world that already exists.

## What is drawn, and where it lives

| Piece | Where |
| --- | --- |
| the palette, the widgets, the wordmark, the splash, the layouts, and every string the front-end shows | `menuui.h` / `menuui.cpp` |
| the title screen: choosing a world, or quitting | `starttask.cpp`, drawing through MenuUI |
| the pause menu: back to the game, the options, saving | `menutask.cpp`, drawing through MenuUI |
| the scaled text the wordmark is made of | `drawStringScaled()` in `font.cpp` |
| the layout test | `tests/menuui_test.cc`, built at both screen sizes |

The layout is deliberately in the module rather than in the two tasks: the strings
and the boxes they have to fit in are decided together, which is what lets a host
test check that every label fits its button, that the wordmark and its outline fit
a 320-pixel screen, and that the column of buttons never reaches the small print
at the bottom. That test is built twice — once for the calculator (320x240) and
once for the desktop window (640x480), where the front-end scales by two.

## Optimisations in this pass

- **The backdrop is drawn once.** The dirt is tiled and dimmed into a screen-sized
  texture the first time the title screen is shown and blitted from then on, so the
  screen costs one copy per frame instead of two hundred and fifty-six scaled tile
  draws plus a full-screen darkening pass.
- **The wordmark is drawn once**, outlines, extrusion and bevel included, into a
  texture that is then blitted with its transparent key. Redrawing it per frame
  would be a few thousand pixel writes to say the same thing.
- **The pause screen's dimming is a mask and a shift.** Halving every channel of a
  565 pixel is `(c & 0xF7DE) >> 1`, which is the fast path `MenuUI::shadeRect()`
  takes at 50% — the case the menus use.
- **Text can no longer draw outside the screen.** The font used to write wherever
  an unsigned coordinate pointed, so a centred line wider than the screen corrupted
  whatever followed the framebuffer. Both `drawString()` and `drawStringCenter()`
  now clip.

## The people in the world

The player and the villagers are the only humanoids. There was a Steve mob — a
second, ambiguous humanoid that only a debug command could create and that did
nothing but stand there — and it is gone: entity, spawn path, melee target and
debug count. Villagers keep the humanoid model and the shared skin, which now
lives in `textures/steve.h` directly rather than behind the deleted entity's
accessor. `/summon` offers animals and creepers only.
