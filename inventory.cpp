#include "inventory.h"

#include <cstdio>
#include <utility>

#include "texturetools.h"
#include "blockrenderer.h"
#include "font.h"
#include "itemicons.h"
#include "itemrules.h"

#include "textures/inventory.h"

Inventory current_inventory;

// The hotbar widget as the official gui/widgets.png draws it: nine 16-pixel slots
// on a 20-pixel pitch starting at (3,3) of a 182x22 bar, and the selected-slot
// frame at (0,22), 24x24.
static constexpr int hotbar_src_width = 182;
static constexpr int hotbar_src_height = 22;
static constexpr int hotbar_slot_src_left = 3;
static constexpr int hotbar_slot_src_top = 3;
static constexpr int hotbar_slot_src_size = 16;
static constexpr int hotbar_slot_src_pitch = 20;
static constexpr int selector_src_x = 0;
static constexpr int selector_src_y = 22;
static constexpr int selector_src_size = 24;

Inventory::Inventory()
{
}

int Inventory::hotbarScale()
{
    return SCREEN_WIDTH >= hotbar_src_width * 2 ? 2 : 1;
}

int Inventory::hotbarWidth() { return hotbar_src_width * hotbarScale(); }
int Inventory::hotbarHeight() { return hotbar_src_height * hotbarScale(); }
int Inventory::hotbarLeft() { return (SCREEN_WIDTH - hotbarWidth()) / 2; }
int Inventory::hotbarTop() { return SCREEN_HEIGHT - hotbarHeight() - 3; }
int Inventory::hotbarSlotSize() { return hotbar_slot_src_size * hotbarScale(); }

int Inventory::hotbarSlotX(int slot)
{
    return hotbarLeft() + (hotbar_slot_src_left + slot * hotbar_slot_src_pitch) * hotbarScale();
}

int Inventory::hotbarSlotY()
{
    return hotbarTop() + hotbar_slot_src_top * hotbarScale();
}

int Inventory::hotbarSelectorSize() { return selector_src_size * hotbarScale(); }

int Inventory::hotbarSelectorX(int slot)
{
    // Vanilla draws the frame one pixel up and to the left of the slot it rings.
    return hotbarLeft() + (hotbar_slot_src_left + slot * hotbar_slot_src_pitch - 1) * hotbarScale();
}

int Inventory::hotbarSelectorY()
{
    return hotbarTop() + (hotbar_slot_src_top - 1) * hotbarScale();
}

void Inventory::draw(TEXTURE &tex)
{
    drawTexture(inventory, tex, 0, 0, hotbar_src_width, hotbar_src_height,
                hotbarLeft(), hotbarTop(), hotbarWidth(), hotbarHeight());

    for(unsigned int i = 0; i < hotbar_slot_count; ++i)
    {
        const BLOCK_WDATA block = entries[i];
        if(counts[i] == 0 || getBLOCK(block) == BLOCK_AIR)
            continue;

        const int slot_x = hotbarSlotX(static_cast<int>(i));
        const int slot_y = hotbarSlotY();
        const int slot_px = hotbarSlotSize();

        if(getBLOCK(block) == BLOCK_ITEM)
        {
            drawItemIcon(block, tex, slot_x, slot_y, slot_px);
        }
        else
        {

    #ifdef _TINSPIRE
        const TextureAtlasEntry &icon_tex = global_block_renderer.materialTexture(block).resized;
        drawTexture(*terrain_resized, tex,
                icon_tex.left, icon_tex.top,
                icon_tex.right - icon_tex.left, icon_tex.bottom - icon_tex.top,
                slot_x, slot_y,
                slot_px, slot_px);
    #else
        if(getBLOCK(block) == BLOCK_DOOR)
        {
            const int door_w = 16;
            const int door_h = 32;
            // DoorRenderer applies an internal +4 x-offset.
            const int preview_x = slot_x + (slot_px - door_w) / 2 - 4;
            const int preview_y = slot_y + (slot_px - door_h) / 2;
            global_block_renderer.drawPreview(block, tex, preview_x, preview_y);
        }
        else
        {
            const int icon_w = 24;
            const int icon_h = 24;
            const int preview_x = slot_x + (slot_px - icon_w) / 2;
            const int preview_y = slot_y + (slot_px - icon_h) / 2;
            global_block_renderer.drawPreview(block, tex, preview_x, preview_y);
        }
#endif
        }

        char count_text[12];
        snprintf(count_text, sizeof(count_text), "%u", counts[i]);
        drawString(count_text, 0xFFFF, tex, slot_x + slot_px - 10, slot_y + 2);

        drawDurabilityBar(tex, block, damage[i], slot_x, slot_y, slot_px);
    }

    drawTexture(inventory, tex,
                selector_src_x, selector_src_y, selector_src_size, selector_src_size,
                hotbarSelectorX(current_slot), hotbarSelectorY(),
                hotbarSelectorSize(), hotbarSelectorSize());
}

