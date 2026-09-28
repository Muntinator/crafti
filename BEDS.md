# Beds

Sleeping is the one thing a player does to the *clock* rather than to the world.
This document is the rule sheet for the bed block: how it is built out of two
blocks, how those two halves agree on a direction, what makes a bed usable, and
where the player comes back after dying.

## The block

`BLOCK_BED` (id 46) is the highest plain block, right after `BLOCK_CHEST`, and it
is the first *horizontal* multi-block structure in the game: the door is two
blocks stacked vertically, the cake is one cell.

A bed is two cells laid flat, a **foot** and a **head**, sharing one facing:

```
        data = facing | (head ? 0x08 : 0)

        facing   BLOCK_FRONT (0) .. BLOCK_RIGHT (3)   -- never a vertical side
        bit 3    this half is the head
```

`facing` is the direction *from the foot to the head*, so the head sits at
`offset(facing)` from the foot and the foot at `offset(facing)` back from the
head. Both halves carry the same byte apart from the head bit, which means either
one can find the other with no lookup table and no state anywhere else. The
encoding, the offsets and every rule below live in `bed.h`/`bed.cpp`, which are
pure and engine-free, so the host tests pin them down (`tests/bed_test.cc`, 3159
checks).

A bed that is written into the world by hand -- `/setblock`, or a save from a
build that did not have beds -- can carry a facing that is not horizontal. Every
reader treats that as a broken half rather than as a bed pointing into the ground:
it is drawn, it can be broken, and it cannot be slept in.

## Placing one

Placed beds take their facing from **where the player is looking**
(`Bed::facingForYaw`), not from the face that was clicked, because the head goes
*in front* of the player. That is the one place the bed differs from every other
oriented block: the side the world task derives from the yaw is the face of the
block being looked at, which is the opposite direction, and reusing it would put
the pillow on the wrong side.

`tryPlaceBed()` (in `worlditems.cpp`) places both halves or neither:

* both cells have to be `BLOCK_AIR`,
* if the head would land on the player, both halves are taken back and the
  placement is refused -- a bed that traps its owner is worse than no bed.

Half a bed is not a bed, so there is never one to clean up afterwards. The item is
only spent (and the place sound only played) when the whole bed went down.

## Sleeping

Right-clicking either half (the same hook the door toggles on) asks the bed to be
slept in. `Bed::canSleep()` allows it when

* it is night (`WorldClock::isNight()`), **or** the weather is a thunderstorm --
  which is why `weather.state` is consulted rather than a flag that would have to
  be kept in step with `weather.h`, and
* the other half is really there: the same facing, the other end of the bed
  (`Bed::isPartner`). Two feet side by side are two beds that touch, not one bed.

Anything else is a refusal with a line of its own ("You can only sleep at night"),
and the click is *always* consumed: a bed answers when it is used, even when the
answer is no, and returning `false` would make the world task try to place a block
into it instead.

Sleeping moves the clock, it does not speed it up:

```
time += Bed::ticksToMorning(time)      // the *next* dawn, never this one
```

The sun, the moon, the stars, the weather, the mob spawns and the survival ticks
are all functions of the clock, so that one call is what brings them all to morning
together. A day is always crossed -- lying down at dawn sleeps through a whole
day, rather than leaving the player asleep in the morning they are already in.

While asleep, `WorldTask::logic()` stops after `updateClock()`/`updateWeather()`,
so the player lies still, and a short fade runs in real time (900 ms, `bed.h`'s
`FadeTotalMs`): the screen darkens to 232/256 at the middle and clears again, with
"Good morning" and the level-up chime at the end of it. Real time rather than
frames matters here -- a fade counted in frames would take seven seconds on the
CX's three steps a second and a third of a second on a desktop.

## The respawn point

Using a bed records the **foot** cell in `WorldTask::bed_spawn`, which is what the
player comes back to after dying:

* storing the foot rather than "whichever half was clicked" is what makes the
  record comparable with the half being broken later (`Bed::isSpawnBed`),
* wording it as a cell also means a bed that is turned round is still the same
  respawn point, which is what a player expects,
* breaking **either** half forgets it, and so does replacing one with another
  block, because `removedBlock` runs for both paths,
* `respawnPlayer()` drops the player in from just above the mattress and lets the
  same down-ray that settles a fresh spawn place them, so a bed that has since
  been built over still leaves them on solid ground. A function plot (a graph
  world, which is not a place) ignores the bed and spawns where it always did.

The death screen says which of the two it will use, so the rule can be discovered
instead of only read about.

## Saving

Save format **version 12** adds four ints for the respawn bed -- `valid`, `x`, `y`,
`z` -- written after the play mode and *before* the world's chunk list, which ends
the file. Four ints rather than the struct: a `bool` in there would leave padding
bytes in the file. A world saved by version 11 or earlier reads as "no bed".

The bed **blocks** need no save support at all: they are ordinary blocks in the
chunk data, and the respawn point is a position, not a reference to one. That is
also why there is no bed entity -- two block halves hold everything there is to
know between frames.

## Art

The bed has no art in the atlas files, so its four tiles are painted at load time
into row 5 (the chest took row 4) by `terrain.cpp`'s `paintBedTile()`:

| tile | column | what it is |
|------|--------|------------|
| head top | 0 | the blanket with the pillow pad inset |
| underside | 1 | plain planks, which is what a bed is built on |
| side | 2 | the blanket turned over the wooden frame |
| foot top | 3 | the blanket on its own |

Every tile is left-right symmetric and only the side has a meaningful top and
bottom, so no tile has to be turned to follow which way the bed points -- which is
what keeps the two ends of the mattress meeting without a seam.

`BedRenderer` (`bedrenderer.cpp`) draws each half as a 9/16-high slab through
`renderSpecialBlock`, with the ordinary face shading (`BlockRenderer::
computeLighting`) and both faces drawn, the way the pressure plate's top face is.
The face along the other half is skipped -- but only when the block next door
really is the other half of the same bed -- so the two coincident quads that would
z-fight are never emitted, and a bed whose partner was replaced still draws
closed.

The item icon is the head tile: the pillow on its blanket, which is what tells a
bed apart from a red wool block at icon size.

## Crafting

Three wool (any of the sixteen colours) over three planks, as in Minecraft, for
one bed -- in the crafting table's 3x2 corner. `RecipeMat::WoolBlock` matches the
whole wool range the way `RecipeMat::Planks` matches all three plank blocks: a bed
is not a different bed because its blanket is blue.

## Debug commands

The bed is a plain block, so it needs no command of its own:

```
/give bed            hands over a bed to place
/setblock 10 30 -4 bed   writes a single half (data 0) -- usable once its head is
                         placed with another /setblock, and never a whole bed
```

Such a hand-written bed is broken on purpose: it is one cell, so it can never be
slept in, which is the honest outcome of typing half a bed into the world.
