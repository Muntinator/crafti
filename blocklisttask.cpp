// The block list, redrawn as the vanilla creative inventory.
//
// It used to be a flat black panel with a 9x5 grid of 32-pixel fields. It is now
// the official 1.17.1 creative window: the 195x136 `tab_items.png` background
// (whose slot grid, hotbar and scrollbar track are baked in), the tab strip from
// `tabs.png` along the top, the scrollbar handle beside the grid, and 16-pixel
// icons in 18-pixel slots -- all at vanilla's own coordinates.
//
// The three pages are the three tabs, and a page longer than the 45 slots the
// grid holds scrolls, which is what the creative item list does. Before this
// pass the tab simply drew the first 45 entries and the rest were unreachable.
//
// The tab geometry is vanilla's: a tab is a 28x32 cell of `tabs.png`, placed 29
// pixels apart with the bottom 4 rows tucked behind the window top, and the
// selected tab is the same cell from the sheet's second band (the lighter one).

#include "blocklisttask.h"

#include <algorithm>
#include <cstdio>

#include "blockrenderer.h"
#include "font.h"
#include "inventory.h"
#include "itemicons.h"
#include "menuui.h"
#include "terrain.h"
#include "texturetools.h"
#include "worldtask.h"
#include "textures/items.h"
#include "textures/creative_window.h"
#include "textures/creative_tabs.h"
#include "textures/creative_scroll.h"

BlockListTask block_list_task;

namespace
{
    // --- the catalogue ----------------------------------------------------

    const BLOCK_WDATA user_selectable[] = {
        BLOCK_STONE,
        BLOCK_COBBLESTONE,
        BLOCK_DIRT,
        BLOCK_GRASS,
        BLOCK_SAND,
        BLOCK_WOOD,
        BLOCK_LEAVES,
        BLOCK_PLANKS_NORMAL,
        BLOCK_PLANKS_DARK,
        BLOCK_PLANKS_BRIGHT,
        BLOCK_WALL,
        BLOCK_GLASS,
        BLOCK_DOOR,
        BLOCK_COAL_ORE,
        BLOCK_GOLD_ORE,
        BLOCK_IRON_ORE,
        BLOCK_DIAMOND_ORE,
        BLOCK_REDSTONE_ORE,
        BLOCK_IRON,
        BLOCK_GOLD,
        BLOCK_DIAMOND,
        BLOCK_WOOL_BLACK,
        BLOCK_WOOL_RED,
        BLOCK_WOOL_DARK_GREEN,
        BLOCK_WOOL_BROWN,
        BLOCK_WOOL_DARK_BLUE,
        BLOCK_WOOL_PURPLE,
        BLOCK_WOOL_CYAN,
        BLOCK_WOOL_WHITE,
        BLOCK_WOOL_GRAY,
        BLOCK_WOOL_PINK,
        BLOCK_WOOL_GREEN,
        BLOCK_WOOL_YELLOW,
        BLOCK_WOOL_LIGHT_BLUE,
        BLOCK_WOOL_MAGENTA,
        BLOCK_WOOL_ORANGE,
        BLOCK_GLOWSTONE,
        BLOCK_NETHERRACK,
        BLOCK_TNT,
        BLOCK_SPONGE,
        BLOCK_FURNACE,
        BLOCK_CHEST,
        BLOCK_BED,
        BLOCK_CRAFTING_TABLE,
        BLOCK_BOOKSHELF,
        BLOCK_PUMPKIN,
        getBLOCKWDATA(BLOCK_WATER, RANGE_WATER),
        getBLOCKWDATA(BLOCK_LAVA, RANGE_LAVA),
        getBLOCKWDATA(BLOCK_FLOWER, 0),
        getBLOCKWDATA(BLOCK_FLOWER, 1),
        getBLOCKWDATA(BLOCK_MUSHROOM, 0),
        getBLOCKWDATA(BLOCK_MUSHROOM, 1),
        getBLOCKWDATA(BLOCK_WHEAT, 0),
        BLOCK_SPIDERWEB,
        BLOCK_TORCH,
        BLOCK_CAKE,
        BLOCK_REDSTONE_LAMP,
        BLOCK_REDSTONE_SWITCH,
        BLOCK_PRESSURE_PLATE,
        BLOCK_REDSTONE_WIRE,
        BLOCK_REDSTONE_TORCH,
        // Snow is the one block in here that appears on its own: the weather puts
        // it on the ground and melts it again (snowcover.h). It is listed so a
        // layer can also be put down and taken up by hand.
        BLOCK_SNOW
    };

