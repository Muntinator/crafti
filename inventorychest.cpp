// The chest container screen.
//
// Chests are the second container in the game (after the furnace) and the only
// one with a variable size: one chest has 27 slots and a double chest 54. The
// contents live in cheststore.h, keyed by block position, so this file only has
// to lay the slots out, draw them and turn clicks into changes of that store —
// there is no per-screen copy of the contents to keep in step, which is also why
// closing the screen cannot lose an item.
//
// The window is the official gui/container/generic_54.png, whose two halves are
// exactly what a chest needs: the chest's own rows on top, and -- blitted below
// them -- the player's inventory part, which is the same picture for a chest of
// any size. A chest is the one container whose height changes with its contents,
// and vanilla's own sheet is laid out to be cut that way.

#include "inventorytask.h"

#ifndef _TINSPIRE
#include <SDL/SDL.h>
#endif

#include <algorithm>
#include <cstdio>

#include "blockrenderer.h"
#include "cheststore.h"
#include "font.h"
#include "inventory.h"
#include "itemicons.h"
#include "itemrules.h"
#include "texturetools.h"
#include "worlditems.h" // ensureChestAt
#include "worldtask.h"

#include "textures/armor_slots.h"
#include "textures/chest_player.h"
#include "textures/chest_top.h"

namespace
{
    constexpr int inv_src_slot_size = 16;
    constexpr int inv_src_slot_gap = 2;
    constexpr int inv_src_pitch = inv_src_slot_size + inv_src_slot_gap; // 18
#ifdef _TINSPIRE
    constexpr int inv_draw_scale = 1;
#else
    constexpr int inv_draw_scale = 2;
#endif
    constexpr int inv_draw_slot_size = inv_src_slot_size * inv_draw_scale;
    constexpr int inv_draw_pitch = inv_src_pitch * inv_draw_scale;

    constexpr int chest_columns = 9;

    // The window sheet's own geometry (GenericContainerScreen). The chest's rows
    // start at (8,18) of the top pane, whose height is the number of rows times a
    // slot plus one 17-pixel band; below that band the sheet carries the player's
    // own inventory as a 96-pixel patch, in which the three rows of storage start
    // 14 pixels down and the hotbar 68 -- which is where the second blit of that
    // patch puts them, wherever the chest's own rows ended.
    constexpr int window_src_width = 176;
    constexpr int slot_src_x = 8;
    constexpr int chest_first_row_src_y = 18;
    constexpr int pane_band = 17;
    constexpr int player_part_src_height = 96;
    constexpr int player_first_row_offset = 14;
    constexpr int player_hotbar_offset = 68;

    // The four armour slots are part of the player's window too, down its left
    // edge at (8,8) one per 18-pixel row, so the worn widgets are placed on the
    // sheet's own slots rather than in a strip of their own.
    constexpr int armor_first_src_x = 8;
    constexpr int armor_first_src_y = 8;
    constexpr int armor_title_src_y = 6; // the height vanilla centres a container title at

    // The offhand slot, at the (77,62) vanilla's InventoryMenu hands its shield
    // slot. Only the player's own window has it; the crafting table's sheet has
    // no well there, which is why render() does not call it for that screen.
    constexpr int offhand_src_x = 77;
    constexpr int offhand_src_y = 62;

    /** How many rows of nine slots the open chest has. */
    int chestRowCount(int slot_count)
    {
        if(slot_count <= 0)
            slot_count = ChestStore::SlotCount;
        return slot_count / chest_columns;
    }

    /** The height of the chest's own pane, in sheet pixels. */
    int chestPaneHeight(int rows)
    {
        return rows * inv_src_pitch + pane_band;
    }

    /** Anything that cannot wear must not carry a damage value into the store. */
    unsigned short sanitizeWear(BLOCK_WDATA block, unsigned short worn)
    {
        const int max_damage = ItemRules::maxDamage(block);
        if(max_damage <= 0)
            return 0;
        return worn > max_damage ? static_cast<unsigned short>(max_damage) : worn;
    }
}

void InventoryTask::openChest(int block_x, int block_y, int block_z)
{
    crafting_table_mode = false;
    furnace_mode = false;
    chest_mode = true;
    chest_bx = block_x;
    chest_by = block_y;
    chest_bz = block_z;

    // A chest from an older save, or one placed before this build, has no storage
    // yet: opening it makes the container rather than showing an empty screen
    // that would refuse the first item put into it. A chest the world generated
    // is filled here with the loot of the structure it belongs to, which is why
    // the loot of an untouched dungeon costs nothing to keep around.
    ensureChestAt(block_x, block_y, block_z);

    activate();
}

