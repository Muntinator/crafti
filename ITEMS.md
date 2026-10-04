# Dropped items, chests and durability

Three things a survival sandbox needs that Crafti did not have: stacks that lie in
the world, somewhere to put them, and tools that wear out. All three keep the
existing shape of the code: the rules are pure modules that the host tests drive,
and the engine files only hook them up.

## The item atlas (`textures/items_texture.h`)

The item icons are the official Minecraft 1.17.1 textures, one file per item
under `textures/item/` in vanilla's own layout, arranged into the atlas by
`tools/textures/gen_item_textures.py`. The script reads the `ItemTexture` enum in
`textures/items.h` and puts each item's texture at the coordinate its value
implies, so the enum and the art cannot drift apart, and `itemicons.cpp` samples
`(index % 16, index / 16)` out of the 256x256 result. Regenerate with

    python3 tools/textures/gen_item_textures.py

Two entries are not a plain copy: the bed, which vanilla draws as a 3D model and
so has no 2D icon (composed here from the same red-bed unwrap the block atlas
uses), and the compass and clock, which vanilla animates over 32 and 64 frames
and this engine draws as a single still.

## Durability (`itemrules.h/.cpp`)

Every number comes from Minecraft 1.4, the balance the tables were written
against, so a player who knows that version already knows these:

| Material | Tools | Helmet | Chest | Legs | Boots |
| --- | --- | --- | --- | --- | --- |
| Wooden / leather | 59 | 55 | 80 | 75 | 65 |
| Stone / chainmail | 131 | 165 | 240 | 225 | 195 |
| Iron | 250 | 165 | 240 | 225 | 195 |
| Diamond | 1561 | 363 | 528 | 495 | 429 |
| Golden | 32 | 77 | 112 | 105 | 91 |

Shears take 238 uses, flint and steel and the fishing rod 64, the bow 384. Armour
points (1/3/2/1 for leather up to 3/8/6/3 for diamond) reduce damage by 4% per
point, capped at 80%, and a hit wears every worn piece once.

Nothing that wears out stacks, which is also what the inventory now uses: a
pickaxe never merges into another pickaxe, and a damaged one never merges at all.
The wear is stored *next to* the stack, not inside it — a `BLOCK_WDATA` has no
room, both of its bytes are the block id and the item id — so the inventory, the
chest and a dropped stack each carry a `damage` for the stack they hold.

**Where the wear is spent**

- breaking a block with a pickaxe, axe, shovel, hoe, sword or shears: 1
- landing a hit: 1 with a sword, 2 with any other tool (vanilla doubles it)
- being hit: 1 on every worn armour piece

A tool that reaches its limit breaks: the slot is emptied, nothing is dropped,
`"Iron Pickaxe broke!"` is shown and `random/break.ogg` plays. The hotbar, the
inventory, the chest screen and the HUD item name all show how much is left
(`Iron Pickaxe (37/250)`).

**Wearing armour**: use a piece with `7` (the place/interact key) and it goes on,
swapping out whatever was in that slot. The four armour widgets are on the player
inventory screen, in the strip to the left of the window: click one to take the
piece onto the cursor, or click it holding the matching piece to put it on. The
HUD draws the armour bar above the hearts, and worn pieces are saved.

**Not done** (deliberate, since there is no way to use them yet): the crafting
grid and the furnace do not carry a stack's wear — a tool that passes through
either comes back unused. Bows, flint and steel and the fishing rod have their
durability tables but nothing spends them yet.

## Dropped items (`grounddrops.*`)

A dropped stack falls, gets pushed around by the blocks it lands on, and is
pulled into the inventory when the player walks close (position and magnet code
that was already there). New in this pass:

- **The wear travels with it.** Breaking a half-used pickaxe off a chest floor
  puts a half-used pickaxe on the ground, and it goes back into the inventory
  with the same number of uses left.
