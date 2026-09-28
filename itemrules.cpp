#include "itemrules.h"

#include "textures/items.h"

namespace ItemRules {

namespace {

// Vanilla 1.4 uses, per material, for every tool and weapon.
constexpr int WoodUses = 59;
constexpr int StoneUses = 131;
constexpr int IronUses = 250;
constexpr int DiamondUses = 1561;
constexpr int GoldUses = 32;

bool isToolItem(uint8_t item)
{
    return (item >= static_cast<uint8_t>(ItemTexture::WOODEN_SWORD) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_SWORD))      // swords
        || (item >= static_cast<uint8_t>(ItemTexture::WOODEN_SHOVEL) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_SHOVEL))    // shovels
        || (item >= static_cast<uint8_t>(ItemTexture::WOODEN_PICKAXE) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_PICKAXE))  // pickaxes
        || (item >= static_cast<uint8_t>(ItemTexture::WOODEN_AXE) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_AXE))          // axes
        || (item >= static_cast<uint8_t>(ItemTexture::WOODEN_HOE) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_HOE));         // hoes
}

bool isArmorItem(uint8_t item)
{
    return (item >= static_cast<uint8_t>(ItemTexture::LEATHER_HELMET) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_HELMET))
        || (item >= static_cast<uint8_t>(ItemTexture::LEATHER_CHESTPLATE) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_CHESTPLATE))
        || (item >= static_cast<uint8_t>(ItemTexture::LEATHER_LEGGINGS) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_LEGGINGS))
        || (item >= static_cast<uint8_t>(ItemTexture::LEATHER_BOOTS) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_BOOTS));
}

} // namespace

int maxStackSize(BLOCK_WDATA stack)
{
    return isDamageable(stack) ? 1 : 64;
}

bool isDamageable(BLOCK_WDATA stack)
{
    if(getBLOCK(stack) != BLOCK_ITEM)
        return false;

    const uint8_t item = getITEMDATA(stack);
    return isToolItem(item) || isArmorItem(item)
        || item == static_cast<uint8_t>(ItemTexture::SHEARS)
        || item == static_cast<uint8_t>(ItemTexture::FLINT_AND_STEEL)
        || item == static_cast<uint8_t>(ItemTexture::FISHING_ROD)
        || item == static_cast<uint8_t>(ItemTexture::FISHING_ROD_CAST)
        || item == static_cast<uint8_t>(ItemTexture::BOW_PULLING_1)
        || item == static_cast<uint8_t>(ItemTexture::BOW_PULLING_2)
        || item == static_cast<uint8_t>(ItemTexture::BOW_PULLING_3);
}

Tool toolKind(BLOCK_WDATA stack)
{
    if(getBLOCK(stack) != BLOCK_ITEM)
        return Tool::None;

    const uint8_t item = getITEMDATA(stack);
    if(item >= static_cast<uint8_t>(ItemTexture::WOODEN_SWORD) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_SWORD))
        return Tool::Sword;
    if(item >= static_cast<uint8_t>(ItemTexture::WOODEN_SHOVEL) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_SHOVEL))
        return Tool::Shovel;
    if(item >= static_cast<uint8_t>(ItemTexture::WOODEN_PICKAXE) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_PICKAXE))
        return Tool::Pickaxe;
    if(item >= static_cast<uint8_t>(ItemTexture::WOODEN_AXE) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_AXE))
        return Tool::Axe;
    if(item >= static_cast<uint8_t>(ItemTexture::WOODEN_HOE) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_HOE))
        return Tool::Hoe;
    if(item == static_cast<uint8_t>(ItemTexture::SHEARS))
        return Tool::Shears;
    if(item == static_cast<uint8_t>(ItemTexture::FLINT_AND_STEEL))
        return Tool::FlintAndSteel;
    if(item == static_cast<uint8_t>(ItemTexture::FISHING_ROD) || item == static_cast<uint8_t>(ItemTexture::FISHING_ROD_CAST))
        return Tool::FishingRod;
    if(item == static_cast<uint8_t>(ItemTexture::BOW_PULLING_1)
       || item == static_cast<uint8_t>(ItemTexture::BOW_PULLING_2)
       || item == static_cast<uint8_t>(ItemTexture::BOW_PULLING_3))
        return Tool::Bow;

    return Tool::None;
}

const char *toolName(Tool tool)
{
    switch(tool)
    {
    case Tool::Pickaxe:       return "Pickaxe";
    case Tool::Axe:           return "Axe";
    case Tool::Shovel:        return "Shovel";
    case Tool::Sword:         return "Sword";
    case Tool::Hoe:           return "Hoe";
    case Tool::Shears:        return "Shears";
    case Tool::FlintAndSteel: return "Flint and Steel";
    case Tool::Bow:           return "Bow";
    case Tool::FishingRod:    return "Fishing Rod";
    default:                  return "Item";
    }
}

