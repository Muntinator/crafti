#include "worlditems.h"

#include <cstdio>

#include "audio_manager.h"
#include "audio_sounds.h"
#include "bed.h"
#include "cheststore.h"
#include "chunk.h" // structureChestAt
#include "grounddrops.h"
#include "inventory.h"
#include "itemrules.h"
#include "structuregen.h"
#include "world.h"
#include "worldtask.h"

bool wearHeldItem(int amount)
{
    if(amount <= 0)
        return false;

    const BLOCK_WDATA held = current_inventory.currentSlot();
    if(ItemRules::maxDamage(held) <= 0)
        return false;

    // Unbreaking gives the item a chance to ignore the use entirely, which is
    // what makes it worth more than a bigger durability bar. Every use goes
    // through here -- mining, attacking, placing -- so the roll is in one place,
    // and it is the *item's* enchantment that is asked, not the player's.
    const int unbreaking = current_inventory.currentSlotEnchant().levelOf(Enchanting::Unbreaking);
    if(unbreaking > 0 && (rand() % 100) >= Enchanting::unbreakingKeepPercent(unbreaking))
        return false;

    if(!current_inventory.damageCurrentSlot(amount))
        return false;

    // It broke: the slot is empty now, so say what was lost.
    char message[40];
    snprintf(message, sizeof(message), "%s broke!", ItemRules::toolName(ItemRules::toolKind(held)));
    world_task.setMessage(message);
    GameAudio::playSound(GameAudio::Sound::RandomBreak);
    return true;
}

bool tryEquipHeldArmor()
{
    const BLOCK_WDATA held = current_inventory.currentSlot();
    if(ItemRules::armorSlot(held) == ItemRules::NoArmorSlot)
        return false;

    // Wearing armour swaps out whatever was in that slot, which goes back into
    // the inventory in place of the piece that was put on.
    if(!current_inventory.equipArmorFromSlot(current_inventory.currentSlotIndex()))
        return false;

    GameAudio::playSound(GameAudio::Sound::ItemArmorEquipGeneric1);
    world_task.setMessage("Armour equipped");
    return true;
}

void placeChestAt(int x, int y, int z)
{
    ChestStore::place(x, y, z);

    // Two chests side by side share one container, like vanilla's double chest.
    // Only the four horizontal neighbours can pair, and a chest that is already
    // paired is left alone.
    static const int offsets[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    for(int i = 0; i < 4; ++i)
    {
        const int nx = x + offsets[i][0];
        const int nz = z + offsets[i][1];
        if(getBLOCK(world.getBlock(nx, y, nz)) != BLOCK_CHEST)
            continue;

        // A chest that predates this feature (or came from an older save) has no
        // storage yet; give it one so the pair can be formed -- and if it is a
        // chest the world generated it keeps (and now gets) its loot.
        ensureChestAt(nx, y, nz);

        if(ChestStore::isLarge(nx, y, nz))
            continue;

        if(ChestStore::link(x, y, z, nx, y, nz))
            return;
    }
}

bool tryPlaceBed(int x, int y, int z, int yaw_degrees)
{
    const BLOCK_SIDE facing = Bed::facingForYaw(yaw_degrees);
    if(!Bed::isHorizontal(facing))
        return false;

    int dx, dy, dz;
    Bed::sideOffset(facing, dx, dy, dz);

    const int head_x = x + dx, head_y = y + dy, head_z = z + dz;

    // Both cells have to be free. A placement that only half succeeded would
    // leave a bed that cannot be slept in and cannot be pointed at, and the
    // player would have paid a bed for it.
    if(world.getBlock(x, y, z) != BLOCK_AIR)
        return false;
    if(world.getBlock(head_x, head_y, head_z) != BLOCK_AIR)
        return false;

    world.changeBlock(x, y, z, getBLOCKWDATA(BLOCK_BED, Bed::dataFor(facing, false)));
    world.changeBlock(head_x, head_y, head_z, getBLOCKWDATA(BLOCK_BED, Bed::dataFor(facing, true)));

    // The head can land where the player is standing, since it is a cell further
    // along than the one that was clicked. That is the same failure the ordinary
    // placement path undoes for a single block, and here it has to take both
    // halves back -- a bed that traps its owner is worse than no bed.
    AABB player = world_task.playerBox();
    if(world.intersect(player))
    {
        world.changeBlock(x, y, z, BLOCK_AIR);
        world.changeBlock(head_x, head_y, head_z, BLOCK_AIR);
        return false;
    }

    return true;
}

void ensureChestAt(int x, int y, int z)
{
    if(ChestStore::exists(x, y, z))
        return;

    ChestStore::place(x, y, z);

    Structures::Plan plan;
    int index = 0;
    if(!structureChestAt(x, y, z, plan, index))
        return; // a chest the player put there, or one from an older save

    Structures::Loot loot[Structures::MaxLootStacks];
    const int stacks = Structures::chestLoot(plan, index, loot, Structures::MaxLootStacks);
    for(int i = 0; i < stacks; ++i)
    {
        if(getBLOCK(loot[i].stack) == BLOCK_AIR || loot[i].count == 0)
            continue;
        ChestStore::addStack(x, y, z, loot[i].stack, loot[i].count);
    }
}

void breakChestAt(int x, int y, int z)
{
    // Breaking a chest the player never opened must still spill its loot, so the
    // container is created (and seeded) before it is removed.
    ensureChestAt(x, y, z);

    ChestStore::Stack dropped[ChestStore::LargeSlotCount];
    const int stacks = ChestStore::remove(x, y, z, dropped, ChestStore::LargeSlotCount);

    const GLFix cx = GLFix(x * BLOCK_SIZE) + GLFix(BLOCK_SIZE / 2);
    const GLFix cy = GLFix(y * BLOCK_SIZE) + GLFix(BLOCK_SIZE) + GLFix::minStep();
    const GLFix cz = GLFix(z * BLOCK_SIZE) + GLFix(BLOCK_SIZE / 2);

    // Whatever was inside falls out the way a broken block drops: one entity per
    // stack, keeping each tool's wear.
    for(int i = 0; i < stacks; ++i)
    {
        if(getBLOCK(dropped[i].block) == BLOCK_AIR || dropped[i].count == 0)
            continue;
        spawnWorldDrop(cx, cy, cz, dropped[i].block, dropped[i].count, dropped[i].damage);
    }
}

void clearChests()
{
    ChestStore::clear();
}