    const BLOCK_WDATA user_items_page_1[] = {
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::STICK)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::COAL)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::CHARCOAL)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::IRON_INGOT)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::REDSTONE_DUST)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::BREAD)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::BUCKET)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::BOWL)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::WOODEN_PICKAXE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::STONE_PICKAXE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::IRON_PICKAXE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::DIAMOND_PICKAXE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::GOLDEN_PICKAXE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::WOODEN_AXE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::STONE_AXE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::IRON_AXE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::DIAMOND_AXE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::GOLDEN_AXE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::WOODEN_SHOVEL)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::STONE_SHOVEL)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::IRON_SHOVEL)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::DIAMOND_SHOVEL)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::GOLDEN_SHOVEL)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::WOODEN_HOE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::STONE_HOE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::IRON_HOE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::DIAMOND_HOE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::GOLDEN_HOE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::WOODEN_SWORD)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::STONE_SWORD)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::IRON_SWORD)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::DIAMOND_SWORD)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::GOLDEN_SWORD)),
    };

    const BLOCK_WDATA user_items_page_2[] = {
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::IRON_HELMET)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::IRON_CHESTPLATE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::IRON_LEGGINGS)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::IRON_BOOTS)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::GOLDEN_HELMET)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::GOLDEN_CHESTPLATE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::GOLDEN_LEGGINGS)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::GOLDEN_BOOTS)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::DIAMOND_HELMET)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::DIAMOND_CHESTPLATE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::DIAMOND_LEGGINGS)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::DIAMOND_BOOTS)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::ARROW)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::APPLE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::GOLDEN_APPLE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::MAP)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::BOOK)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::PAPER)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::COMPASS)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::CLOCK)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::OAK_DOOR)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::IRON_DOOR)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::MELON_SLICE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::PUMPKIN_PIE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::COOKIE)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::RAW_BEEF)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::COOKED_BEEF)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::RAW_CHICKEN)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::COOKED_CHICKEN)),
        getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::ROTTEN_FLESH)),
    };

    constexpr int user_selectable_count = sizeof(user_selectable) / sizeof(*user_selectable);
    constexpr int user_items_page_1_count = sizeof(user_items_page_1) / sizeof(*user_items_page_1);
    constexpr int user_items_page_2_count = sizeof(user_items_page_2) / sizeof(*user_items_page_2);

    /**
     * A tab: the page's name, its entries, and the item drawn on the tab itself
     * -- vanilla puts a category's own item on its tab, and this does the same
     * with the first thing in the page.
     */
    struct Page
    {
        const char *name;
        const BLOCK_WDATA *entries;
        int count;
        BLOCK_WDATA icon;
    };

    const Page selectable_pages[] = {
        {"Blocks",  user_selectable,    user_selectable_count,    BLOCK_GRASS},
        {"Items",   user_items_page_1,  user_items_page_1_count,  user_items_page_1[0]},
        {"Tools",   user_items_page_2,  user_items_page_2_count,  user_items_page_2[0]},
    };

    constexpr int selectable_page_count = sizeof(selectable_pages) / sizeof(*selectable_pages);

    // --- colours taken from the vanilla tooltip and container label --------

    /** A container title is dark grey on the light window. */
    constexpr COLOR title_color = 0x4208; // rgb(64,64,64)
    /** The tooltip's own dark background and its purple border. */
    constexpr COLOR tooltip_bg = 0x1002;  // rgb(16,0,16)
    constexpr COLOR tooltip_border = 0x501F; // rgb(80,0,255)
    /** The hover highlight, which vanilla draws as a light overlay. */
    constexpr COLOR cursor_color = 0xFFFF;

    /** The 16-pixel icon that goes inside a slot, drawn at `size`. */
    void drawEntryIcon(BLOCK_WDATA entry, TEXTURE &tex, int x, int y, int size)
    {
        if(getBLOCK(entry) == BLOCK_ITEM)
        {
            drawItemIcon(entry, tex, x, y, size);
            return;
        }

#ifdef _TINSPIRE
        // A calculator slot is 16 pixels, so the flat (resized) block face is used
        // and scaled into it, the same way the player inventory does it.
        const TextureAtlasEntry &icon = global_block_renderer.materialTexture(entry).resized;
        drawTexture(*terrain_resized, tex,
                    icon.left, icon.top, icon.right - icon.left, icon.bottom - icon.top,
                    x, y, size, size);
#else
        // The desktop runs at twice the scale, so its slots are 32 pixels and the
        // 24-pixel block preview fits inside one.
        const int preview = 24;
        global_block_renderer.drawPreview(entry, tex, x + (size - preview) / 2, y + (size - preview) / 2);
#endif
    }

    /** A stored stack: its icon, its count and its wear, as the inventory draws them. */
    void drawStack(BLOCK_WDATA block, unsigned int count, unsigned short damage,
                   TEXTURE &tex, int x, int y, int size)
    {
        if(getBLOCK(block) == BLOCK_AIR || count == 0)
            return;

        drawEntryIcon(block, tex, x, y, size);

        char count_text[12];
        snprintf(count_text, sizeof(count_text), "%u", count);
        drawString(count_text, 0xFFFF, tex, x + size - 10, y + 1);

        Inventory::drawDurabilityBar(tex, block, damage, x, y, size);
    }
}