int InventoryTask::chestSlotCount() const
{
    if(!chest_mode)
        return 0;
    return ChestStore::slotCountOf(chest_bx, chest_by, chest_bz);
}

void InventoryTask::chestPanelRect(int &x, int &y, int &w, int &h) const
{
    const int rows = chestRowCount(chestSlotCount());

    w = window_src_width * inv_draw_scale;
    h = (chestPaneHeight(rows) + player_part_src_height) * inv_draw_scale;

    x = (SCREEN_WIDTH - w) / 2;
    y = (SCREEN_HEIGHT - h) / 2;
    if(x < 0)
        x = 0;
    if(y < 0)
        y = 0;
}

void InventoryTask::chestSlotBounds(int chest_slot, int &x, int &y, int &w, int &h) const
{
    int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
    chestPanelRect(panel_x, panel_y, panel_w, panel_h);

    const int column = chest_slot % chest_columns;
    const int row = chest_slot / chest_columns;

    x = panel_x + (slot_src_x + column * inv_src_pitch) * inv_draw_scale;
    y = panel_y + (chest_first_row_src_y + row * inv_src_pitch) * inv_draw_scale;
    w = inv_draw_slot_size;
    h = inv_draw_slot_size;
}

void InventoryTask::chestPlayerSlotBounds(int player_slot, int &x, int &y, int &w, int &h) const
{
    int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
    chestPanelRect(panel_x, panel_y, panel_w, panel_h);

    const int rows = chestRowCount(chestSlotCount());

    // Slots 0..8 are the hotbar and go in the bottom row, exactly like the player
    // inventory screen, so a stack does not move around between the two screens.
    const bool hotbar = player_slot < Inventory::hotbar_slot_count;
    const int index = hotbar ? player_slot : player_slot - Inventory::hotbar_slot_count;
    const int row = hotbar ? 3 : index / chest_columns;
    const int column = hotbar ? index : index % chest_columns;

    const int part_top = chestPaneHeight(rows)
                       + (hotbar ? player_hotbar_offset : player_first_row_offset + row * inv_src_pitch);

    x = panel_x + (slot_src_x + column * inv_src_pitch) * inv_draw_scale;
    y = panel_y + part_top * inv_draw_scale;
    w = inv_draw_slot_size;
    h = inv_draw_slot_size;
}

int InventoryTask::chestSlotFromMouse(int mouse_x, int mouse_y) const
{
    const int slot_count = chestSlotCount();
    for(int i = 0; i < slot_count; ++i)
    {
        int sx, sy, sw, sh;
        chestSlotBounds(i, sx, sy, sw, sh);
        if(mouse_x >= sx && mouse_x < sx + sw && mouse_y >= sy && mouse_y < sy + sh)
            return CHEST_SLOT_OFFSET + i;
    }

    for(int s = 0; s < Inventory::slot_count; ++s)
    {
        int sx, sy, sw, sh;
        chestPlayerSlotBounds(s, sx, sy, sw, sh);
        if(mouse_x >= sx && mouse_x < sx + sw && mouse_y >= sy && mouse_y < sy + sh)
            return s;
    }

    return INVALID_SLOT;
}

void InventoryTask::chestHandleClick(int slot, bool right_click)
{
    const int index = slot - CHEST_SLOT_OFFSET;
    ChestStore::Stack inside = ChestStore::stackAt(chest_bx, chest_by, chest_bz, index);
    if(inside.count == 0)
        inside = ChestStore::Stack();

    if(!isHoldingItem())
    {
        if(getBLOCK(inside.block) == BLOCK_AIR || inside.count == 0)
            return;

        // Right click takes half the stack, left click all of it.
        const unsigned int picked = right_click ? (inside.count + 1) / 2 : inside.count;
        held_block = inside.block;
        held_count = picked;
        held_damage = inside.damage;

        inside.count -= picked;
        if(inside.count == 0)
            inside = ChestStore::Stack();
        ChestStore::setStackAt(chest_bx, chest_by, chest_bz, index, inside);
        return;
    }

    const BLOCK_WDATA held = held_block;
    const unsigned short wear = sanitizeWear(held, held_damage);

    if(getBLOCK(inside.block) == BLOCK_AIR || inside.count == 0)
    {
        const unsigned int placed = right_click ? 1u : held_count;
        inside.block = held;
        inside.count = placed;
        inside.damage = wear;
        ChestStore::setStackAt(chest_bx, chest_by, chest_bz, index, inside);

        held_count -= placed;
        if(held_count == 0)
        {
            held_block = BLOCK_AIR;
            held_damage = 0;
        }
        return;
    }

    // Top up the same item, up to a full stack; a tool never merges because a
    // stack of one is already full.
    const unsigned int limit = static_cast<unsigned int>(ItemRules::maxStackSize(held));
    if(inside.block == held && inside.damage == wear && limit > 1)
    {
        const unsigned int room = (inside.count < limit) ? limit - inside.count : 0;
        if(room > 0)
        {
            const unsigned int placed = right_click ? std::min(1u, room) : std::min(held_count, room);
            inside.count += placed;
            ChestStore::setStackAt(chest_bx, chest_by, chest_bz, index, inside);

            held_count -= placed;
            if(held_count == 0)
            {
                held_block = BLOCK_AIR;
                held_damage = 0;
            }
            return;
        }
    }

    // Otherwise swap what is on the cursor with what is in the chest.
    const ChestStore::Stack previous = inside;
    inside.block = held;
    inside.count = held_count;
    inside.damage = wear;
    ChestStore::setStackAt(chest_bx, chest_by, chest_bz, index, inside);

    held_block = previous.block;
    held_count = previous.count;
    held_damage = previous.damage;
}