int maxDamage(BLOCK_WDATA stack)
{
    if(getBLOCK(stack) != BLOCK_ITEM)
        return 0;

    const uint8_t item = getITEMDATA(stack);

    // Every tool run and every armour run starts at a multiple of 16 and holds
    // the five materials in the same order, so the material is just the low
    // nibble: 0 wooden/leather, 1 stone/chainmail, 2 iron, 3 diamond, 4 golden.
    const uint8_t material = item % 16;

    // Tools and weapons: the material decides the number of uses.
    if(isToolItem(item))
    {
        if(material > 4)
            return 0;

        static const int uses[5] = { WoodUses, StoneUses, IronUses, DiamondUses, GoldUses };
        return uses[material];
    }

    // Armour: the material decides it as well, but each piece differs.
    if(isArmorItem(item))
    {
        uint8_t piece = 0; // helmet

        if(item >= static_cast<uint8_t>(ItemTexture::LEATHER_HELMET) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_HELMET))
            piece = 0;
        else if(item >= static_cast<uint8_t>(ItemTexture::LEATHER_CHESTPLATE) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_CHESTPLATE))
            piece = 1;
        else if(item >= static_cast<uint8_t>(ItemTexture::LEATHER_LEGGINGS) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_LEGGINGS))
            piece = 2;
        else
            piece = 3;

        // Rows are leather, chainmail, iron, diamond, golden — the order the
        // armour ids use in the atlas.
        static const int durability[5][4] = {
            { 55,  80,  75,  65 }, // leather
            { 165, 240, 225, 195 }, // chainmail
            { 165, 240, 225, 195 }, // iron
            { 363, 528, 495, 429 }, // diamond
            { 77,  112, 105, 91 },  // golden
        };
        if(material > 4)
            return 0;
        return durability[material][piece];
    }

    switch(static_cast<ItemTexture>(item))
    {
    case ItemTexture::SHEARS:          return 238;
    case ItemTexture::FLINT_AND_STEEL: return 64;
    case ItemTexture::FISHING_ROD:     return 64;
    case ItemTexture::FISHING_ROD_CAST: return 64;
    case ItemTexture::BOW_PULLING_1:
    case ItemTexture::BOW_PULLING_2:
    case ItemTexture::BOW_PULLING_3:   return 384;
    default: return 0;
    }
}

int pickaxeTier(BLOCK_WDATA stack)
{
    if(getBLOCK(stack) != BLOCK_ITEM)
        return 0;

    const uint8_t item = getITEMDATA(stack);
    if(item < static_cast<uint8_t>(ItemTexture::WOODEN_PICKAXE) || item > static_cast<uint8_t>(ItemTexture::GOLDEN_PICKAXE))
        return 0;

    switch(static_cast<ItemTexture>(item))
    {
    case ItemTexture::WOODEN_PICKAXE:  return 1;
    case ItemTexture::STONE_PICKAXE:   return 2;
    case ItemTexture::IRON_PICKAXE:    return 3;
    case ItemTexture::DIAMOND_PICKAXE: return 4;
    case ItemTexture::GOLDEN_PICKAXE:  return 5;
    default: return 0;
    }
}

int requiredPickaxeTierForDrop(BLOCK block)
{
    switch(block)
    {
    case BLOCK_STONE:
    case BLOCK_COBBLESTONE:
    case BLOCK_COAL_ORE:
    case BLOCK_FURNACE:
    case BLOCK_NETHERRACK:
        return 1; // Wooden+

    case BLOCK_IRON_ORE:
    case BLOCK_IRON:
        return 2; // Stone+

    case BLOCK_GOLD_ORE:
    case BLOCK_GOLD:
    case BLOCK_DIAMOND_ORE:
    case BLOCK_DIAMOND:
    case BLOCK_REDSTONE_ORE:
        return 3; // Iron+

    default:
        return 0;
    }
}

bool isPickaxeMinedBlock(BLOCK block)
{
    return requiredPickaxeTierForDrop(block) > 0;
}

uint8_t armorSlot(BLOCK_WDATA stack)
{
    if(getBLOCK(stack) != BLOCK_ITEM)
        return NoArmorSlot;

    const uint8_t item = getITEMDATA(stack);
    if(item >= static_cast<uint8_t>(ItemTexture::LEATHER_HELMET) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_HELMET))
        return HelmetSlot;
    if(item >= static_cast<uint8_t>(ItemTexture::LEATHER_CHESTPLATE) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_CHESTPLATE))
        return ChestSlot;
    if(item >= static_cast<uint8_t>(ItemTexture::LEATHER_LEGGINGS) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_LEGGINGS))
        return LegsSlot;
    if(item >= static_cast<uint8_t>(ItemTexture::LEATHER_BOOTS) && item <= static_cast<uint8_t>(ItemTexture::GOLDEN_BOOTS))
        return BootsSlot;

    return NoArmorSlot;
}