BlockListTask::BlockListTask()
{
}

BlockListTask::~BlockListTask()
{
}

void BlockListTask::makeCurrent()
{
    // The save file keeps one selection, and it may belong to a tab that has
    // since become shorter; clamp it so the cursor is never past the end.
    current_selection = std::min(current_selection, std::max(0, selectable_pages[current_page].count - 1));
    scroll = scrollFor(current_selection, selectable_pages[current_page].count, scroll);

    if(!background_saved)
        saveBackground();

    Task::makeCurrent();
}

void BlockListTask::openFrom(Task *from)
{
    return_task = from;
    makeCurrent();
}

void BlockListTask::close()
{
    Task *back = return_task;
    return_task = nullptr;

    if(back != nullptr)
        back->makeCurrent();
    else
        world_task.makeCurrent();
}

int BlockListTask::pageCount()
{
    return selectable_page_count;
}

const char *BlockListTask::pageName(int page)
{
    if(page < 0 || page >= selectable_page_count)
        return "";
    return selectable_pages[page].name;
}

int BlockListTask::pageEntryCount(int page)
{
    if(page < 0 || page >= selectable_page_count)
        return 0;
    return selectable_pages[page].count;
}

int BlockListTask::rowsFor(int count)
{
    if(count <= 0)
        return 0;
    return (count + GridColumns - 1) / GridColumns;
}

int BlockListTask::maxScroll(int page)
{
    const int rows = rowsFor(pageEntryCount(page));
    const int extra = rows - visible_rows;
    return extra > 0 ? extra : 0;
}

int BlockListTask::scrollFor(int selection, int page_count, int current_scroll)
{
    const int rows = rowsFor(page_count);
    int max = rows - visible_rows;
    if(max < 0)
        max = 0;

    const int row = selection >= 0 ? selection / GridColumns : 0;
    int scroll = current_scroll;

    // Move as little as possible: the grid does not jump while the selection
    // stays inside the window, and steps by a row when it leaves it.
    if(row < scroll)
        scroll = row;
    else if(row >= scroll + visible_rows)
        scroll = row - visible_rows + 1;

    if(scroll > max)
        scroll = max;
    if(scroll < 0)
        scroll = 0;
    return scroll;
}

int BlockListTask::Layout::handleY(int scroll, int max_scroll) const
{
    if(max_scroll <= 0)
        return scroll_y;
    // Vanilla's own travel: the handle sweeps the track less its own height and
    // the two pixels it is inset from the bottom, i.e. (112 - 17) at scale 1.
    const int travel = scroll_track - handle_h - 2 * scale;
    if(travel <= 0)
        return scroll_y;
    return scroll_y + scroll * travel / max_scroll;
}

