# Livestock

Crafti has passive livestock: **cows, pigs, sheep, chickens, horses, wolves,
mooshrooms and donkeys**. They spawn around the player from the generated
terrain, wander and graze, breed when fed, take damage, drop loot and play the
sounds from the audio pack. Chickens are now one species of this system (the old
standalone chicken mob was folded in).

The wolf, the mooshroom and the donkey are the ones the vanilla asset tree added
last: the wolf has its own model (and its own idle calls, from the wolf sounds
the pack already carried), the mooshroom is the cow's boxes on the official
`red_mooshroom.png`, and the donkey is the horse's boxes on the official
`donkey.png`. Wolves hunt in forests, mooshrooms keep to the grass, and donkeys
graze with the horses.

## Behaviour

- **Idle / wander** – a three-state timer AI: mostly stand and graze, otherwise
  pick a random heading and walk at the species' speed. A blocked step makes the
  animal hop over one-block obstacles and re-pick a direction.
- **Flee** – a hit makes the animal run directly away from the player for ~4 s.
- **Health / drops** – hit points per species; on death it drops its loot
  (cow: raw beef + leather, pig: porkchop, sheep: white wool, chicken: raw
  chicken, horse: leather, wolf: leather, mooshroom: raw beef + leather,
  donkey: leather) through the existing world-drop system. A vanilla wolf drops
  nothing at all, so it drops its pelt as leather, which is what the horse and
  the donkey do as well.
- **Breeding** – feed an adult wheat seeds or an apple (aim at it and press the
  place key) to put it in love; two animals in love that meet make a baby, which
  grows up over ~2 minutes and renders at 60% scale.
- **Sounds** – idle calls and hurt calls through `GameAudio::mobSound`, chosen by
  species and attenuated by distance to the player.

## Spawning

Spawning is procedural and biome-weighted. There is no dedicated biome map, so
the biome is classified from what the generator already put down
(`livestockspecies.*`):

| biome | how it is classified |
| --- | --- |
| Grassland | grass surface, no forest noise |
| Forest | grass surface where the tree-placement noise is high |
| Desert | sand surface away from water |
| Shore | water in the column or in an adjacent column |
| Water | water surface (never spawns) |

Each biome has per-species weights, so deserts are mostly empty, forests favour
sheep, chickens and wolves, and the mooshroom and the donkey only turn up on
grassland. Spawns keep 8–16 blocks from the player, only land on
a real terrain surface with air above, and only inside the loaded chunk radius.

## CX budget

- One flat `std::vector<LivestockEntity>` of fixed structs, no pointers and no
  allocation in the update path.
- Population cap `Livestock::maxEntities()`: **10 on the CX**, 24 on desktop.
- Animals further than `despawnDistanceBlocks()` (56) from the player despawn,
  unless they are mid-breeding.
- Spawn attempts happen every 30 ticks while the world is filling, then every 90.
- Rendering culls beyond 48 blocks and draws one `glBegin/glEnd` batch per
  species, so at most a handful of texture binds per frame.

## Models and textures

The animals draw the vanilla 1.17.1 box models. `Livestock::model()` returns the
box tables, written in the terms `mobmodel.h` describes and transcribed from the
client's `ModelCow`, `ModelQuadruped`, `ModelSheep1`, `ModelHorse` and `ModelWolf`;
`Mob::draw()` poses them. Minecraft ships no entity models in its assets, so the
geometry is a transcription -- but the *skins* are the official ones, checked in
under `textures/entity/` exactly as the vanilla asset tree names them.

A mooshroom and a donkey are skin variants in vanilla too, so `model()` hand them
the cow and the horse tables instead of a second copy of the same boxes. The wolf
is the only model here whose numbers move: the tables hold whole model units and
the wolf's head pivot sits at y = 13.5 with the legs at x = -2.5 / 0.5, so the
head drops the half and the legs move to -3 / 1 -- still symmetric about the same
x = -1 the vanilla offsets are built around.

`textures/{creeper,cow,pig,sheep,chicken,horse,villager,wolf,mooshroom,donkey}.h`
are generated from those PNGs by `python3 tools/textures/gen_entity_textures.py`.
Regenerate them if a skin changes. The chicken still draws itself, because its
model carries a fixed body rotation and flapping wings; its boxes are the vanilla
ones already.

## Tests

`make -C tests` runs `livestock_test`, which checks the per-species stats, the
drop tables against the real item ids, that every box of every vanilla model
lands inside the skin it is drawn with and that each model's legs stand on the
ground, and the biome weights/spawn selection (including the empty-desert and
empty-water cases).

`tools/pcsim/mobs.txt` is the visual counterpart: it builds a flat stage, summons
one animal per heading and captures each view with and without the animal, so the
three new silhouettes can be compared against each other.

Villages and their inhabitants are a separate system; see `VILLAGE.md`.