void InventoryTask::drawStackItem(TEXTURE &tex, BLOCK_WDATA block, unsigned int count, unsigned short damage, int x, int y, int size)
{
    if(getBLOCK(block) == BLOCK_AIR || count == 0)
        return;

    if(getBLOCK(block) == BLOCK_ITEM)
    {
        drawItemIcon(block, tex, x, y, size);
    }
    else
    {
#ifdef _TINSPIRE
        const TextureAtlasEntry &icon_tex = global_block_renderer.materialTexture(block).resized;
        drawTexture(*terrain_resized, tex,
                    icon_tex.left, icon_tex.top,
                    icon_tex.right - icon_tex.left, icon_tex.bottom - icon_tex.top,
                    x, y,
                    size, size);
#else
        const int icon_w = 24;
        const int icon_h = 24;
        const int preview_x = x + (size - icon_w) / 2;
        const int preview_y = y + (size - icon_h) / 2;
        global_block_renderer.drawPreview(block, tex, preview_x, preview_y);
#endif
    }

    char count_text[12];
    snprintf(count_text, sizeof(count_text), "%u", count);
    drawString(count_text, 0xFFFF, tex, x + size - 10, y + 2);

    Inventory::drawDurabilityBar(tex, block, damage, x, y, size);
}

bool InventoryTask::armorWidgetBounds(int index, int &x, int &y, int &w, int &h) const
{
    if(index < 0 || index >= Inventory::armor_slot_count)
        return false;

    // The window already draws the four empty armour slots, so the widget for a
    // worn piece is just that slot's rectangle. Both the player window and the
    // crafting table's carry them at the same place, and the furnace window (the
    // one screen this is not called for) does not carry them at all.
    x = InventoryTask::inventoryWindowX() + armor_first_src_x * inv_draw_scale;
    y = InventoryTask::inventoryWindowY()
      + (armor_first_src_y + index * inv_src_pitch) * inv_draw_scale;
    w = inv_draw_slot_size;
    h = inv_draw_slot_size;
    return true;
}

int InventoryTask::armorSlotFromMouse(int mouse_x, int mouse_y) const
{
    for(int i = 0; i < Inventory::armor_slot_count; ++i)
    {
        int x, y, w, h;
        if(!armorWidgetBounds(i, x, y, w, h))
            continue;
        if(mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h)
            return ARMOR_SLOT_OFFSET + i;
    }

    return INVALID_SLOT;
}

void InventoryTask::armorHandleClick(int index)
{
    const BLOCK_WDATA worn = current_inventory.armorBlock(index);
    const unsigned int worn_count = current_inventory.armorCount(index);
    const unsigned short worn_damage = current_inventory.armorDamage(index);

    if(!isHoldingItem())
    {
        if(getBLOCK(worn) == BLOCK_AIR || worn_count == 0)
            return;

        held_block = worn;
        held_count = worn_count;
        held_damage = worn_damage;
        current_inventory.setArmorSlot(index, BLOCK_AIR, 0, 0);
        return;
    }

    // Only the piece that belongs in this slot can go on it.
    if(ItemRules::armorSlot(held_block) != static_cast<uint8_t>(index))
        return;

    current_inventory.setArmorSlot(index, held_block, 1, sanitizeWear(held_block, held_damage));

    if(worn_count > 0)
    {
        held_block = worn;
        held_count = worn_count;
        held_damage = worn_damage;
    }
    else
    {
        held_block = BLOCK_AIR;
        held_count = 0;
        held_damage = 0;
    }
}

