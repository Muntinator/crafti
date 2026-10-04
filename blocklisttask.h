#ifndef BLOCKLISTTASK_H
#define BLOCKLISTTASK_H

#include "task.h"

/**
 * The block list, drawn as the vanilla creative inventory.
 *
 * The screen is the official 1.17.1 `gui/container/creative_inventory` art: the
 * 195x136 window, the tab strip along its top, the 9x5 slot grid, the scrollbar
 * and the hotbar. Its three pages are the three tabs, and a page longer than the
 * grid scrolls the way the creative item list does, with the same scrollbar.
 *
 * The geometry is vanilla's own, stated at GUI scale 1 and multiplied by the
 * engine's scale on the way out, so it lands where the real screen puts it: the
 * window centred, the grid at (9,18) on an 18-pixel pitch, the hotbar at y 112,
 * the scrollbar at x 175, and the tabs 28 pixels up and 29 apart.
 */
class BlockListTask : public Task
{
public:
    BlockListTask();
    virtual ~BlockListTask();

    virtual void makeCurrent() override;

    /**
     * Opens the screen from `from`, so closing it hands control back there. The
     * pause menu opens it as a child screen, the way vanilla opens Statistics
     * from the pause menu: closing it comes back to the pause menu.
     */
    void openFrom(Task *from);

    virtual void render() override;
    virtual void logic(GLFix dt) override;

    /** The catalogue entry the cursor is on, which is what the save file keeps. */
    int current_selection = 0;

    // --- the creative window, at GUI scale 1 (vanilla's numbers) ------------

    static constexpr int WindowWidth = 195;
    static constexpr int WindowHeight = 136;
    static constexpr int GridColumns = 9;
    static constexpr int GridRows = 5;
    static constexpr int SlotPitch = 18;   ///< one slot plus its gap
    static constexpr int SlotSize = 16;    ///< the icon inside a slot
    static constexpr int GridLeft = 9;     ///< window-relative
    static constexpr int GridTop = 18;
    static constexpr int HotbarTop = 112;  ///< window-relative
    static constexpr int ScrollLeft = 175; ///< window-relative
    static constexpr int ScrollTop = 18;
    static constexpr int ScrollTrack = 112;///< vanilla's track: y18 to y130
    static constexpr int ScrollHandle = 15;///< handle height in tabs.png
    static constexpr int TabWidth = 28;    ///< the tab cell in tabs.png
    static constexpr int TabHeight = 32;
    static constexpr int TabStride = 29;   ///< vanilla leaves a pixel between tabs
    static constexpr int TabOverhang = 28; ///< tabs stick up this far above the window

    /** The window's pixel geometry for the current GUI scale. */
    struct Layout
    {
        int scale = 1;
        int window_x = 0, window_y = 0, window_w = 0, window_h = 0;
        int tab_w = 0, tab_h = 0, tab_stride = 0, tab_y = 0;
        int grid_x = 0, grid_y = 0, slot_pitch = 0, slot_size = 0;
        int hotbar_y = 0;
        int scroll_x = 0, scroll_y = 0, scroll_track = 0, handle_h = 0;

        /** The top-left of the slot at (column, row). */
        int slotX(int column) const { return grid_x + column * slot_pitch; }
        int slotY(int row) const { return grid_y + row * slot_pitch; }
        /** A tab's top-left, i.e. the box above the window. */
        int tabX(int index) const { return window_x + index * tab_stride; }
        /** Where the scrollbar handle sits for a given scroll fraction. */
        int handleY(int scroll, int max_scroll) const;
    };

    static Layout layout();

    /** How many tabs (pages) the selector has. */
    static int pageCount();
    /** The name of a tab, drawn as the window's title. */
    static const char *pageName(int page);
    /** How many entries a tab holds. */
    static int pageEntryCount(int page);

    /** The rows `count` entries need in the grid. */
    static int rowsFor(int count);
    /** The scroll that keeps `selection`'s row inside the visible window. */
    static int scrollFor(int selection, int page_count, int current_scroll);

    /** The most a tab can scroll, which is 0 when it fits the grid. */
    static int maxScroll(int page);

private:
    /** The visible rows: the grid is five rows, and that is all that fits. */
    static constexpr int visible_rows = GridRows;

    /** Esc hands control back to the screen that opened this one, or the world. */
    void close();

    int current_page = 0;
    /** The top row on screen, which the selection is kept inside. */
    int scroll = 0;
    Task *return_task = nullptr;

#ifndef _TINSPIRE
#endif
};

extern BlockListTask block_list_task;

#endif // BLOCKLISTTASK_H
