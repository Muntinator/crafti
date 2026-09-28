# Enchantments, experience and the effects they have

Crafti has Minecraft's enchantments: the same registry, the same offer algorithm,
the same anvil arithmetic and the same numbers for what each one is worth. The
rules live in `enchanting.h` / `enchanting.cpp`, which is pure — no engine header,
no state, no hardware — and the whole module is walked on the host by
`tests/enchanting_test.cc` (715,000 checks: 86,400 offers rolled, the anvil's
cases, and the effect table). This document says what is in the game, where each
piece is applied, and what is deliberately still missing.

## Where the state lives

An item's enchantments travel with the item, not with the inventory position:
`Inventory` keeps an `Enchanting::Set` per slot *and* one per worn armour piece,
so an enchanted sword is the same object when it is swapped, dropped, put in a
chest, or written to the save. Four enchantments fit on one item (`MaxPerItem`),
each packed into one byte — the id in the low five bits, the level in the top
three — which is why the whole of it fits beside a 16-bit stack.

The save format is **version 13**: 36 inventory slots and the four worn pieces
each carry their set after the stack they belong to. The sets of a stack that is
not there are cleared, so a tool that passes through the crafting grid or the
furnace does not come back enchanted.

## Getting enchantments: `/enchant`

The enchanting table's rules are reachable from the console today; the block
itself is not in the world yet (see *Not in the build* below).

```
/enchant            the three offers for the item in hand, and the bookshelf count
/enchant 1|2|3      take that offer, spending its levels and its lapis lazuli
```

Offers are the vanilla roll: `base = randomInt(1,8) + power/2 + randomInt(0,power)`
scaled a third / two thirds / all of it for the three buttons, where `power` is
the bookshelves around the player capped at 15, plus the item's own enchantability
and a triangular ±15% wobble. That is why a wall of bookshelves turns a level-3
offer into a level-30 one, and why two diamond picks do not offer the same spells.

An offer costs its levels **and** lapis lazuli — one, two and three, as vanilla
charges for the three buttons — and the game has a real lapis item (id 142), found
in dungeon, ruin and temple chests. The levels are vanilla's own curve
(`Survival::xpForNextLevel`: 7 + 2 a level, then 5, then 9), so a level-30 offer
costs what it costs in Minecraft.

## What each enchantment does in play

| Enchantment | What the game does with it | Where |
| --- | --- | --- |
| Efficiency | mining progress per swing is `(level² + 2)` times a bare hand, so level 5 is 27× | `worldtask.cpp`, the mining loop |
| Silk Touch | the block drops itself instead of its usual drop | `worldtask.cpp`, block breaking |
| Fortune | one roll per level of the extra-drop chance, so Fortune III really drops more than I | `worldtask.cpp`, block breaking |
| Unbreaking | the roll that keeps a tool alive on each use | `worlditems.cpp`, `wearHeldItem()` |
| Sharpness | +½ damage point per level, against everything | `Enchanting::meleeDamage` |
| Smite / Bane of Arthropods | the same bonus, but only against the kind of mob they name | `Enchanting::meleeDamage` |
| Knockback | +4 of throw per level on the hit mob | each mob's `applyMeleeDamage` |
| Fire Aspect | 80 ticks of burning per level: the mob takes a damage point a second and is drawn glowing orange | each mob's `update()` / `render()` |
| Looting | one more roll of that mob's drop table per level | each mob's death, in `update()` |
| Protection, Fire/Blast/Projectile Protection | a second damage reduction after the armour's own, capped separately at 80% | `WorldTask::armorProtectionPoints`, `applyDamage` |
| Feather Falling | 20% off fall damage per level, up to 80%, applied to the fall itself | `worldtask.cpp`, landing |
| Respiration | each breath has a `level / (level + 1)` chance of not being spent | `Survival::respirationSavesBreath` |
| Aqua Affinity | cancels the 5× penalty vanilla puts on mining under water | `worldtask.cpp`, the mining loop |

Everything in that table is a number from `enchanting.h`; no engine file computes
one of its own, which is what keeps the host tests meaningful.

## Melee damage

Weapons used to all hit for the same amount. They now follow vanilla's table
(`ItemRules::attackDamage`, in half-hearts), which is what makes a sword worth
carrying:

| | fist | wooden | stone | iron | diamond | golden |
| --- | --- | --- | --- | --- | --- | --- |
| sword | — | 4 | 5 | 6 | 7 | 4 |
| axe | — | 3 | 4 | 5 | 6 | 3 |
| pickaxe | — | 2 | 3 | 4 | 5 | 2 |
| shovel | — | 1 | 2 | 3 | 4 | 1 |
| hoe / anything else | 1 | 1 | 1 | 1 | 1 | 1 |

A diamond sword (7) takes a 20-health creeper down in three hits; a fist takes
twenty. `WorldTask::heldMeleeDamage()` adds the weapon's Sharpness and the
Strength effect on top, and a landed hit costs hunger the way vanilla charges it.

Experience is earned the way Minecraft earns it: ores give their own range when
mined (`Survival::xpBlockRange`), taking a smelted item out of a furnace gives a
point per item, and killing a mob gives its kind's worth — 5 for a creeper, 2 for
an animal, and none for a villager, which drops nothing to be worth anything.

## The anvil

`Enchanting::combine()` is the anvil, with vanilla's rules: two of the same item
add their durability plus 12% of the maximum, the same enchantment on both
deepens by one level up to its own cap, different enchantments merge, everything
costs its rarity price per level, each prior use of the item doubles its
contribution and "Too Expensive" is 39. `repairedDamage()`, `repairUnitsNeeded()`
and `repairMaterialFor()` are the repair half of the same table. All of it is
tested — and none of it is reachable in play yet, because there is no anvil block
and no anvil screen.

## Not in the build

- **The enchanting table block and its screen.** The rules, the offers, the
  lapis and the bookshelf counting are all in place and used by `/enchant`; what
  is missing is the block itself and a screen with the three buttons. Two things
  have to be decided before it can be built the vanilla way: the texture packs
  ship no enchanting-table GUI sheet (every container panel in this build except
  the furnace and the crafting table is drawn from rectangles, so that is a
  convention that already exists), and vanilla's recipe — a book, two diamonds and
  four **obsidian** — cannot be satisfied, because obsidian has no source here.
  The packs do have a dark tile for it, but this build's water and lava do not
  flow, so vanilla's water-over-a-lava-source does not exist to make the block.
- **The anvil block and its screen**, which is what the arithmetic above is for.
- **Bows**: there is no arrow, so Power, Punch, Flame and Infinity have nothing to
  change yet. Their numbers are in the table and tested.
- **Thorns**: nothing in the game damages the player from an attacker it can be
  reflected onto — a creeper that explodes is already dead — so the reflect has no
  target. The number is in the table and tested.
- **Smite and Bane of Arthropods**: there are no undead or arthropod mobs, so both
  are worth their bonus against nobody. `Enchanting::TargetKind` is what a new mob
  would set, and the melee rule already reads it.
- **Brewing**: no potion exists, so most of the effects the survival rules compute
  (Speed, Strength, Haste, Jump Boost) have no source yet. Strength is applied in
  melee and the others are read where they belong; they are waiting for a potion.

## Where it is put together

| File | What lives there |
| --- | --- |
| `enchanting.h` / `enchanting.cpp` | the registry, the offer roll, the anvil, the effect numbers, the melee and protection rules. Pure, unit tested |
| `tests/enchanting_test.cc` | the offers at every bookshelf power, the exclusions, the anvil's cases, every effect, the wired melee and protection sums, and the cost per roll |
| `inventory.h` / `inventory.cpp` | the sets per slot and per worn piece, and their movement with the stack |
| `itemrules.h` / `itemrules.cpp` | the weapon damage table and the armour points the protection is added to |
| `survival.h` / `survival.cpp` | the experience curve, the experience a mob or an ore is worth, and the breath Respiration saves |
| `worldcommands.cpp` | `/enchant`, the bookshelf count and the lapis spend |
| `worldsurvival.cpp` | wearing the numbers: melee damage, protection, Respiration, and the damage pipeline they feed |
| `worldtask.cpp` | the mining side (Efficiency, Silk Touch, Fortune, Aqua Affinity), Fire Aspect's ticks, fall damage and Feather Falling |
| `*entity.cpp` | burning, extra knockback, extra drops and kill experience |
| `task.cpp` | the enchantments in the save file (version 13) |
