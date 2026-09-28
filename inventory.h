#ifndef INVENTORY_H
#define INVENTORY_H

#include "gl.h"
#include "terrain.h"
#include "enchanting.h"

class Inventory
{
public:
    Inventory();

    void draw(TEXTURE &tex);
    /**
     * The small green-to-red wear bar under an item widget. Nothing is drawn for
     * an item that cannot break or that is still unused.
     */
    static void drawDurabilityBar(TEXTURE &tex, BLOCK_WDATA block, unsigned short worn, int x, int y, int size);

    static unsigned int height();
    BLOCK_WDATA currentSlot() const;
    unsigned int currentSlotCount() const;
    /** Wear of the held stack: 0 for a fresh tool, maxDamage() when it breaks. */
    unsigned short currentSlotDamage() const;
    int currentSlotIndex() const { return current_slot; }

    void setCurrentSlot(BLOCK_WDATA block, unsigned int count = 1);
    bool addItem(BLOCK_WDATA block, unsigned int count = 1);
    /** Same, but keeps the wear of the item being stored. */
    bool addItemWithDamage(BLOCK_WDATA block, unsigned int count, unsigned short damage);
    bool removeFromCurrentSlot(unsigned int count = 1);
    void importLegacyCounts();

    BLOCK_WDATA slotBlock(int slot) const;
    unsigned int slotCount(int slot) const;
    unsigned short slotDamage(int slot) const;
    void setSlot(int slot, BLOCK_WDATA block, unsigned int count);
    void setSlotWithDamage(int slot, BLOCK_WDATA block, unsigned int count, unsigned short damage);
    void swapSlots(int a, int b);

    /**
     * Wears the item in a slot. Returns true when it broke, in which case the slot
     * has already been emptied: a broken tool leaves nothing behind.
     */
    bool damageSlot(int slot, int amount);
    bool damageCurrentSlot(int amount);

    // --- armour ------------------------------------------------------------

    static constexpr int armor_slot_count = 4; // helmet, chest, legs, boots

    /** What is worn; counts are 0 or 1, damage is the wear of the piece. */
    BLOCK_WDATA armor[armor_slot_count] = {};
    unsigned int armor_counts[armor_slot_count] = {};
    unsigned short armor_damage[armor_slot_count] = {};

    /** Enchantments of the worn pieces, in the same order as the armour itself. */
    Enchanting::Set armor_enchant[armor_slot_count] = {};
    /** Enchantments on the held stack, which is what a table or an anvil works on. */
    const Enchanting::Set &currentSlotEnchant() const { return enchant[current_slot]; }
    Enchanting::Set &currentSlotEnchant() { return enchant[current_slot]; }
    const Enchanting::Set &slotEnchant(int slot) const;
    Enchanting::Set &slotEnchant(int slot);
    void setSlotEnchant(int slot, const Enchanting::Set &set);
    /** Puts the same enchantments into every slot holding this stack kind... */
    void setCurrentSlotEnchant(const Enchanting::Set &set) { enchant[current_slot] = set; }

    /** Total armour points of the worn pieces (0..20). */
    int totalArmorPoints() const;
    BLOCK_WDATA armorBlock(int index) const;
    unsigned int armorCount(int index) const;
    unsigned short armorDamage(int index) const;
    void setArmorSlot(int index, BLOCK_WDATA block, unsigned int count, unsigned short damage);
    /**
     * Wears every worn piece once and removes the ones that break. Returns true
     * when at least one piece broke, so the caller can say so.
     */
    bool damageArmor();
    /** Wears armour from a slot of this inventory, swapping with what is worn. */
    bool equipArmorFromSlot(int slot);
    void clearArmor();

    void previousSlot();
    void nextSlot();
    void reset();

    static constexpr int hotbar_slot_count = 9;
    static constexpr int storage_slot_count = 27;
    static constexpr int slot_count = hotbar_slot_count + storage_slot_count;
    BLOCK_WDATA entries[slot_count] = {};
    unsigned int counts[slot_count] = {};
    /** Wear of each slot, in parallel with entries/counts. */
    unsigned short damage[slot_count] = {};
    /**
     * Enchantments of each slot, in parallel with the rest. They travel with the
     * stack -- swapped, dropped, put in a chest, saved and loaded -- which is what
     * makes an enchanted sword an item rather than an inventory position. The
     * rules for what they are worth live in enchanting.h.
     */
    Enchanting::Set enchant[slot_count] = {};
    int current_slot = 0;
};

extern Inventory current_inventory;

#endif // INVENTORY_H