void InventoryTask::renderArmorWidgets()
{
    // The slots themselves are already painted by whichever vanilla window is
    // showing, so only the worn piece has to be drawn on top of its slot.
    for(int i = 0; i < Inventory::armor_slot_count; ++i)
    {
        int x, y, w, h;
        if(!armorWidgetBounds(i, x, y, w, h))
            continue;

        const BLOCK_WDATA worn = current_inventory.armorBlock(i);
        if(getBLOCK(worn) == BLOCK_AIR || current_inventory.armorCount(i) == 0)
        {
            // An empty armour slot shows the faint outline of the piece that
            // belongs there, which is vanilla's Slot.getNoItemIcon. The five
            // outlines are stitched into one strip in the order the slots run
            // down, so the piece's own index picks its cell.
            drawTexture(armor_slots, *screen,
                        static_cast<uint16_t>(i * inv_src_slot_size), static_cast<uint16_t>(0),
                        static_cast<uint16_t>(inv_src_slot_size), static_cast<uint16_t>(inv_src_slot_size),
                        static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                        static_cast<uint16_t>(w), static_cast<uint16_t>(h));
            continue;
        }

        drawStackItem(*screen, worn, 1, current_inventory.armorDamage(i), x, y, w);
    }
}

bool InventoryTask::offhandWidgetBounds(int &x, int &y, int &w, int &h) const
{
    // The window's own art already draws the empty well, so this is just that
    // slot's rectangle -- where vanilla's InventoryMenu puts its shield slot.
    x = InventoryTask::inventoryWindowX() + offhand_src_x * inv_draw_scale;
    y = InventoryTask::inventoryWindowY() + offhand_src_y * inv_draw_scale;
    w = inv_draw_slot_size;
    h = inv_draw_slot_size;
    return true;
}

int InventoryTask::offhandSlotFromMouse(int mouse_x, int mouse_y) const
{
    int x, y, w, h;
    offhandWidgetBounds(x, y, w, h);
    if(mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h)
        return OFFHAND_SLOT;

    return INVALID_SLOT;
}

void InventoryTask::offhandHandleClick(bool right_click)
{
    const BLOCK_WDATA off_block = current_inventory.offhandBlock();
    const unsigned int off_count = current_inventory.offhandCount();
    const unsigned short off_damage = current_inventory.offhandDamage();

    // The offhand takes any item, so it behaves exactly as a plain inventory
    // slot does: a left click moves the whole stack, a right click half of it.
    if(!isHoldingItem())
    {
        if(getBLOCK(off_block) == BLOCK_AIR || off_count == 0)
            return;

        if(right_click)
        {
            const unsigned int picked = (off_count + 1) / 2;
            const unsigned int remaining = off_count - picked;
            held_block = off_block;
            held_count = picked;
            held_damage = off_damage;
            current_inventory.setOffhand(remaining == 0 ? BLOCK_AIR : off_block, remaining, off_damage);
        }
        else
        {
            held_block = off_block;
            held_count = off_count;
            held_damage = off_damage;
            current_inventory.setOffhand(BLOCK_AIR, 0, 0);
        }
        return;
    }

    const unsigned int placed = right_click ? 1 : held_count;

    if(getBLOCK(off_block) == BLOCK_AIR || off_count == 0)
    {
        current_inventory.setOffhand(held_block, placed, held_damage);
        held_count -= placed;
        if(held_count == 0)
        {
            held_block = BLOCK_AIR;
            held_damage = 0;
        }
    }
    else if(off_block == held_block && ItemRules::maxStackSize(off_block) > 1)
    {
        current_inventory.setOffhand(off_block, off_count + placed, off_damage);
        held_count -= placed;
        if(held_count == 0)
        {
            held_block = BLOCK_AIR;
            held_damage = 0;
        }
    }
    else if(!right_click)
    {
        // Two different items: they trade places, wear and all, like a slot.
        current_inventory.setOffhand(held_block, held_count, held_damage);
        held_block = off_block;
        held_count = off_count;
        held_damage = off_damage;
    }
}

