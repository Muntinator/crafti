# Debug commands

Crafti has an in-game console. Press **`/`** (the divide key) in the world to open
it, type a line, and press **Enter** to run it. **Esc** closes the console and
hands the screen back to the world; nothing the player was doing is lost, because
the world simply stops being simulated while the console is up.

| Key | What it does |
| --- | --- |
| `/` | opens the console over the world |
| letters, digits, space | type |
| `.` `,` `-` `+` `/` `*` `:` `~` `(` `)` `^` `=` `'` | the punctuation a line needs, each on its own key |
| Enter | runs the line |
| Del | rubs out one character |
| Up / Down | walks back and forward through what has been run |
| Esc | closes the console |

Every character a command needs has its own key, so nothing has to be typed with
a modifier. The line scrolls rather than overflowing when it gets long, and the
hint line under the prompt lists the commands the current word could still
become, so the whole language can be found by typing one letter. The console
remembers the last eight commands; a typo is edited by pressing Up and retyping
rather than by starting again.

Commands and the names of blocks and items are matched without regard to case,
spaces, underscores or hyphens, so `give cobble_stone`, `give cobble-stone` and
`give Cobblestone` are the same command. A name with a space in it is typed
without the space: `give "redstone torch"` and `give redstonetorch` both work.

## The commands

```
/give <block|item> [count]            Put a stack in the inventory
/time <set|add> <value>               Move the day/night clock
/weather <clear|rain|thunder|snow> [s]  Force the weather for a while
/teleport <x> <y> <z>                 Move the player, ~ for relative
/setblock <x> <y> <z> <block> [data]  Replace one block
/fill <x1> <y1> <z1> <x2> <y2> <z2> <b>  Fill a box with a block
/summon <mob> [x] [y] [z]             Spawn a mob near the player
/enchant [1|2|3]                      The table's offers, or take one
/seed                                 Show the world seed
/gamemode <survival|creative>         Switch the play mode
/help [command]                       List the commands, or explain one
```

`/tp` is `/teleport` and `/gm` is `/gamemode`.

### /give

Names come from the tables the game itself displays, so anything the block list
or the inventory can name can be given: `stone`, `cobblestone`, `oak` is not a
name but `log` is, `planks`, `glowstone`, `chest`, `torch`, `redstone torch`,
`diamond sword`, `golden apple`, `bread`, `bucket`... A numeric block id works
too (`give 45` is a chest). A stack is capped at the item's own limit, and a full
inventory drops the stack at the player's feet and says so instead of losing it.

### /time

`set` moves the clock, `add` moves it forward by that much, and the word may be
left out (`/time noon` means `/time set noon`). Times are named or numeric:

| Named | Ticks | Numeric units |
| --- | --- | --- |
| `sunrise`, `dawn`, `day` | 0 | `t` — ticks |
| `morning` | 1000 | `s` — seconds of game time |
| `noon`, `midday` | 6000 | `m` — minutes |
| `afternoon` | 9000 | `h` — hours |
| `sunset` | 12000 | `d` — days |
| `dusk`, `evening` | 13000 | |
| `night` | 14000 | |
| `midnight` | 18000 | |

A bare number is a tick count, and the units combine: `1d6h30m` is half past six
in the morning. Times wrap around a 24000-tick day, and the day counter advances
when the clock passes dawn. The clock is part of the save file, so it is where it
was left when the world is reopened.

### /weather

Forces clear, rain or thunderstorms for the given number of *real* seconds (600
by default, up to four game days). While it lasts it is always at full strength,
and the debug screen says `forced` beside it. When it runs out, or when the world
is reopened, the natural weather from the seed and the clock comes back: an
override is a look-at-the-rain switch and is deliberately not saved. The command
refuses while the day/night or weather settings are off, because there is no
clock for a forced spell to end against.

### /teleport

Whole blocks, and `~` is relative: `/teleport ~ ~10 ~` is ten blocks up from
where the player is. The destination must have room for the head and the feet —
a spot inside rock is refused rather than accepted, because on the calculator a
player who lands in stone suffocates before they can tell what happened. Chunks
around the destination are loaded before the next frame, so a long teleport does
not fall through unloaded air.

### /setblock and /fill

`/setblock 10 30 -4 glowstone` replaces one block; `air` clears it. A chest that
is written becomes an empty container, one that is replaced drops what it held,
and a furnace forgets its cooking state, exactly as breaking the block by hand
would.

`/fill` takes two opposite corners and a block, and is capped at **2048 blocks**.
Every block written costs a chunk rebuild, which is why the cap is there: at 2048
the worst case is a handful of frames, where Minecraft's 32768 would freeze the
calculator. The box is clamped to the world vertically so a fill that runs past
the sky still builds the part that fits.

### /summon

`chicken`, `cow`, `pig`, `sheep`, `horse` and `creeper`. With no coordinates the
mob appears two blocks in front of the player; with coordinates it appears exactly
there. Villagers are deliberately not summonable: a villager is bound to the
village it belongs to (its home, its bed and its trade all hang off it), so one
dropped in open country would have nowhere to be. There is no humanoid mob either
-- the player and the villagers are the only people in Muntcraft, and nothing can
summon a Steve.

Animals are refused once the population is already at its limit.

### /gamemode

`survival` (the default) or `creative`. Creative is the debug mode: nothing can
hurt the player, hunger and breath never move, blocks break at a touch, and
placing one does not spend it. It is stored in the save file (format version 11),
so a world opened again is still in the mode it was left in. A new world always
starts in survival.

### /enchant

With no argument it lists the three enchanting-table offers for the item in hand
and how many bookshelves are around the player; `/enchant 1`, `2` or `3` takes
that offer, spending its levels and its lapis lazuli. The offers are the vanilla
algorithm and a wall of bookshelves is what turns a level-3 offer into a level-30
one. See `ENCHANTING.md` for what every enchantment is worth in play.

### /seed

The world seed in decimal and hexadecimal, and the day counter. Two players with
the same seed have the same terrain, biomes, caves, villages and structures.

## The debug screen

Turn on **Show FPS** in Settings for a debug readout in the top-left corner:

```
Day 3 06:42 Survival
X 12 Y 34 Z -8
Rain 23% sky 102
fps 28 mobs 14
```

The clock line is printed by the clock's own formatter, so it always agrees with
`/time`. The coordinates are whole blocks, which is what `/teleport` and
`/setblock` take, so they can be read off and typed back. `sky` is the sky light
level the day/night tint is made from, which is how a dark screen at noon is told
from a dark screen at night. The coordinate readout in Settings stands aside
while the debug screen is on, since it shows the same numbers.

## How it is put together

| File | What lives there |
| --- | --- |
| `command.h` / `command.cpp` | the language: tokenising, quoting, numbers, names, times, weather, gamemodes, the `~` shorthand, the `/fill` cap and the command table. Pure, and unit tested on the host (`tests/command_test.cc`) |
| `worldcommands.h` / `worldcommands.cpp` | what a command does to the world, through the same calls the rest of the game uses |
| `commandtask.h` / `commandtask.cpp` | the console: the line, the log, the history and the keys |
| `Task::textKeyPressed()` in `task.cpp` | the character a text key types, with the edge detection that stops a held key from repeating |
| `worldtask.h` / `worldweather.cpp` / `worldsurvival.cpp` | the play mode and the weather override, and the places creative mode is honoured |

Nothing in the console knows what a command means, and nothing in the world side
knows how a line was typed: a command can be run from the console, from a script
or from a future chat line and mean the same thing.