int attackDamage(BLOCK_WDATA stack)
{
    if(getBLOCK(stack) != BLOCK_ITEM)
        return HandAttackDamage;

    const uint8_t item = getITEMDATA(stack);

    // Every run of tools is five materials long and starts on a multiple of 16
    // (swords at 64, shovels at 80, pickaxes at 96, axes at 112, hoes at 128), so
    // the material is the item's own position inside its run.
    const uint8_t material = item % 16;
    if(material > 4)
        return HandAttackDamage;

    // Wooden, stone, iron, diamond, golden.
    static const int swords[5]   = { 4, 5, 6, 7, 4 };
    static const int axes[5]     = { 3, 4, 5, 6, 3 };
    static const int pickaxes[5] = { 2, 3, 4, 5, 2 };
    static const int shovels[5]  = { 1, 2, 3, 4, 1 };

    switch(toolKind(stack))
    {
    case Tool::Sword:   return swords[material];
    case Tool::Axe:     return axes[material];
    case Tool::Pickaxe: return pickaxes[material];
    case Tool::Shovel:  return shovels[material];
    default:
        // A hoe, a pair of shears, a bow held as a club: nothing a table gives.
        return HandAttackDamage;
    }
}

int armorPoints(BLOCK_WDATA stack)
{
    const uint8_t slot = armorSlot(stack);
    if(slot == NoArmorSlot)
        return 0;

    const uint8_t item = getITEMDATA(stack);
    const uint8_t material = item % 16; // 0..4 inside the piece's own run


    // Same row order as the durability table: leather, chainmail, iron, diamond, golden.
    static const int points[5][4] = {
        { 1, 3, 2, 1 }, // leather
        { 2, 5, 4, 1 }, // chainmail
        { 2, 6, 5, 2 }, // iron
        { 3, 8, 6, 3 }, // diamond
        { 2, 5, 3, 1 }, // golden
    };
    if(material > 4)
        return 0;
    return points[material][slot];
}

int durabilityPerBlockMined(BLOCK_WDATA stack)
{
    switch(toolKind(stack))
    {
    case Tool::Pickaxe:
    case Tool::Axe:
    case Tool::Shovel:
    case Tool::Hoe:
    case Tool::Sword:
    case Tool::Shears:
        return 1;
    default:
        return 0;
    }
}

int durabilityPerAttack(BLOCK_WDATA stack)
{
    switch(toolKind(stack))
    {
    case Tool::Sword:
        return 1;
    case Tool::Pickaxe:
    case Tool::Axe:
    case Tool::Shovel:
    case Tool::Hoe:
        return 2; // Vanilla doubles the wear when a tool is used as a weapon.
    default:
        return 0;
    }
}

int durabilityPerUse(BLOCK_WDATA stack)
{
    switch(toolKind(stack))
    {
    case Tool::FlintAndSteel:
    case Tool::FishingRod:
        return 1;
    case Tool::Bow:
        return 1;
    default:
        return 0;
    }
}

bool applyDamage(unsigned short &damage, int max_damage, int amount)
{
    if(max_damage <= 0 || amount <= 0)
        return false;

    const int worn = static_cast<int>(damage) + amount;
    if(worn >= max_damage)
    {
        damage = static_cast<unsigned short>(max_damage);
        return true;
    }

    damage = static_cast<unsigned short>(worn);
    return false;
}

int remainingDurability(int damage, int max_damage)
{
    if(max_damage <= 0)
        return 0;

    const int left = max_damage - damage;
    return left > 0 ? left : 0;
}

int armorDamageReductionPercent(int points)
{
    if(points <= 0)
        return 0;

    int percent = points * 4;
    if(percent > 80)
        percent = 80;
    return percent;
}

int reduceDamageWithArmor(int amount, int points)
{
    if(amount <= 0 || points <= 0)
        return amount;

    const int percent = armorDamageReductionPercent(points);
    const int reduced = amount - (amount * percent) / 100;
    return reduced > 0 ? reduced : 1; // armour never makes a hit harmless
}

bool dropPickupable(int age_ms)
{
    return age_ms >= DropPickupDelayMs;
}

bool dropExpired(int age_ms)
{
    return age_ms >= DropLifetimeMs;
}

int dropMsRemaining(int age_ms)
{
    if(age_ms >= DropLifetimeMs)
        return 0;
    return DropLifetimeMs - age_ms;
}

} // namespace ItemRules
