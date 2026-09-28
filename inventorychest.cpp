// The chest container screen.
//
// Chests are the second container in the game (after the furnace) and the only
// one with a variable size: one chest has 27 slots and a double chest 54. The
// contents live in cheststore.h, keyed by block position, so this file only has
// to lay the slots out, draw them and turn clicks into changes of that store —
// there is no per-screen copy of the contents to keep in step, which is also why
// closing the screen cannot lose an item.
//
// The panel is drawn from rectangles rather than from a GUI sprite: the three
// atlases the game ships do not contain a chest window, and a slot is two
// rectangles and an item icon either way.

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
#include "worlditems.h" // ensureChestAt
#include "worldtask.h"

namespace
{
    constexpr int inv_src_slot_size = 16;
    constexpr int inv_src_slot_gap = 2;
#ifdef _TINSPIRE
    constexpr int inv_draw_scale = 1;
#else
    constexpr int inv_draw_scale = 2;
#endif
    constexpr int inv_draw_slot_size = inv_src_slot_size * inv_draw_scale;
    constexpr int inv_draw_pitch = (inv_src_slot_size + inv_src_slot_gap) * inv_draw_scale;

    constexpr int panel_padding = 6 * inv_draw_scale;
    constexpr int panel_title_height = 14 * inv_draw_scale;
    constexpr int panel_grid_gap = 6 * inv_draw_scale;
    constexpr int panel_hotbar_gap = 2 * inv_draw_scale;

    constexpr int chest_columns = 9;

    /** A filled rectangle: drawRectangle() only draws the outline. */
    void fillRect(TEXTURE &tex, int x, int y, int w, int h, COLOR c)
    {
        if(w <= 0 || h <= 0)
            return;

        if(x < 0)
        {
            w += x;
            x = 0;
        }
        if(y < 0)
        {
            h += y;
            y = 0;
        }
        if(x + w > static_cast<int>(tex.width))
            w = static_cast<int>(tex.width) - x;
        if(y + h > static_cast<int>(tex.height))
            h = static_cast<int>(tex.height) - y;
        if(w <= 0 || h <= 0)
            return;

        for(int py = y; py < y + h; ++py)
            for(int px = x; px < x + w; ++px)
                tex.bitmap[px + py * tex.width] = c;
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
    int slot_count = chestSlotCount();
    if(slot_count <= 0)
        slot_count = ChestStore::SlotCount;
    const int chest_rows = slot_count / chest_columns;

    w = panel_padding * 2 + (chest_columns - 1) * inv_draw_pitch + inv_draw_slot_size;
    h = panel_padding * 2 + panel_title_height
      + chest_rows * inv_draw_pitch
      + panel_grid_gap
      + 3 * inv_draw_pitch + panel_hotbar_gap + inv_draw_pitch;

    if(w > SCREEN_WIDTH)
        w = SCREEN_WIDTH;
    if(h > SCREEN_HEIGHT)
        h = SCREEN_HEIGHT;

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

    x = panel_x + panel_padding + column * inv_draw_pitch;
    y = panel_y + panel_padding + panel_title_height + row * inv_draw_pitch;
    w = inv_draw_slot_size;
    h = inv_draw_slot_size;
}

void InventoryTask::chestPlayerSlotBounds(int player_slot, int &x, int &y, int &w, int &h) const
{
    int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
    chestPanelRect(panel_x, panel_y, panel_w, panel_h);

    int slot_count = chestSlotCount();
    if(slot_count <= 0)
        slot_count = ChestStore::SlotCount;
    const int chest_rows = slot_count / chest_columns;

    // Slots 0..8 are the hotbar and go in the bottom row, exactly like the player
    // inventory screen, so a stack does not move around between the two screens.
    const bool hotbar = player_slot < Inventory::hotbar_slot_count;
    const int index = hotbar ? player_slot : player_slot - Inventory::hotbar_slot_count;
    const int row = hotbar ? 3 : index / chest_columns;
    const int column = hotbar ? index : index % chest_columns;

    const int grid_top = panel_y + panel_padding + panel_title_height + chest_rows * inv_draw_pitch + panel_grid_gap;

    x = panel_x + panel_padding + column * inv_draw_pitch;
    y = grid_top + row * inv_draw_pitch + (hotbar ? panel_hotbar_gap : 0);
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

    // The widgets go in the empty strip to the left of the inventory window: the
    // window is centred and narrower than the screen on both platforms, so
    // nothing else is drawn there and a click cannot be ambiguous.
    const int gap = 4 * inv_draw_scale;
    const int block_w = 2 * inv_draw_slot_size + gap;

    int origin_x = InventoryTask::inventoryWindowX() - block_w - 2 * gap;
    if(origin_x < 2)
        origin_x = 2;

    const int column = index % 2;
    const int row = index / 2;
    const int origin_y = InventoryTask::inventoryWindowY() + inv_draw_pitch;

    x = origin_x + column * (inv_draw_slot_size + gap);
    y = origin_y + row * (inv_draw_slot_size + gap);
    w = inv_draw_slot_size;
    h = inv_draw_slot_size;
    return x + w <= SCREEN_WIDTH && y + h <= SCREEN_HEIGHT;
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
    for(int i = 0; i < Inventory::armor_slot_count; ++i)
    {
        int x, y, w, h;
        if(!armorWidgetBounds(i, x, y, w, h))
            continue;

        fillRect(*screen, x, y, w, h, 0x8C71);
        drawRectangle(*screen, x, y, w, h, 0x4208);

        const BLOCK_WDATA worn = current_inventory.armorBlock(i);
        if(getBLOCK(worn) == BLOCK_AIR || current_inventory.armorCount(i) == 0)
            continue;

        drawStackItem(*screen, worn, 1, current_inventory.armorDamage(i), x, y, w);
    }
}

void InventoryTask::renderChestPanel()
{
    int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
    chestPanelRect(panel_x, panel_y, panel_w, panel_h);

    // The window, then the two rows of slots in it.
    fillRect(*screen, panel_x, panel_y, panel_w, panel_h, 0xC618);
    drawRectangle(*screen, panel_x, panel_y, panel_w, panel_h, 0x4208);
    drawRectangle(*screen, panel_x + 1, panel_y + 1, panel_w - 2, panel_h - 2, 0xFFFF);

    // How many items are inside, so a chest can be checked without looking in it.
    char title[32];
    snprintf(title, sizeof(title), "%s (%u items)",
             chestSlotCount() > ChestStore::SlotCount ? "Large Chest" : "Chest",
             ChestStore::itemCount(chest_bx, chest_by, chest_bz));
    drawString(title, 0x4208, *screen, panel_x + panel_padding, panel_y + panel_padding);

    const int slot_count = chestSlotCount();
    for(int i = 0; i < slot_count; ++i)
    {
        int sx, sy, sw, sh;
        chestSlotBounds(i, sx, sy, sw, sh);
        fillRect(*screen, sx, sy, sw, sh, 0x8C71);
        drawRectangle(*screen, sx, sy, sw, sh, 0x4208);
    }

    for(int s = 0; s < Inventory::slot_count; ++s)
    {
        int sx, sy, sw, sh;
        chestPlayerSlotBounds(s, sx, sy, sw, sh);
        fillRect(*screen, sx, sy, sw, sh, 0x8C71);
        drawRectangle(*screen, sx, sy, sw, sh, 0x4208);
    }

    // The items, on top of the empty slots.
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
