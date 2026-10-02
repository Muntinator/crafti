#ifndef ITEMRULES_H
#define ITEMRULES_H

#include <cstdint>

#include "terrain.h"

/**
 * Item durability, tool tiers, armour and ground-drop lifetime.
 *
 * Pure rules: no engine state, no nGL rendering, no globals. Every function is a
 * function of the stack value alone, so the whole file is exercised by
 * tests/itemrules_test.cc on the host. The tables mirror Minecraft 1.4's
 * balance, so the numbers on the calculator are the ones a player already knows.
 *
 * Durability lives next to the stack, not inside it: a BLOCK_WDATA has no room
 * for a damage counter (both bytes are used by the block id and the item id).
 * The inventory and the chest store each keep a parallel array, which is what
 * the damage parameter of the helpers below is.
 */
namespace ItemRules {

/** What a stack is used for. Only tools and weapons take wear. */
enum class Tool : uint8_t {
    None = 0,
    Pickaxe,
    Axe,
    Shovel,
    Sword,
    Hoe,
    Shears,
    FlintAndSteel,
    Bow,
    FishingRod,
};

/** Armour slot, in the order of the vanilla inventory: helmet, chest, legs, boots. */
enum ArmorSlot : uint8_t {
    NoArmorSlot = 0xFF,
    HelmetSlot = 0,
    ChestSlot = 1,
    LegsSlot = 2,
    BootsSlot = 3,
    ArmorSlotCount = 4,
};

/** Vanilla stack limits: nothing that wears out stacks. */
int maxStackSize(BLOCK_WDATA stack);

/** True when the stack can be worn down at all. */
bool isDamageable(BLOCK_WDATA stack);
/** Maximum damage (uses) the stack takes before breaking; 0 when it cannot break. */
int maxDamage(BLOCK_WDATA stack);

Tool toolKind(BLOCK_WDATA stack);
const char *toolName(Tool tool);

/**
 * Pickaxe harvest tier, 1 = wooden … 5 = golden. Anything that is not a pickaxe
 * is 0, which is also the "no pickaxe" tier the mining code already used.
 */
int pickaxeTier(BLOCK_WDATA stack);

/** True for blocks that only drop with a pickaxe of a high enough tier. */
int requiredPickaxeTierForDrop(BLOCK block);
bool isPickaxeMinedBlock(BLOCK block);

/**
 * Melee damage of the stack, in the same half-hearts the mobs' health is
 * counted in (vanilla's damage points). Vanilla 1.4's table: a fist does one
 * point, and a tool does what its material and its kind are worth, so a diamond
 * sword (7) takes a 20-point creeper down in three hits and a bare hand in
 * twenty. Tools used as weapons are worth less than the sword of the same
 * material, which is why an axe is worth taking to a fight but not as good as
 * the blade it was made beside.
 */
int attackDamage(BLOCK_WDATA stack);

/** What an empty hand does, and the floor of the table above. */
constexpr int HandAttackDamage = 1;

/** Armour slot the stack belongs to, or NoArmorSlot. */
uint8_t armorSlot(BLOCK_WDATA stack);
/** Armour points the piece contributes, 0 when it is not armour. */
int armorPoints(BLOCK_WDATA stack);

/** Durability spent when the stack is used as a tool. 0 when it should not wear. */
int durabilityPerBlockMined(BLOCK_WDATA stack);
int durabilityPerAttack(BLOCK_WDATA stack);
/** Durability spent by the held item on every 1/20 s while it is burning/working. */
int durabilityPerUse(BLOCK_WDATA stack);

/**
 * Applies wear. Returns true when the tool broke, which is when the item itself
 * must be removed from the slot: the caller drops nothing for a broken tool.
 */
bool applyDamage(unsigned short &damage, int max_damage, int amount);

/** Damage left in the item, never negative. */
int remainingDurability(int damage, int max_damage);

/**
 * Vanilla armour: 4% damage reduction per point, capped at 80%. `points` is the
 * total of the worn pieces, and the sources that bypass armour are filtered by
 * the caller (survival.h already names them).
 */
int armorDamageReductionPercent(int points);
int reduceDamageWithArmor(int amount, int points);

/** Durability each worn piece loses when the player is hit. */
constexpr int ArmorDamagePerHit = 1;

// ---------------------------------------------------------------- ground drops

/**
 * A dropped stack cannot be picked up for a moment (vanilla's pickup delay), and
 * after five minutes it vanishes. Both are real milliseconds rather than ticks:
 * the world's simulation tick is 33 ms on a desktop build and 300 ms on the
 * calculator, so counting ticks would make an item last nine times longer on the
 * CX. 10 ticks and 6000 ticks of vanilla's 1/20 s are 500 ms and five minutes.
 */
constexpr int DropPickupDelayMs = 500;
constexpr int DropLifetimeMs = 5 * 60 * 1000;

bool dropPickupable(int age_ms);
bool dropExpired(int age_ms);
/** Milliseconds left before the drop disappears, 0 once it is due. */
int dropMsRemaining(int age_ms);

} // namespace ItemRules

#endif // ITEMRULES_H