/**
 * The little wear bar under a widget's item, the same one Minecraft draws: only
 * visible once the item has been used, and green to red as it runs out.
 */
void Inventory::drawDurabilityBar(TEXTURE &tex, BLOCK_WDATA block, unsigned short worn, int x, int y, int size)
{
    const int max_damage = ItemRules::maxDamage(block);
    if(max_damage <= 0 || worn == 0)
        return;

    int left = ItemRules::remainingDurability(worn, max_damage);
    if(left > max_damage)
        left = max_damage;

    int width = (size * left) / max_damage;
    if(width <= 0)
        width = 1;

    const int bar_y = y + size - 2;
    const int bar_h = size >= 24 ? 3 : 2;

    int red = 255;
    int green = 255;
    if(left * 2 < max_damage)
        red = (left * 2 * 255) / max_damage;
    else
        green = ((max_damage - left) * 2 * 255) / max_damage;

    const COLOR color = colorRGB(RGB{ red / 255.0f, green / 255.0f, 0.0f });

    // A filled bar (drawRectangle() would only outline it).
    for(int px = x; px < x + width; ++px)
    {
        if(px < 0 || px >= static_cast<int>(tex.width))
            continue;
        for(int py = bar_y; py < bar_y + bar_h; ++py)
        {
            if(py < 0 || py >= static_cast<int>(tex.height))
                continue;
            tex.bitmap[px + py * tex.width] = color;
        }
    }
}

unsigned int Inventory::height()
{
    return hotbarHeight();
}

BLOCK_WDATA Inventory::currentSlot() const
{
    if(counts[current_slot] == 0)
        return BLOCK_AIR;

    return entries[current_slot];
}

unsigned short Inventory::currentSlotDamage() const
{
    return slotDamage(current_slot);
}

BLOCK_WDATA Inventory::slotBlock(int slot) const
{
    if(slot < 0 || slot >= slot_count || counts[slot] == 0)
        return BLOCK_AIR;

    return entries[slot];
}

unsigned int Inventory::slotCount(int slot) const
{
    if(slot < 0 || slot >= slot_count)
        return 0;

    return counts[slot];
}

unsigned short Inventory::slotDamage(int slot) const
{
    if(slot < 0 || slot >= slot_count || counts[slot] == 0)
        return 0;

    return damage[slot];
}

void Inventory::setSlot(int slot, BLOCK_WDATA block, unsigned int count)
{
    if(slot < 0 || slot >= slot_count)
        return;

    // Callers that do not mention the wear (the crafting grid, the legacy save
    // format) keep whatever the slot already had; setSlotWithDamage() drops it
    // when the slot ends up holding a different item.
    setSlotWithDamage(slot, block, count, damage[slot]);
}

void Inventory::setSlotWithDamage(int slot, BLOCK_WDATA block, unsigned int count, unsigned short worn)
{
    if(slot < 0 || slot >= slot_count)
        return;

    const bool kept_item = (entries[slot] == block && getBLOCK(block) != BLOCK_AIR);

    entries[slot] = block;
    counts[slot] = (getBLOCK(block) == BLOCK_AIR || count == 0) ? 0 : count;

    if(counts[slot] == 0)
    {
        entries[slot] = BLOCK_AIR;
        // An emptied slot keeps no wear, or the next item in it would arrive used.
        damage[slot] = 0;
        // ...and no enchantments, for the same reason: an item that is not there
        // cannot hand its Sharpness to whatever is put in the slot next.
        enchant[slot].clear();
        return;
    }

    // The wear travels with the stack it belongs to, is dropped when the slot is
    // refilled with something else, and never exceeds what the item can take —
    // otherwise a stale value on a block stack would stop it merging with an
    // identical one.
    const int max_damage = ItemRules::maxDamage(block);
    if(!kept_item || max_damage <= 0)
    {
        damage[slot] = 0;
        // A different item in this slot does not inherit what was enchanted onto
        // the last one -- the enchantments belong to the stack, not the position.
        if(!kept_item)
            enchant[slot].clear();
    }
    else
        damage[slot] = worn > max_damage ? static_cast<unsigned short>(max_damage) : worn;
}

const Enchanting::Set &Inventory::slotEnchant(int slot) const
{
    static const Enchanting::Set empty;
    if(slot < 0 || slot >= slot_count)
        return empty;
    return enchant[slot];
}

Enchanting::Set &Inventory::slotEnchant(int slot)
{
    static Enchanting::Set empty;
    if(slot < 0 || slot >= slot_count)
        return empty;
    return enchant[slot];
}