BlockListTask::Layout BlockListTask::layout()
{
    Layout out;
    out.scale = MenuUI::uiScale();
    const int s = out.scale;

    out.window_w = WindowWidth * s;
    out.window_h = WindowHeight * s;
    out.window_x = (SCREEN_WIDTH - out.window_w) / 2;
    out.window_y = (SCREEN_HEIGHT - out.window_h) / 2;

    out.tab_w = TabWidth * s;
    out.tab_h = TabHeight * s;
    out.tab_stride = TabStride * s;
    out.tab_y = out.window_y - TabOverhang * s;

    out.grid_x = out.window_x + GridLeft * s;
    out.grid_y = out.window_y + GridTop * s;
    out.slot_pitch = SlotPitch * s;
    out.slot_size = SlotSize * s;

    out.hotbar_y = out.window_y + HotbarTop * s;

    out.scroll_x = out.window_x + ScrollLeft * s;
    out.scroll_y = out.window_y + ScrollTop * s;
    out.scroll_track = ScrollTrack * s;
    out.handle_h = ScrollHandle * s;
    return out;
}

void BlockListTask::render()
{
    drawBackground();

    // CreativeModeInventoryScreen is an AbstractContainerScreen, so the world
    // behind it is washed in the same gradient the other in-game containers use
    // (Screen.renderBackground) rather than left at full brightness.
    MenuUI::drawPauseOverlay(*screen);

    const Layout l = layout();
    const Page &page = selectable_pages[current_page];
    const int scale = l.scale;

    // The window itself, with the slot grid, the hotbar and the scrollbar track
    // baked into the art.
    drawTexture(creative_window, *screen,
                0, 0, creative_window.width, creative_window.height,
                l.window_x, l.window_y, l.window_w, l.window_h);

    // The tab strip. Every tab is drawn unselected first and the selected one is
    // laid over it from the sheet's lighter band, which is how vanilla draws them.
    for(int i = 0; i < selectable_page_count; ++i)
    {
        const bool selected = (i == current_page);
        const int src_x = TabWidth * i;
        const int src_y = selected ? TabHeight : 0;
        drawTexture(creative_tabs, *screen,
                    src_x, src_y, TabWidth, TabHeight,
                    l.tabX(i), l.tab_y, l.tab_w, l.tab_h);
    }

    // The tab's own icon, at the offset vanilla puts it: six pixels in, nine
    // down (the top row adds one to the base eight).
    const int icon_size = SlotSize * scale;
    for(int i = 0; i < selectable_page_count; ++i)
        drawEntryIcon(selectable_pages[i].icon, *screen,
                      l.tabX(i) + 6 * scale, l.tab_y + 9 * scale, icon_size);

    // The grid, scrolled so the cursor is on screen.
    for(int row = 0; row < visible_rows; ++row)
    {
        for(int column = 0; column < GridColumns; ++column)
        {
            const int index = (scroll + row) * GridColumns + column;
            if(index >= page.count)
                continue;

            drawEntryIcon(page.entries[index], *screen, l.slotX(column), l.slotY(row), l.slot_size);
        }
    }

    // The cursor. Vanilla hovers with a translucent light fill; nGL cannot blend,
    // so the same slot is marked with a light outline instead.
    const int cursor_row = current_selection / GridColumns;
    const int visible_row = cursor_row - scroll;
    if(visible_row >= 0 && visible_row < visible_rows)
        drawRectangle(*screen,
                      l.slotX(current_selection % GridColumns), l.slotY(visible_row),
                      l.slot_size, l.slot_size, cursor_color);

    // The player's hotbar, drawn inside the window where the creative inventory
    // keeps it rather than as the HUD's own widget.
    for(int slot = 0; slot < Inventory::hotbar_slot_count; ++slot)
    {
        const int x = l.grid_x + slot * l.slot_pitch;
        const int y = l.hotbar_y;
        drawStack(current_inventory.slotBlock(slot), current_inventory.slotCount(slot),
                  current_inventory.slotDamage(slot), *screen, x, y, l.slot_size);

        if(slot == current_inventory.currentSlotIndex())
            drawRectangle(*screen, x, y, l.slot_size, l.slot_size, cursor_color);
    }

    // The scrollbar handle, when the tab is longer than the grid.
    const int max_scroll = maxScroll(current_page);
    if(max_scroll > 0)
        drawTexture(creative_scroll, *screen,
                    0, 0, creative_scroll.width, creative_scroll.height,
                    l.scroll_x, l.handleY(scroll, max_scroll),
                    creative_scroll.width * scale, creative_scroll.height * scale);

    // The tab's name where a container window puts its label, in the dark grey
    // vanilla uses on the light window.
    drawString(page.name, title_color, *screen, l.window_x + GridLeft * scale, l.window_y + 6 * scale);

    // The held entry's name, in a vanilla tooltip box under the window. It is the
    // keyboard's stand-in for the name a mouse would hover up.
    if(page.count > 0)
    {
        const char *name = global_block_renderer.getName(page.entries[current_selection]);
        const int text_w = static_cast<int>(measureString(name));
        const int box_w = text_w + 8;
        const int box_h = static_cast<int>(fontHeight()) + 6;
        const int box_x = (SCREEN_WIDTH - box_w) / 2;
        const int box_y = l.window_y + l.window_h + 4 * scale;

        MenuUI::fillRect(*screen, box_x, box_y, box_w, box_h, tooltip_bg);
        drawRectangle(*screen, box_x, box_y, box_w, box_h, tooltip_border);
        drawString(name, MenuUI::Text, *screen, box_x + 4, box_y + 3);
    }

    // The controls, in the front-end's own small print along the bottom.
    MenuUI::drawSmallPrint("7/9 Tab   2-8-4-6 Move   5 Take   1/3 Slot   ./ESC Close",
                           *screen, 1, SCREEN_HEIGHT - static_cast<int>(fontHeight()) - 1);
}

