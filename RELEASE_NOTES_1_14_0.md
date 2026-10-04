Muntcraft 1.14.0
==============

Playable on a damaged keyboard, and the block outline is vanilla's own shape
at last.


Playable with the keys that still work
--------------------------------------

The calculator this was built for has a keyboard that no longer reads `8 5 6 4
. Car Menu Doc Tab Esc`. Those are not a few bindings to skip -- they were the
movement cross, jump, block list, backspace, and both ways out of a menu. The
game is now playable without reading any of them.

Every binding lives in one new file, `controls.h`, and no screen names a physical
key any more. The scheme:

    the four arrow keys round the touchpad   walk, and move the cursor in a menu
    the touchpad itself                      the camera; tap to level the view
    the touchpad's click button              jump, and select in a menu
    Enter                                    select, as a second way in
    Shift                                    the menu, and back out of anything
    Ctrl                                     memory: saves the world
    B                                        the block list (Ctrl+B screenshots)
    the Bar key                              erase, where Delete used to be
    /                                        the console

The Bar key is the one worth explaining: it sits directly above the dead Delete
key, so it is under the same finger, and it is where backspace now lives -- in
the console, in the world list's search box, and in the rename field.

The banned keys are not merely unused. `Task::keyPressed` has no desktop mapping
for any of them either, so a screen cannot grow a dependency on one by accident,
and `main.cpp` no longer reads Esc for its two early exits -- a stuck Esc would
have quit the game before it had started.

Four jobs had no home once Esc and Menu went away, so they moved:

  - the world-select button walk moved to Left/Right, and the create form's
    field cycle to Up/Down
  - the inventory's two drag gestures moved to the var and cat keys, since Shift
    is the menu everywhere else

Sprint and sneak are gone rather than moved. Both keys are spoken for, so walking
is at the speed setting and the only modifier left is the desktop's 10x.


The block outline is a wireframe, like vanilla's
------------------------------------------------

G2 is closed. Vanilla draws the selection box with `DrawMode.LINES` over a
POSITION-only vertex format and binds no texture at all, so there was never
anything to convert: the hand-drawn sheet could not become vanilla's outline, it
could only ever be replaced by lines. `worldtask.cpp` now emits the 12 edges
itself in flat black, inflated by vanilla's `OUTLINE_SIZE` of 0.002 of a block so
the box sits on the surface instead of z-fighting through it. nGL's rasteriser is
one pixel wide, which is the `lineStrength` of 1.0 that vanilla's
`RenderType.outline()` asks for.

`textures/blockselection.{h,png,kra}` are deleted, which takes the atlas from 41
embedded headers to 40.

One engine bug had to be fixed first, and it was not small. `nglDrawLine3D` in
`nGL/gl.cpp` computed `dy = diff_y / diff_x` *before* testing whether `diff_x`
was zero. Every world-vertical edge of the box is a screen-vertical line, so
drawing the outline trapped on a divide by zero every time. `tests/nglines_test.cc`
covers it now, along with the rasteriser itself: horizontal, screen-vertical,
diagonal, disjoint pairs, a degenerate segment, vertex colour and the z-reject,
and a real 12-edge box -- 31 checks, all passing.

The outline is verified in the running game, not just on the host.
`tools/pcsim/blockoutline.txt` captures it from four angles; a negative control
with the indicator toggled on and then back off differs by ~300 pixels that are
black only in the on-run, which is the box.


Also in this release
--------------------

  - `tests/nglines_test.cc`, the line rasteriser's test (31 checks)
  - `gmon.out` is gitignored, so a `-pg` host build stops leaving a 318 KB
    profile in the working tree
  - the version string is 1.14.0, and `MENU.md` / `GUI_VANILLA_PORT.md` now
    describe the scheme above rather than the `8-4-6-2 / 5 / Esc` one


Verified
--------

`make clean && make -j2` clean, `crafti.tns` 744,449 bytes (23,551 under the
768,000 ceiling). `make -C tests -s` green across 21 binaries. The desktop
simulator scenarios reach the same depth as before the change -- `tour` 54 shots,
`gui` 14/14, `titlemenu` 9/9, `controls` 10/13, `blockoutline` 7/7.

Known gaps, stated plainly: there is no TI-Nspire here, so the remap is verified
through the host build, the desktop SDL map and the simulator rather than on
hardware. Three scenarios (`ui`, `pausemenu`, `pointer`) still stop short; they
were checked against the pre-change tree and stop at the same points, because the
harness cannot enter a world by keyboard alone -- the title opens with nothing
focused. And `. 4 5 6 8` cannot be typed into the console on a keyboard that
cannot read them; no command needs them, and the entries are left visible in the
text table and documented in `controls.h` rather than quietly dropped.