void Inventory::setSlotEnchant(int slot, const Enchanting::Set &set)
{
    if(slot < 0 || slot >= slot_count)
        return;
    enchant[slot] = set;
}

void Inventory::swapSlots(int a, int b)
{
    if(a < 0 || a >= slot_count || b < 0 || b >= slot_count || a == b)
        return;

    std::swap(entries[a], entries[b]);
    std::swap(counts[a], counts[b]);
    std::swap(damage[a], damage[b]);
    // Enchantments move with the stack, like the wear does.
    const Enchanting::Set move = enchant[a];
    enchant[a] = enchant[b];
    enchant[b] = move;
}

unsigned int Inventory::currentSlotCount() const
{
    return counts[current_slot];
}

void Inventory::setCurrentSlot(BLOCK_WDATA block, unsigned int count)
{
    setSlot(current_slot, block, count);
}

bool Inventory::addItem(BLOCK_WDATA block, unsigned int count)
{
    return addItemWithDamage(block, count, 0);
}

bool Inventory::addItemWithDamage(BLOCK_WDATA block, unsigned int count, unsigned short worn)
{
    if(getBLOCK(block) == BLOCK_AIR || count == 0)
        return false;

    const unsigned int limit = static_cast<unsigned int>(ItemRules::maxStackSize(block));

    // Only identical, stackable stacks merge. A tool never does: two half-used
    // pickaxes are not one fresh one, and a damaged one would lose its wear.
    if(limit > 1)
    {
        for(unsigned int i = 0; i < slot_count; ++i)
        {
            if(counts[i] == 0 || entries[i] != block || damage[i] != worn)
                continue;
            if(counts[i] + count > limit)
                continue;

            counts[i] += count;
            return true;
        }
    }

    for(unsigned int i = 0; i < slot_count; ++i)
    {
        if(counts[i] == 0)
        {
            entries[i] = block;
            counts[i] = count;
            damage[i] = worn;
            return true;
        }
    }

    return false;
}

bool Inventory::removeFromCurrentSlot(unsigned int count)
{
    if(count == 0 || counts[current_slot] == 0)
        return false;

    if(counts[current_slot] <= count)
    {
        counts[current_slot] = 0;
        entries[current_slot] = BLOCK_AIR;
        damage[current_slot] = 0;
        enchant[current_slot].clear();
    }
    else
        counts[current_slot] -= count;

    return true;
}

bool Inventory::damageSlot(int slot, int amount)
{
    if(slot < 0 || slot >= slot_count || counts[slot] == 0 || amount <= 0)
        return false;

    const int max_damage = ItemRules::maxDamage(entries[slot]);
    if(max_damage <= 0)
        return false;

    if(!ItemRules::applyDamage(damage[slot], max_damage, amount))
        return false;

    // It broke: a worn-out tool disappears instead of leaving a useless stack.
    entries[slot] = BLOCK_AIR;
    counts[slot] = 0;
    damage[slot] = 0;
    enchant[slot].clear();
    return true;
}

bool Inventory::damageCurrentSlot(int amount)
{
    return damageSlot(current_slot, amount);
}

void Inventory::clearArmor()
{
    for(int i = 0; i < armor_slot_count; ++i)
    {
        armor[i] = BLOCK_AIR;
        armor_counts[i] = 0;
        armor_damage[i] = 0;
        armor_enchant[i].clear();
    }
}

BLOCK_WDATA Inventory::armorBlock(int index) const
{
    if(index < 0 || index >= armor_slot_count || armor_counts[index] == 0)
        return BLOCK_AIR;

    return armor[index];
}

unsigned int Inventory::armorCount(int index) const
{
    if(index < 0 || index >= armor_slot_count)
        return 0;

    return armor_counts[index];
}

unsigned short Inventory::armorDamage(int index) const
{
    if(index < 0 || index >= armor_slot_count || armor_counts[index] == 0)
        return 0;

    return armor_damage[index];
}

void Inventory::setArmorSlot(int index, BLOCK_WDATA block, unsigned int count, unsigned short worn)
{
    if(index < 0 || index >= armor_slot_count)
        return;

    const bool kept_item = (armor[index] == block && getBLOCK(block) != BLOCK_AIR);

    armor[index] = block;
    armor_counts[index] = (getBLOCK(block) == BLOCK_AIR || count == 0) ? 0 : count;

    if(armor_counts[index] == 0)
    {
        armor[index] = BLOCK_AIR;
        armor_damage[index] = 0;
        armor_enchant[index].clear();
        return;
    }

    if(!kept_item)
        armor_enchant[index].clear(); // a different piece is not the enchanted one
    armor_damage[index] = kept_item ? worn : 0;
}