void BlockListTask::logic(GLFix /*dt*/)
{
    if(key_held_down)
        key_held_down = keyPressed(KEY_NSPIRE_ESC) || keyPressed(KEY_NSPIRE_PERIOD) || keyPressed(KEY_NSPIRE_2) || keyPressed(KEY_NSPIRE_8) || keyPressed(KEY_NSPIRE_4) || keyPressed(KEY_NSPIRE_6) || keyPressed(KEY_NSPIRE_7) || keyPressed(KEY_NSPIRE_9) || keyPressed(KEY_NSPIRE_1) || keyPressed(KEY_NSPIRE_3) || keyPressed(KEY_NSPIRE_5) || keyPressed(KEY_NSPIRE_UP) || keyPressed(KEY_NSPIRE_DOWN) || keyPressed(KEY_NSPIRE_LEFT) || keyPressed(KEY_NSPIRE_RIGHT)  || keyPressed(KEY_NSPIRE_CLICK) || keyPressed(KEY_NSPIRE_ENTER);
    else if(keyPressed(KEY_NSPIRE_ESC) || keyPressed(KEY_NSPIRE_PERIOD))
    {
        close();

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_7))
    {
        current_page = (current_page + selectable_page_count - 1) % selectable_page_count;
        current_selection = 0;
        scroll = 0;

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_9))
    {
        current_page = (current_page + 1) % selectable_page_count;
        current_selection = 0;
        scroll = 0;

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_2) || keyPressed(KEY_NSPIRE_DOWN))
    {
        const int page_count = selectable_pages[current_page].count;
        current_selection += GridColumns;
        if(current_selection >= page_count)
            current_selection %= GridColumns;
        scroll = scrollFor(current_selection, page_count, scroll);

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_8) || keyPressed(KEY_NSPIRE_UP))
    {
        const int page_count = selectable_pages[current_page].count;
        if(current_selection >= GridColumns)
            current_selection -= GridColumns;
        else
        {
            current_selection = ((page_count - 1) / GridColumns) * GridColumns + (current_selection % GridColumns);
            if(current_selection >= page_count)
                current_selection -= GridColumns;
        }
        scroll = scrollFor(current_selection, page_count, scroll);

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_4) || keyPressed(KEY_NSPIRE_LEFT))
    {
        const int page_count = selectable_pages[current_page].count;
        if(current_selection % GridColumns == 0)
        {
            current_selection += GridColumns - 1;
            if(current_selection >= page_count)
                current_selection = page_count - 1;
        }
        else
            current_selection--;

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_6) || keyPressed(KEY_NSPIRE_RIGHT))
    {
        const int page_count = selectable_pages[current_page].count;
        if(current_selection % GridColumns != GridColumns - 1 && current_selection < page_count - 1)
            current_selection++;
        else
            current_selection -= current_selection % GridColumns;

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_1)) //Switch inventory slot
    {
        current_inventory.previousSlot();

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_3))
    {
        current_inventory.nextSlot();

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_5) || keyPressed(KEY_NSPIRE_CLICK) || keyPressed(KEY_NSPIRE_ENTER))
    {
        const Page &page = selectable_pages[current_page];
        if(page.count > 0)
            current_inventory.setCurrentSlot(page.entries[current_selection], 64);

        key_held_down = true;
    }
}