- **Half a second of grace** before a drop can be collected or pulled in
  (`ItemRules::DropPickupDelayMs`), so the block you just broke does not snap into
  the inventory before you see it. The drop still falls normally.
- **Five minutes of life** (`ItemRules::DropLifetimeMs`), then it is gone. Both
  are *real milliseconds*, not ticks: the calculator's logic loop runs at 300 ms a
  step and the desktop at 33 ms, and counting steps would make an item last nine
  times longer on the CX.
- **A cap of 256 drops** in the world, dropping the oldest first, so nothing can
  exhaust the calculator's memory.

Drops are not saved: a world that is left mid-pickup should not come back
littered with the contents of its chests.

## Chests (`cheststore.*`)

`BLOCK_CHEST` is block id 45 — the id was unused and the new block is appended to
the normal range, so every existing save still reads. Vanilla has no 2D chest
texture to copy (the chest is a block entity, so its texture is a model unwrap),
and the atlases the game ships (an external 512² file and two embedded 256² ones)
had no art for it either. `tools/textures/gen_block_textures.py` therefore cuts a
wooden lid (top), a plain side and a front with hinges and a latch out of
`entity/chest/normal.png` and places them in **row 4 of the atlas, which was
unused**: the world, the inventory icon, the block list and the drop entity all
get them without a hand-drawn tile.

Chests are block entities. The world stores only the block; `cheststore.h` owns
the contents, keyed by block position, so there is no per-screen copy of the
inventory that could drift out of step with the world.

- **27 slots** per chest, **54** for a double chest. Place a second chest next to
  an existing one and they pair: the chest that was already there keeps slots
  0..26 and the new one owns 27..53, whichever order they were placed in.
- Breaking one half drops **that half's** items and leaves the other standing as a
  single chest with its own items, which is what vanilla does.
- Each half physically holds its own 27 slots: an item that goes in through the
  half you are standing at fills that half first and only spills into the other
  when it is full. A slot holds 64, as everywhere else.
- **The screen** (`inventorychest.cpp`) is drawn from rectangles: the panel, the
  slot cells, then the items with their counts and wear bars, then the player's 36
  slots below, laid out like the player inventory so a stack does not move when the
  screen changes. Left click takes a stack, right click takes half or places one,
  and a stack that is picked up, put down or swapped keeps its wear.
- **A chest can never eat an item**: the screen reads and writes the store
  directly, so closing it (with `A` or the menu key, `Shift`) needs no "save the
  container" step and cannot lose what is on the cursor.
- Blowing a chest up with TNT scatters its contents instead of deleting them.
- A chest the **world** generated (a dungeon, a ruin or a temple) is filled with
  that structure's loot the first time it is opened or broken, so an untouched one
  costs neither memory nor save space. The loot is a pure function of the world
  seed, so it is the same however the chunk was reached. See STRUCTURES.md.

## Save file

Version 10 adds, in this order after the day/night clock:

1. `unsigned short damage[36]` — the wear of every inventory slot
2. the four worn armour slots (block, count, wear)
3. `unsigned int chest_count` and then one fixed-size record per chest
   (position, other half, slot count, 54 stacks of block/count/wear)

All of it is written before the world section, which ends the file with a chunk
list, exactly like the clock in version 9. Versions 4..9 still load: they simply
arrive with no wear, no armour and no chests.

## Tests

`tests/itemrules_test.cc` (347 checks) pins down every durability and armour
number, the tool tiers, the wear each use costs, breaking, the armour reduction
and the drop lifetimes. It also guards the bug this file's `getITEMDATA` exists
for: the hoes (128..132) and `BOW_PULLING_3` (133) are above 127 and would be read
as helmets and flint and steel by `getBLOCKDATA`.

`tests/cheststore_test.cc` (169 checks) drives placement, filling, a full chest,
single and double chest breaking from either half, the 64-per-slot limit, damaged
tools not merging, and a byte-exact save/load round trip (including refusing a
truncated buffer and leaving the store empty rather than half-loaded).