int Inventory::totalArmorPoints() const
{
    int points = 0;
    for(int i = 0; i < armor_slot_count; ++i)
    {
        if(armor_counts[i] == 0)
            continue;
        points += ItemRules::armorPoints(armor[i]);
    }
    return points;
}

bool Inventory::damageArmor()
{
    bool any_broke = false;
    for(int i = 0; i < armor_slot_count; ++i)
    {
        if(armor_counts[i] == 0)
            continue;

        const int max_damage = ItemRules::maxDamage(armor[i]);
        if(max_damage <= 0)
            continue;

        if(ItemRules::applyDamage(armor_damage[i], max_damage, ItemRules::ArmorDamagePerHit))
        {
            armor[i] = BLOCK_AIR;
            armor_counts[i] = 0;
            armor_damage[i] = 0;
            armor_enchant[i].clear();
            any_broke = true;
        }
    }
    return any_broke;
}

bool Inventory::equipArmorFromSlot(int slot)
{
    const BLOCK_WDATA block = slotBlock(slot);
    const uint8_t armor_index = ItemRules::armorSlot(block);
    if(armor_index == ItemRules::NoArmorSlot)
        return false;

    const BLOCK_WDATA worn_block = armor[armor_index];
    const unsigned int worn_count = armor_counts[armor_index];
    const unsigned short worn_damage = armor_damage[armor_index];

    // The enchantments swap with the pieces themselves, so taking a helmet off
    // and putting it back does not lose its Protection.
    const Enchanting::Set worn_enchant = armor_enchant[armor_index];
    const Enchanting::Set slot_enchant = slotEnchant(slot);

    setArmorSlot(armor_index, block, 1, slotDamage(slot));
    armor_enchant[armor_index] = slot_enchant;

    if(worn_count > 0)
    {
        setSlotWithDamage(slot, worn_block, worn_count, worn_damage);
        setSlotEnchant(slot, worn_enchant);
    }
    else
        setSlot(slot, BLOCK_AIR, 0);

    return true;
}

BLOCK_WDATA Inventory::offhandBlock() const
{
    if(offhand_count == 0)
        return BLOCK_AIR;

    return offhand;
}

unsigned int Inventory::offhandCount() const
{
    return offhand_count;
}

unsigned short Inventory::offhandDamage() const
{
    if(offhand_count == 0)
        return 0;

    return offhand_damage;
}

void Inventory::setOffhand(BLOCK_WDATA block, unsigned int count, unsigned short worn)
{
    // Same rule as a plain inventory slot: the wear belongs to the stack, so it is
    // kept only while the same item stays in the slot, and never more than the
    // item can take.
    const bool kept_item = (offhand == block && getBLOCK(block) != BLOCK_AIR);

    offhand = block;
    offhand_count = (getBLOCK(block) == BLOCK_AIR || count == 0) ? 0 : count;

    if(offhand_count == 0)
    {
        offhand = BLOCK_AIR;
        offhand_damage = 0;
        return;
    }

    const int max_damage = ItemRules::maxDamage(block);
    if(!kept_item || max_damage <= 0)
        offhand_damage = 0;
    else
        offhand_damage = worn > max_damage ? static_cast<unsigned short>(max_damage) : worn;
}

void Inventory::swapOffhandWithCurrentSlot()
{
    const BLOCK_WDATA held_block = currentSlot();
    const unsigned int held_count = currentSlotCount();
    const unsigned short held_worn = currentSlotDamage();

    const BLOCK_WDATA off_block = offhandBlock();
    const unsigned int off_count = offhandCount();
    const unsigned short off_worn = offhandDamage();

    setOffhand(held_block, held_count, held_worn);
    setSlotWithDamage(current_slot, off_block, off_count, off_worn);
}

void Inventory::importLegacyCounts()
{
    for(unsigned int i = 0; i < slot_count; ++i)
    {
        if(getBLOCK(entries[i]) == BLOCK_AIR)
        {
            counts[i] = 0;
            damage[i] = 0;
        }
        else if(counts[i] == 0)
            counts[i] = 1;
    }
}

void Inventory::previousSlot()
{
    --current_slot;
    if(current_slot < 0)
        current_slot = hotbar_slot_count - 1;
}

void Inventory::nextSlot()
{
    ++current_slot;
    if(current_slot >= hotbar_slot_count)
        current_slot = 0;
}

void Inventory::reset()
{
    for(int i = 0; i < slot_count; ++i)
    {
        entries[i] = BLOCK_AIR;
        counts[i] = 0;
        damage[i] = 0;
        enchant[i].clear();
    }
    clearArmor();
    offhand = BLOCK_AIR;
    offhand_count = 0;
    offhand_damage = 0;
    current_slot = 0;
}