void InventoryTask::renderOffhandWidget()
{
    int x, y, w, h;
    if(!offhandWidgetBounds(x, y, w, h))
        return;

    const BLOCK_WDATA item = current_inventory.offhandBlock();
    if(getBLOCK(item) == BLOCK_AIR || current_inventory.offhandCount() == 0)
    {
        // The offhand's own no-item icon is the shield outline; vanilla's
        // InventoryMenu overrides the slot's getNoItemIcon() with it. It is the
        // fifth cell of the armour-slot strip, just past the four worn pieces.
        drawTexture(armor_slots, *screen,
                    static_cast<uint16_t>(Inventory::armor_slot_count * inv_src_slot_size),
                    static_cast<uint16_t>(0),
                    static_cast<uint16_t>(inv_src_slot_size), static_cast<uint16_t>(inv_src_slot_size),
                    static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                    static_cast<uint16_t>(w), static_cast<uint16_t>(h));
        return;
    }

    drawStackItem(*screen, item, current_inventory.offhandCount(),
                  current_inventory.offhandDamage(), x, y, w);
}

void InventoryTask::renderChestPanel()
{
    int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
    chestPanelRect(panel_x, panel_y, panel_w, panel_h);

    const int rows = chestRowCount(chestSlotCount());
    const int pane_h = chestPaneHeight(rows);
    const int draw_w = panel_w;
    const int player_h = panel_h - pane_h * inv_draw_scale; // the 96-pixel part

    // The chest's own pane, then the player's inventory part below it: the two
    // halves of generic_54.png, stitched together exactly as vanilla's chest
    // screen does -- the top pane carries the chest rows for however many rows the
    // chest has, and the bottom pane is the same 96-pixel picture for every chest.
    drawTexture(chest_top, *screen,
                static_cast<uint16_t>(0), static_cast<uint16_t>(0),
                static_cast<uint16_t>(window_src_width), static_cast<uint16_t>(pane_h),
                static_cast<uint16_t>(panel_x), static_cast<uint16_t>(panel_y),
                static_cast<uint16_t>(draw_w), static_cast<uint16_t>(pane_h * inv_draw_scale));
    drawTexture(chest_player, *screen,
                static_cast<uint16_t>(0), static_cast<uint16_t>(0),
                static_cast<uint16_t>(window_src_width), static_cast<uint16_t>(player_part_src_height),
                static_cast<uint16_t>(panel_x),
                static_cast<uint16_t>(panel_y + pane_h * inv_draw_scale),
                static_cast<uint16_t>(draw_w),
                static_cast<uint16_t>(player_h));

    // "Chest" or "Large Chest", centred on the window's top edge, which is where
    // vanilla draws a container's title.
    const char *title = chestSlotCount() > ChestStore::SlotCount ? "Large Chest" : "Chest";
    const int title_w = static_cast<int>(measureString(title));
    drawString(title, 0x4208, *screen,
               panel_x + (draw_w - title_w) / 2,
               panel_y + armor_title_src_y * inv_draw_scale);

    const int slot_count = chestSlotCount();

    // The items, on top of the empty slots the sheet just drew.
    for(int i = 0; i < slot_count; ++i)
    {
        const ChestStore::Stack stack = ChestStore::stackAt(chest_bx, chest_by, chest_bz, i);
        if(getBLOCK(stack.block) == BLOCK_AIR || stack.count == 0)
            continue;

        int sx, sy, sw, sh;
        chestSlotBounds(i, sx, sy, sw, sh);
        drawStackItem(*screen, stack.block, stack.count, stack.damage, sx, sy, std::min(sw, sh));
    }

    for(int s = 0; s < Inventory::slot_count; ++s)
    {
        int sx, sy, sw, sh;
        chestPlayerSlotBounds(s, sx, sy, sw, sh);
        drawSlotItem(*screen, s, sx, sy);
    }

    // The stack on the cursor, drawn over everything, in the same way the player
    // inventory screen draws it.
#ifndef _TINSPIRE
    int mouse_x = 0, mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    if(getBLOCK(held_block) != BLOCK_AIR && held_count > 0)
    {
        drawStackItem(*screen, held_block, held_count, held_damage,
                      mouse_x - inv_draw_slot_size / 2, mouse_y - inv_draw_slot_size / 2, inv_draw_slot_size);
    }
#else
    if(getBLOCK(held_block) != BLOCK_AIR && held_count > 0)
    {
        const int held_x = std::max(0, std::min(cursor_x - inv_draw_slot_size / 2, SCREEN_WIDTH - inv_draw_slot_size));
        const int held_y = std::max(0, std::min(cursor_y - inv_draw_slot_size / 2, SCREEN_HEIGHT - inv_draw_slot_size));
        drawStackItem(*screen, held_block, held_count, held_damage, held_x, held_y, inv_draw_slot_size);
    }

    drawRectangle(*screen, std::max(0, cursor_x - 4), std::max(0, cursor_y - 4), 9, 9, 0xFFFF);
#endif
}
