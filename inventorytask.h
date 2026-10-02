#ifndef INVENTORYTASK_H
#define INVENTORYTASK_H

#include <array>
#include <map>
#include <tuple>

#include "task.h"
#include "terrain.h"

class World;

/** Per-position furnace state (MCP TileEntityFurnace: 3 stacks + burn/cook timers). */
struct FurnaceTileData
{
    std::array<std::pair<BLOCK_WDATA, unsigned>, 3> stacks{};
    int burn_time = 0;
    int current_item_burn_time = 0;
    int cook_time = 0;
    int cook_time_total = 200;
};

class InventoryTask : public Task
{
public:
    void openPlayerInventory();
    /**
     * Opens the player inventory and remembers where closing should return to, so
     * a pause-menu button lands back on the pause menu rather than in the world.
     * The flag is cleared the next time the inventory closes, so an in-game open
     * still returns to the world.
     */
    void openPlayerInventoryFrom(Task *from);
    void openCraftingTable();
    void openFurnace(int block_x, int block_y, int block_z);
    /** Opens the chest at a block position (its contents live in cheststore.h). */
    void openChest(int block_x, int block_y, int block_z);
    void tickFurnaces(World &world);
    void ensureFurnaceTile(int block_x, int block_y, int block_z);
    void removeFurnaceTile(int block_x, int block_y, int block_z);
    virtual void makeCurrent() override;
    virtual void render() override;
    virtual void logic(GLFix dt) override;
    void reset();

private:
    // Inventory slot constants
    static constexpr int INVALID_SLOT = -1;
    static constexpr int CRAFTING_SLOT_OFFSET = 36; // After hotbar (9) + storage (27)
    static constexpr int CRAFTING_INPUT_COUNT = 9; // Up to 3x3 grid
    static constexpr int CRAFTING_OUTPUT_SLOT = CRAFTING_SLOT_OFFSET + CRAFTING_INPUT_COUNT;
    static constexpr int FURNACE_INPUT_SLOT = 46;
    static constexpr int FURNACE_FUEL_SLOT = 47;
    static constexpr int FURNACE_OUTPUT_SLOT = 48;
    // The chest slots come after the furnace's, so one slot number addresses one
    // widget no matter which container is open. The armour widgets sit above even
    // those, so a chest slot can never be mistaken for a worn piece.
    static constexpr int CHEST_SLOT_OFFSET = 49;
    static constexpr int ARMOR_SLOT_OFFSET = CHEST_SLOT_OFFSET + 54;
    /** The single offhand widget, above the four armour ones. */
    static constexpr int OFFHAND_SLOT = ARMOR_SLOT_OFFSET + 4; // Inventory::armor_slot_count

    void activate();
    /** Closes the window, back to wherever it was opened from. */
    void close();
    int slotFromMouse(int mouse_x, int mouse_y) const;
    /**
     * Top-left corner of the window render() draws, in screen pixels. The armour
     * widgets and the chest panel are laid out from it, which is why it is shared
     * rather than recomputed in each of the two files.
     */
    static int inventoryWindowX();
    static int inventoryWindowY();
    int craftingSlotFromMouse(int mouse_x, int mouse_y) const;
    int activeCraftingInputCount() const;
    int activeCraftingCols() const;
    int activeCraftingRows() const;
    void craftingGridBounds(int &x, int &y, int &w, int &h) const;
    void craftingOutputBounds(int &x, int &y, int &w, int &h) const;
    int furnaceSlotFromMouse(int mouse_x, int mouse_y) const;
    void furnaceSlotBounds(int slot_index, int &x, int &y, int &w, int &h) const;
    /** Player inventory slots 0–35 in vanilla 176×166 furnace GUI space (same as net.minecraft.inventory.ContainerFurnace). */
    void furnacePlayerSlotBounds(int player_slot, int &x, int &y, int &w, int &h) const;
    /** When input_slot_affected is true, MCP resets cook progress (input stack slot index 0). */
    void syncFurnaceStorage(bool reset_cook_for_input_slot = false);
    void consumeCraftingIngredients();
    void drawSlotItem(TEXTURE &tex, int slot, int x, int y);
    bool isHoldingItem() const;
    void handleLeftClick(int slot);
    void handleRightClick(int slot);
    void handleHalfPlace(int slot);
    void tryCraft();
    bool isCraftingSlot(int slot) const;

    // --- chest container (inventorychest.cpp) -------------------------------
    void renderChestPanel();
    /** The 27 or 54 slots the open chest has, or 0 when it has none. */
    int chestSlotCount() const;
    int chestSlotFromMouse(int mouse_x, int mouse_y) const;
    void chestSlotBounds(int chest_slot, int &x, int &y, int &w, int &h) const;
    void chestPlayerSlotBounds(int player_slot, int &x, int &y, int &w, int &h) const;
    void chestPanelRect(int &x, int &y, int &w, int &h) const;
    /** Picks a stack up, or puts the held one down, in a chest slot. */
    void chestHandleClick(int slot, bool right_click);
    /** Draws any stack with its count and its wear bar. */
    void drawStackItem(TEXTURE &tex, BLOCK_WDATA block, unsigned int count, unsigned short damage, int x, int y, int size);

    // --- worn armour widgets ------------------------------------------------
    void renderArmorWidgets();
    bool armorWidgetBounds(int index, int &x, int &y, int &w, int &h) const;
    /** The armour widget under the mouse, or INVALID_SLOT. */
    int armorSlotFromMouse(int mouse_x, int mouse_y) const;
    /** Takes a worn piece onto the cursor, or puts the held one on. */
    void armorHandleClick(int index);

    // --- offhand widget (inventorychest.cpp) --------------------------------
    /**
     * Draws the offhand stack, or the shield outline when it is empty. Only the
     * player's own window has the slot: vanilla's InventoryMenu is the one menu
     * that carries it, not the crafting table's or the furnace's.
     */
    void renderOffhandWidget();
    bool offhandWidgetBounds(int &x, int &y, int &w, int &h) const;
    /** The offhand widget under the mouse, or INVALID_SLOT. */
    int offhandSlotFromMouse(int mouse_x, int mouse_y) const;
    /**
     * Picks the offhand stack up, or puts the held one down, exactly as a plain
     * inventory slot does -- the offhand takes any item, not just a shield.
     */
    void offhandHandleClick(bool right_click);

    /**
     * Where closing returns to: null means the world, and a pause-menu open sets
     * it to the pause menu. Cleared as soon as the window closes.
     */
    Task *return_task = nullptr;

    BLOCK_WDATA held_block = BLOCK_AIR;
    unsigned int held_count = 0;
    /** Wear of the stack on the cursor, so a used tool keeps it while it is moved. */
    unsigned short held_damage = 0;
    bool left_mouse_was_down = false;
    bool right_mouse_was_down = false;

    int cursor_x = SCREEN_WIDTH / 2;
    int cursor_y = SCREEN_HEIGHT / 2;
    bool nspire_select_was_down = false;
    bool nspire_single_was_down = false;
    bool nspire_half_was_down = false;
    bool nspire_tp_had_contact = false;
    uint16_t nspire_tp_last_x = 0;
    uint16_t nspire_tp_last_y = 0;
    bool crafting_table_mode = false;
    bool furnace_mode = false;
    int furnace_bx = 0, furnace_by = 0, furnace_bz = 0;
    BLOCK_WDATA furnace_slots[3] = {};
    unsigned int furnace_counts[3] = {};
    std::map<std::tuple<int, int, int>, FurnaceTileData> furnace_tiles;

    /** True while a chest is open; its contents live in cheststore.h. */
    bool chest_mode = false;
    int chest_bx = 0, chest_by = 0, chest_bz = 0;

    // Crafting table
    BLOCK_WDATA crafting_input[CRAFTING_INPUT_COUNT] = {};
    unsigned int crafting_counts[CRAFTING_INPUT_COUNT] = {};
    BLOCK_WDATA crafting_output = BLOCK_AIR;
    unsigned int crafting_output_count = 0;
    int matched_recipe_slots[CRAFTING_INPUT_COUNT] = {};
    int matched_recipe_slot_count = 0;
};

extern InventoryTask inventory_task;

#endif // INVENTORYTASK_H
