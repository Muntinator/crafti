// The world side of the debug commands: /give, /time, /weather, /teleport,
// /setblock, /fill, /summon, /seed, /gamemode and /help.
//
// Everything here goes through the same calls the rest of the game uses --
// world.changeBlock, current_inventory.addItem, spawnWorldDrop, the entity
// vectors, WorldClock -- so a command has exactly the effect the equivalent
// action in the world would. Nothing reaches behind those calls into the
// save file, the meshes or the light caches: changeBlock already invalidates
// the chunk's geometry and light, which is why a /fill of a couple of thousand
// blocks stays correct as well as bounded.
//
// The block and item names are resolved against the tables the game already has
// (block_names in terrain.cpp, getItemName in itemicons.cpp) instead of a second
// list here. That is what keeps /give honest: a name that the inventory can show
// is a name the command accepts.

#include "worldcommands.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "command.h"
#include "creeperentity.h"
#include "fastmath.h"
#include "grounddrops.h"
#include "inventory.h"
#include "inventorytask.h"
#include "itemicons.h"
#include "itemrules.h"
#include "livestockentity.h"
#include "livestockspecies.h"
#include "settingstask.h"
#include "terrain.h"
#include "textures/items.h"
#include "weather.h"
#include "world.h"
#include "worldclock.h"
#include "worlditems.h"
#include "worldtask.h"

#include "enchanting.h"
#include "survival.h"

namespace
{
    /** Top world block, for the bounds every coordinate has to fall inside. */
    constexpr int max_world_y = World::HEIGHT * Chunk::SIZE - 1;

    /** How long /weather holds when it is not given a duration, in real seconds. */
    constexpr unsigned int DefaultWeatherSeconds = 600;

    /**
     * How far a bookshelf counts towards a table, in blocks: vanilla's shell is
     * two blocks out horizontally and one up or down, and only the ones level with
     * the table's own layer (or the one below) count.
     */
    constexpr int BookshelfReach = 2;
    constexpr int BookshelfRise = 1;

    /** One /summon may not push a species past a sane population. */
    constexpr unsigned int MaxSummonPopulation = 40;

    void setReply(char *reply, unsigned int size, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        vsnprintf(reply, size, format, args);
        va_end(args);
    }

    // --- names --------------------------------------------------------------

    /** Extra spellings for blocks whose displayed name is not what people type. */
    struct BlockAlias
    {
        const char *name;
        BLOCK id;
    };

    const BlockAlias block_aliases[] = {
        {"planks", BLOCK_PLANKS_NORMAL},
        {"wood planks", BLOCK_PLANKS_NORMAL},
        {"oak planks", BLOCK_PLANKS_NORMAL},
        {"log", BLOCK_WOOD},
        {"cobble", BLOCK_COBBLESTONE},
        {"grass block", BLOCK_GRASS},
        {"brick", BLOCK_WALL},
        {"bricks", BLOCK_WALL},
        {"tnt", BLOCK_TNT},
        {"diamond block", BLOCK_DIAMOND},
        {"iron block", BLOCK_IRON},
        {"gold block", BLOCK_GOLD},
    };

    /**
     * The special blocks, which have no entry in block_names because they are not
     * drawn by the plain block renderer. The data byte is left at zero: placement
     * recomputes it from the side the block is put against, so what /give stores
     * for a torch or a door never reaches the world.
     */
    struct SpecialBlock
    {
        const char *name;
        BLOCK id;
    };

    const SpecialBlock special_blocks[] = {
        {"torch", BLOCK_TORCH},
        {"flower", BLOCK_FLOWER},
        {"rose", BLOCK_FLOWER},
        {"spider web", BLOCK_SPIDERWEB},
        {"cobweb", BLOCK_SPIDERWEB},
        {"cake", BLOCK_CAKE},
        {"mushroom", BLOCK_MUSHROOM},
        {"door", BLOCK_DOOR},
        {"water", BLOCK_WATER},
        {"lava", BLOCK_LAVA},
        {"wheat", BLOCK_WHEAT},
        {"lamp", BLOCK_REDSTONE_LAMP},
        {"redstone lamp", BLOCK_REDSTONE_LAMP},
        {"lever", BLOCK_REDSTONE_SWITCH},
        {"switch", BLOCK_REDSTONE_SWITCH},
        {"wire", BLOCK_REDSTONE_WIRE},
        {"redstone wire", BLOCK_REDSTONE_WIRE},
        {"redstone torch", BLOCK_REDSTONE_TORCH},
        {"pressure plate", BLOCK_PRESSURE_PLATE},
    };

    /**
     * Turns a name (or a numeric id) into the stack to place or hand over.
     *
     * Order matters: a plain block first, then the shorthand aliases, then the
     * specials, and the item table last. The item table is last because its names
     * are longer and more specific ("Iron Ingot" against "Iron block"), and a
     * block should win when a player types a word that means one.
     */
    bool resolveStack(const char *name, bool allow_air, BLOCK_WDATA &out)
    {
        if(name == nullptr || name[0] == '\0')
            return false;

        int numeric;
        if(Command::parseInt(name, numeric))
        {
            if(numeric == 0)
            {
                out = BLOCK_AIR;
                return allow_air;
            }
            if(numeric >= 1 && numeric <= BLOCK_NORMAL_LAST)
            {
                out = getBLOCKWDATA(static_cast<BLOCK>(numeric), 0);
                return true;
            }
            // A numeric id only reaches the specials, never the gap between them:
            // ids with no renderer would put an untextured block in the world.
            for(unsigned int i = 0; i < sizeof(special_blocks) / sizeof(special_blocks[0]); ++i)
            {
                if(special_blocks[i].id == numeric)
                {
                    out = getBLOCKWDATA(static_cast<BLOCK>(numeric), 0);
                    return true;
                }
            }
            return false;
        }

        for(int i = 1; i <= BLOCK_NORMAL_LAST; ++i)
        {
            if(Command::namesMatch(name, block_names[i]))
            {
                out = getBLOCKWDATA(static_cast<BLOCK>(i), 0);
                return true;
            }
        }

        for(unsigned int i = 0; i < sizeof(block_aliases) / sizeof(block_aliases[0]); ++i)
        {
            if(Command::namesMatch(name, block_aliases[i].name))
            {
                out = getBLOCKWDATA(block_aliases[i].id, 0);
                return true;
            }
        }

        for(unsigned int i = 0; i < sizeof(special_blocks) / sizeof(special_blocks[0]); ++i)
        {
            if(Command::namesMatch(name, special_blocks[i].name))
            {
                out = getBLOCKWDATA(special_blocks[i].id, 0);
                return true;
            }
        }

        // The item atlas, up to the last tile it defines. Ids the atlas has no
        // name for answer with the "Item" placeholder rather than nothing, and
        // those are skipped so a typo cannot silently hand over tile 0.
        constexpr int last_item_id = static_cast<int>(ItemTexture::COOKED_SALMON);
        for(int id = 0; id <= last_item_id; ++id)
        {
            const char *item_name = getItemName(getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(id)));
            if(item_name != nullptr && strcmp(item_name, "Item") != 0 && Command::namesMatch(name, item_name))
            {
                out = getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(id));
                return true;
            }
        }

        return false;
    }

    bool isAirStack(BLOCK_WDATA stack)
    {
        return getBLOCK(stack) == BLOCK_AIR;
    }

    /**
     * A name for a stack, for the replies. Blocks come from the game's own table,
     * the specials from the table above and items from the atlas. The block table
     * only covers the plain blocks -- indexing it with a torch would read past its
     * end -- which is why the specials are asked separately.
     */
    const char *stackName(BLOCK_WDATA stack)
    {
        const BLOCK block = getBLOCK(stack);

        if(block == BLOCK_ITEM)
        {
            const char *name = getItemName(stack);
            return name != nullptr ? name : "item";
        }

        if(block <= BLOCK_NORMAL_LAST)
            return block_names[block];

        for(unsigned int i = 0; i < sizeof(special_blocks) / sizeof(special_blocks[0]); ++i)
        {
            if(special_blocks[i].id == block)
                return special_blocks[i].name;
        }

        return "block";
    }

    bool inWorldY(int y)
    {
        return y >= 0 && y <= max_world_y;
    }

    // --- individual commands -------------------------------------------------

    void commandHelp(const Command::Parsed &parsed, char *reply, unsigned int size)
    {
        if(parsed.arg_count == 0)
        {
            // The command list is longer than one line, so it is clipped: the
            // console shows the hint line for whatever is being typed as well.
            char list[128];
            Command::formatHelpAll(list, sizeof(list));
            setReply(reply, size, "%s", list);
            return;
        }

        const int index = Command::commandIndex(parsed.args[0]);
        if(index < 0)
        {
            setReply(reply, size, "No such command: %s", parsed.args[0]);
            return;
        }

        char line[64];
        Command::formatHelp(index, line, sizeof(line));
        setReply(reply, size, "%s: %s", line, Command::commandSummary(index));
    }

    void commandGive(const Command::Parsed &parsed, char *reply, unsigned int size)
    {
        if(parsed.arg_count < 1)
        {
            setReply(reply, size, "Usage: /give <block|item> [count]");
            return;
        }

        BLOCK_WDATA stack;
        if(!resolveStack(parsed.args[0], false, stack))
        {
            setReply(reply, size, "Unknown block or item: %s", parsed.args[0]);
            return;
        }

        int count = 1;
        if(parsed.arg_count >= 2 && !Command::parseInt(parsed.args[1], count))
        {
            setReply(reply, size, "Not a count: %s", parsed.args[1]);
            return;
        }

        // A stack, not a chest: the limit is the item's own, and a tool is one.
        const int limit = ItemRules::maxStackSize(stack);
        if(count < 1)
            count = 1;
        if(count > limit)
            count = limit;

        if(current_inventory.addItem(stack, static_cast<unsigned int>(count)))
        {
            setReply(reply, size, "Gave %d %s", count, stackName(stack));
            return;
        }

        // Refusing outright would be unhelpful on a full inventory, so the stack
        // is dropped where the player stands, which is also how the game already
        // hands back what a broken chest held.
        spawnWorldDrop(world_task.x, world_task.y, world_task.z, stack, static_cast<unsigned int>(count));
        setReply(reply, size, "Inventory full - dropped it");
    }

    /**
     * The bookshelves around the player, as vanilla's enchanting table counts them:
     * a 5x5 shell two blocks out, one block up and one block down, and only the
     * ring -- the space directly between the table and a shelf has to be clear, but
     * a shelf with anything beside it still counts, which is why the ring is
     * counted rather than the whole box.
     *
     * The rule is a world question, not a rule of the enchantment module: the module
     * is handed the number and only knows that fifteen is the cap (`bookshelfPower`).
     */
    int countBookshelves()
    {
        const int centre_x = (world_task.x / BLOCK_SIZE).floor();
        const int centre_y = (world_task.y / BLOCK_SIZE).floor();
        const int centre_z = (world_task.z / BLOCK_SIZE).floor();

        int shelves = 0;
        for(int dy = -BookshelfRise; dy <= BookshelfRise; ++dy)
            for(int dz = -BookshelfReach; dz <= BookshelfReach; ++dz)
                for(int dx = -BookshelfReach; dx <= BookshelfReach; ++dx)
                {
                    // Only the shell: the two blocks straight out in each direction,
                    // and the corners of that square.
                    if(dx > -BookshelfReach && dx < BookshelfReach
                       && dz > -BookshelfReach && dz < BookshelfReach)
                        continue;
                    if(dy != 0 && (dx == 0 || dz == 0))
                        continue;
                    if(getBLOCK(world.getBlock(centre_x + dx, centre_y + dy, centre_z + dz)) == BLOCK_BOOKSHELF)
                        ++shelves;
                }

        return shelves;
    }

    /** The levels the player is standing at, from the one running experience total. */
    int playerLevel()
    {
        return Survival::levelFromTotalXp(world_task.experience());
    }

    /** Spends whole levels, which is how a table and an anvil charge. */
    bool spendLevels(int levels)
    {
        if(levels <= 0)
            return true;

        const int have = playerLevel();
        if(have < levels)
            return false;

        world_task.setExperience(Survival::totalXpForLevel(have - levels));
        return true;
    }

    /**
     * The three offers for the held item. The seed is built from the world, the
     * item and the player's level, so the offers hold still while the item is in
     * hand (a table does not re-roll under your fingers) but a different item, a
     * different world or a level-up gives different spells, which is the
     * behaviour that makes enchanting a gamble rather than a lookup.
     */
    void heldItemOffers(Enchanting::Offer offers[Enchanting::OfferCount], uint8_t &item_id)
    {
        const BLOCK_WDATA held = current_inventory.currentSlot();
        const BLOCK held_block = getBLOCK(held);

        item_id = 0xFF;
        if(held_block != BLOCK_ITEM)
        {
            for(int i = 0; i < Enchanting::OfferCount; ++i)
            {
                offers[i].level = 0;
                offers[i].set.clear();
                offers[i].available = false;
            }
            return;
        }

        item_id = getITEMDATA(held);
        const Enchanting::ItemType type = Enchanting::typeOfItem(item_id);
        const int enchantability = Enchanting::enchantability(item_id);
        const int power = Enchanting::bookshelfPower(countBookshelves());

        uint32_t seed = world.seedValue() * 2654435761u;
        seed ^= static_cast<uint32_t>(item_id) * 40503u;
        seed ^= static_cast<uint32_t>(current_inventory.currentSlotCount()) * 2246822519u;
        seed ^= static_cast<uint32_t>(playerLevel()) * 3266489917u;

        Enchanting::rollOffers(seed, power, enchantability, type, offers);
    }

    /** The Roman numeral an enchantment's level is shown as. */
    const char *romanLevel(int level)
    {
        switch(level)
        {
        case 1: return "I";
        case 2: return "II";
        case 3: return "III";
        case 4: return "IV";
        case 5: return "V";
        default: return "?";
        }
    }

    /** One offer as "2: 9 levels - Efficiency III, Unbreaking II". Returns the length. */
    int describeOffer(char *buffer, unsigned int size, int slot, const Enchanting::Offer &offer)
    {
        if(size == 0)
            return 0;

        int written = snprintf(buffer, size, "%d: %d levels", slot + 1, offer.level);
        for(int e = 0; e < offer.set.count && written > 0 && static_cast<unsigned int>(written) < size; ++e)
        {
            const Enchanting::Id id = Enchanting::unpackId(offer.set.entries[e]);
            const int level = Enchanting::unpackLevel(offer.set.entries[e]);
            written += snprintf(buffer + written, size - static_cast<unsigned int>(written),
                               e == 0 ? " - %s %s" : ", %s %s",
                               Enchanting::name(id), romanLevel(level));
        }
        return written < 0 ? 0 : written;
    }

    /** How many of a stack the inventory holds, across every slot. */
    unsigned int inventoryCount(BLOCK_WDATA stack)
    {
        unsigned int total = 0;
        for(int i = 0; i < Inventory::slot_count; ++i)
            if(current_inventory.slotBlock(i) == stack)
                total += current_inventory.slotCount(i);
        return total;
    }

    /** Takes a stack out of the inventory, from the slots that hold it. */
    void consumeInventoryItem(BLOCK_WDATA stack, int amount)
    {
        for(int i = 0; i < Inventory::slot_count && amount > 0; ++i)
        {
            if(current_inventory.slotBlock(i) != stack)
                continue;

            const unsigned int held = current_inventory.slotCount(i);
            const unsigned int taken = held < static_cast<unsigned int>(amount)
                ? held : static_cast<unsigned int>(amount);
            const unsigned int left = held - taken;
            current_inventory.setSlotWithDamage(i, left == 0 ? BLOCK_AIR : stack, left,
                                                current_inventory.slotDamage(i));
            amount -= static_cast<int>(taken);
        }
    }

    /**
     * /enchant -- the enchanting table's rules, until the block itself is in the
     * world.
     *
     * With no argument it lists the three offers for the held item, exactly as the
     * table's three buttons would; with 1, 2 or 3 it takes that offer, spending the
     * levels and the lapis lazuli vanilla charges. The bookshelves around the
     * player are counted the vanilla way, so a wall of them is what turns a level 3
     * offer into a level 30 one.
     */
    void commandEnchant(const Command::Parsed &parsed, char *reply, unsigned int size)
    {
        Enchanting::Offer offers[Enchanting::OfferCount];
        uint8_t item_id = 0xFF;
        heldItemOffers(offers, item_id);

        if(item_id == 0xFF)
        {
            setReply(reply, size, "Hold the item to enchant");
            return;
        }

        const int shelves = countBookshelves();

        if(parsed.arg_count == 0)
        {
            // All three offers in one reply, built into one buffer: the console
            // shows a line at a time, so the three buttons of the table become one
            // readable row here.
            char buffer[160];
            int written = 0;
            for(int i = 0; i < Enchanting::OfferCount && written >= 0 && static_cast<unsigned int>(written) < sizeof(buffer); ++i)
                written += describeOffer(buffer + written, sizeof(buffer) - static_cast<unsigned int>(written), i, offers[i]);
            setReply(reply, size, "%s (%d bookshelves)", buffer, shelves);
            return;
        }

        int slot = 0;
        if(!Command::parseInt(parsed.args[0], slot) || slot < 1 || slot > Enchanting::OfferCount)
        {
            setReply(reply, size, "Offer must be 1, 2 or 3");
            return;
        }
        --slot;

        const Enchanting::Offer &offer = offers[slot];
        if(!offer.available)
        {
            setReply(reply, size, "Offer %d has nothing for this item", slot + 1);
            return;
        }

        const int level = playerLevel();
        if(level < offer.level)
        {
            setReply(reply, size, "Needs %d levels (you have %d)", offer.level, level);
            return;
        }

        // Vanilla asks for lapis on top of the levels: one, two, three by offer.
        const int lapis_needed = Enchanting::lapisForSlot(slot);
        const BLOCK_WDATA lapis = getBLOCKWDATA(BLOCK_ITEM, static_cast<uint8_t>(ItemTexture::LAPIS_LAZULI));
        if(!world_task.isCreative()
           && inventoryCount(lapis) < static_cast<unsigned int>(lapis_needed))
        {
            setReply(reply, size, "Needs %d lapis lazuli", lapis_needed);
            return;
        }

        if(!spendLevels(offer.level))
        {
            setReply(reply, size, "Not enough levels");
            return;
        }

        if(!world_task.isCreative())
            consumeInventoryItem(lapis, lapis_needed);

        const Enchanting::Set before = current_inventory.currentSlotEnchant();
        Enchanting::Set after = before;
        Enchanting::applyOffer(offer, after);
        current_inventory.setCurrentSlotEnchant(after);

        if(after.count == before.count)
        {
            setReply(reply, size, "Nothing new to add");
            return;
        }

        char buffer[64];
        int written = 0;
        for(int e = 0; e < after.count && written >= 0 && static_cast<unsigned int>(written) < sizeof(buffer); ++e)
        {
            const Enchanting::Id id = Enchanting::unpackId(after.entries[e]);
            const int lvl = Enchanting::unpackLevel(after.entries[e]);
            written += snprintf(buffer + written, sizeof(buffer) - static_cast<unsigned int>(written),
                               e == 0 ? "%s %d" : ", %s %d", Enchanting::name(id), lvl);
        }

        setReply(reply, size, "Enchanted %s: %s", stackName(current_inventory.currentSlot()), buffer);
    }

    void commandTime(const Command::Parsed &parsed, char *reply, unsigned int size)
    {
        if(parsed.arg_count == 0)
        {
            char clock[8];
            WorldClock::formatClock(clock, sizeof(clock));
            setReply(reply, size, "%s, day %u", clock, WorldClock::dayCount());
            return;
        }

        bool adding = false;
        const char *value = parsed.args[0];

        // The sub-words are compared the same way an item name is, so "SET" works
        // as well as "set"; only the command name itself is case-folded by the
        // parser.
        if(parsed.arg_count >= 2
           && (Command::namesMatch(parsed.args[0], "set")
               || Command::namesMatch(parsed.args[0], "add")))
        {
            adding = Command::namesMatch(parsed.args[0], "add");
            value = parsed.args[1];
        }
        else if(Command::namesMatch(parsed.args[0], "add")
                || Command::namesMatch(parsed.args[0], "set"))
        {
            setReply(reply, size, "Usage: /time <set|add> <value>");
            return;
        }

        unsigned int ticks;
        if(!Command::parseTime(value, ticks))
        {
            setReply(reply, size, "Not a time: %s", value);
            return;
        }

        if(adding)
            WorldClock::advance(ticks);
        else
            WorldClock::setTime(ticks);

        // The clock is part of the save file, so a moved clock is a changed world.
        world.setDirty();

        char clock[8];
        WorldClock::formatClock(clock, sizeof(clock));
        setReply(reply, size, "Time %s to %s", adding ? "advanced" : "set", clock);
    }

    void commandWeather(const Command::Parsed &parsed, char *reply, unsigned int size)
    {
        if(parsed.arg_count == 0)
        {
            setReply(reply, size, "Weather: %s", Weather::stateName(world_task.weatherState()));
            return;
        }

        int state;
        if(!Command::parseWeather(parsed.args[0], state))
        {
            setReply(reply, size, "Not weather: %s", parsed.args[0]);
            return;
        }

        if(settings_task.getValue(SettingsTask::DAY_NIGHT) == 0
           || settings_task.getValue(SettingsTask::WEATHER) == 0)
        {
            setReply(reply, size, "Weather is off in Settings");
            return;
        }

        unsigned int seconds = DefaultWeatherSeconds;
        if(parsed.arg_count >= 2)
        {
            int given;
            if(!Command::parseInt(parsed.args[1], given) || given <= 0)
            {
                setReply(reply, size, "Not a duration: %s", parsed.args[1]);
                return;
            }
            seconds = static_cast<unsigned int>(given);
        }

        world_task.setWeatherOverride(state, seconds);
        setReply(reply, size, "%s for %u seconds", Command::weatherName(state), seconds);
    }

    void commandTeleport(const Command::Parsed &parsed, char *reply, unsigned int size)
    {
        if(parsed.arg_count < 3)
        {
            setReply(reply, size, "Usage: /teleport <x> <y> <z>");
            return;
        }

        // The player's block position is the origin for the "~" form, which is
        // why the coordinates are resolved against it rather than against zero.
        const int origin_x = (world_task.x / BLOCK_SIZE).floor();
        const int origin_y = (world_task.y / BLOCK_SIZE).floor();
        const int origin_z = (world_task.z / BLOCK_SIZE).floor();

        int bx, by, bz;
        if(!Command::parseCoordinate(parsed.args[0], origin_x, bx)
           || !Command::parseCoordinate(parsed.args[1], origin_y, by)
           || !Command::parseCoordinate(parsed.args[2], origin_z, bz))
        {
            setReply(reply, size, "Coordinates must be numbers or ~");
            return;
        }

        if(!inWorldY(by) || !inWorldY(by + 1))
        {
            setReply(reply, size, "Y must be 0..%d", max_world_y - 1);
            return;
        }

        // Refusing a destination inside rock is not how vanilla behaves, but on
        // the CX a player who lands in stone has no way to tell what happened:
        // the head is two blocks up and suffocation starts immediately.
        const BLOCK feet = getBLOCK(world.getBlock(bx, by, bz));
        const BLOCK head = getBLOCK(world.getBlock(bx, by + 1, bz));
        if(!(feet == BLOCK_AIR || feet == BLOCK_WATER || feet == BLOCK_WATER_FAST)
           || !(head == BLOCK_AIR || head == BLOCK_WATER || head == BLOCK_WATER_FAST))
        {
            setReply(reply, size, "That spot is inside a block");
            return;
        }

        world_task.teleportTo(bx, by, bz);

        setReply(reply, size, "Teleported to %d, %d, %d", bx, by, bz);
    }

    /**
     * Writes one block and keeps the containers consistent: a chest that is
     * replaced drops what it held, one that is written becomes an empty
     * container, and a furnace that is replaced forgets its cooking state. That
     * mirrors what the world's own placement and breaking code does, so a command
     * cannot leave a chest container attached to a block that is no longer there.
     */
    void writeBlock(int bx, int by, int bz, BLOCK_WDATA block)
    {
        const BLOCK before = getBLOCK(world.getBlock(bx, by, bz));
        const BLOCK after = getBLOCK(block);

        if(before == BLOCK_CHEST && after != BLOCK_CHEST)
            breakChestAt(bx, by, bz);
        if(before == BLOCK_FURNACE && after != BLOCK_FURNACE)
            inventory_task.removeFurnaceTile(bx, by, bz);

        world.changeBlock(bx, by, bz, block);

        if(after == BLOCK_CHEST)
            placeChestAt(bx, by, bz);
        if(after == BLOCK_FURNACE)
            inventory_task.ensureFurnaceTile(bx, by, bz);
    }

    void commandSetblock(const Command::Parsed &parsed, char *reply, unsigned int size)
    {
        if(parsed.arg_count < 4)
        {
            setReply(reply, size, "Usage: /setblock <x> <y> <z> <block> [data]");
            return;
        }

        int bx, by, bz;
        if(!Command::parseInt(parsed.args[0], bx)
           || !Command::parseInt(parsed.args[1], by)
           || !Command::parseInt(parsed.args[2], bz))
        {
            setReply(reply, size, "Coordinates must be numbers");
            return;
        }

        if(!inWorldY(by))
        {
            setReply(reply, size, "Y must be 0..%d", max_world_y);
            return;
        }

        BLOCK_WDATA stack;
        if(!resolveStack(parsed.args[3], true, stack))
        {
            setReply(reply, size, "Unknown block: %s", parsed.args[3]);
            return;
        }

        if(getBLOCK(stack) == BLOCK_ITEM)
        {
            setReply(reply, size, "That is an item, not a block");
            return;
        }

        BLOCK_WDATA block = stack;
        if(parsed.arg_count >= 5)
        {
            int data;
            if(!Command::parseInt(parsed.args[4], data) || data < 0 || data > 0x7F)
            {
                setReply(reply, size, "Data must be 0..127");
                return;
            }
            block = getBLOCKWDATA(getBLOCK(stack), static_cast<uint8_t>(data));
        }

        writeBlock(bx, by, bz, block);
        world.setDirty();

        if(isAirStack(block))
            setReply(reply, size, "Cleared %d, %d, %d", bx, by, bz);
        else
            setReply(reply, size, "Set %s at %d, %d, %d",
                     stackName(block), bx, by, bz);
    }

    void commandFill(const Command::Parsed &parsed, char *reply, unsigned int size)
    {
        if(parsed.arg_count < 7)
        {
            setReply(reply, size, "Usage: /fill <x1> <y1> <z1> <x2> <y2> <z2> <block>");
            return;
        }

        int x1, y1, z1, x2, y2, z2;
        if(!Command::parseInt(parsed.args[0], x1)
           || !Command::parseInt(parsed.args[1], y1)
           || !Command::parseInt(parsed.args[2], z1)
           || !Command::parseInt(parsed.args[3], x2)
           || !Command::parseInt(parsed.args[4], y2)
           || !Command::parseInt(parsed.args[5], z2))
        {
            setReply(reply, size, "Coordinates must be numbers");
            return;
        }

        long volume = 0;
        if(!Command::fillVolume(x1, y1, z1, x2, y2, z2, volume))
        {
            if(volume <= 0)
                setReply(reply, size, "Empty box");
            else
                setReply(reply, size, "Too big: %ld blocks (max %ld)", volume, Command::MaxFillBlocks);
            return;
        }

        BLOCK_WDATA stack;
        if(!resolveStack(parsed.args[6], true, stack))
        {
            setReply(reply, size, "Unknown block: %s", parsed.args[6]);
            return;
        }

        if(getBLOCK(stack) == BLOCK_ITEM)
        {
            setReply(reply, size, "That is an item, not a block");
            return;
        }

        // The box is clamped to the world rather than refused, so a fill that runs
        // past the top still builds the part that fits.
        if(y1 > y2) { const int t = y1; y1 = y2; y2 = t; }
        if(y1 < 0) y1 = 0;
        if(y2 > max_world_y) y2 = max_world_y;
        if(x1 > x2) { const int t = x1; x1 = x2; x2 = t; }
        if(z1 > z2) { const int t = z1; z1 = z2; z2 = t; }

        long placed = 0;
        for(int by = y1; by <= y2; ++by)
            for(int bx = x1; bx <= x2; ++bx)
                for(int bz = z1; bz <= z2; ++bz)
                {
                    writeBlock(bx, by, bz, stack);
                    ++placed;
                }

        world.setDirty();
        setReply(reply, size, "Filled %ld blocks", placed);
    }

    /** Mob kinds /summon understands. The ids are local to this file. */
    enum SummonKind
    {
        SummonChicken = 0,
        SummonCow,
        SummonPig,
        SummonSheep,
        SummonHorse,
        SummonWolf,
        SummonMooshroom,
        SummonDonkey,
        SummonCreeper,
    };

    struct MobName
    {
        const char *name;
        SummonKind kind;
    };

    const MobName mob_names[] = {
        {"chicken", SummonChicken},
        {"cow", SummonCow},
        {"pig", SummonPig},
        {"sheep", SummonSheep},
        {"horse", SummonHorse},
        {"wolf", SummonWolf},
        {"mooshroom", SummonMooshroom},
        {"donkey", SummonDonkey},
        {"creeper", SummonCreeper},
        // Villagers are bound to the village they belong to (their home, their
        // bed and their trade all hang off it), so they are deliberately not
        // summonable: one dropped in open country would have nowhere to be. The
        // villagers and the player are the only humanoids in the game -- there is
        // no Steve mob, and nothing here can make one.
    };

    void commandSummon(const Command::Parsed &parsed, char *reply, unsigned int size)
    {
        if(parsed.arg_count < 1)
        {
            setReply(reply, size, "Usage: /summon <mob> [x] [y] [z]");
            return;
        }

        const MobName *mob = nullptr;
        for(unsigned int i = 0; i < sizeof(mob_names) / sizeof(mob_names[0]); ++i)
        {
            if(Command::namesMatch(parsed.args[0], mob_names[i].name))
            {
                mob = &mob_names[i];
                break;
            }
        }

        if(mob == nullptr)
        {
            setReply(reply, size, "No such mob: %s", parsed.args[0]);
            return;
        }

        if(world.worldType() == World::WorldType::Graph)
        {
            setReply(reply, size, "Not in a graph view");
            return;
        }

        const int origin_x = (world_task.x / BLOCK_SIZE).floor();
        const int origin_y = (world_task.y / BLOCK_SIZE).floor();
        const int origin_z = (world_task.z / BLOCK_SIZE).floor();

        int bx = origin_x, by = origin_y, bz = origin_z;
        if(parsed.arg_count >= 4
           && (!Command::parseCoordinate(parsed.args[1], origin_x, bx)
               || !Command::parseCoordinate(parsed.args[2], origin_y, by)
               || !Command::parseCoordinate(parsed.args[3], origin_z, bz)))
        {
            setReply(reply, size, "Coordinates must be numbers or ~");
            return;
        }

        if(!inWorldY(by))
        {
            setReply(reply, size, "Y must be 0..%d", max_world_y);
            return;
        }

        // With no coordinates the mob appears two blocks in front of the player,
        // which is where a crosshair is pointing and therefore where it is
        // expected. The sine and cosine are the same pair getForward() uses.
        GLFix sx, sy, sz;
        if(parsed.arg_count >= 4)
        {
            sx = GLFix(bx * BLOCK_SIZE + BLOCK_SIZE / 2);
            sy = GLFix(by * BLOCK_SIZE);
            sz = GLFix(bz * BLOCK_SIZE + BLOCK_SIZE / 2);
        }
        else
        {
            sx = world_task.x + GLFix(fast_sin(world_task.yr)) * GLFix(2 * BLOCK_SIZE);
            sy = world_task.y;
            sz = world_task.z + GLFix(fast_cos(world_task.yr)) * GLFix(2 * BLOCK_SIZE);
        }

        switch(mob->kind)
        {
        case SummonCreeper:
            creeper_entities.push_back(CreeperEntity(sx, sy, sz));
            setReply(reply, size, "Summoned a creeper");
            return;
        default:
            break;
        }

        if(livestockCount() >= MaxSummonPopulation
           || livestock_entities.size() >= Livestock::maxEntities())
        {
            setReply(reply, size, "Too many animals already");
            return;
        }

        Livestock::Species species = Livestock::Species::Cow;
        switch(mob->kind)
        {
        case SummonChicken: species = Livestock::Species::Chicken; break;
        case SummonCow: species = Livestock::Species::Cow; break;
        case SummonPig: species = Livestock::Species::Pig; break;
        case SummonSheep: species = Livestock::Species::Sheep; break;
        case SummonHorse: species = Livestock::Species::Horse; break;
        case SummonWolf: species = Livestock::Species::Wolf; break;
        case SummonMooshroom: species = Livestock::Species::Mooshroom; break;
        case SummonDonkey: species = Livestock::Species::Donkey; break;
        default: break;
        }

        livestock_entities.push_back(LivestockEntity(species, sx, sy, sz));
        setReply(reply, size, "Summoned a %s", mob->name);
    }

    void commandSeed(char *reply, unsigned int size)
    {
        setReply(reply, size, "Seed %u (0x%X), day %u",
                 world.seedValue(), world.seedValue(), WorldClock::dayCount());
    }

    void commandGamemode(const Command::Parsed &parsed, char *reply, unsigned int size)
    {
        if(parsed.arg_count == 0)
        {
            setReply(reply, size, "Gamemode: %s", Command::gamemodeName(world_task.gamemode));
            return;
        }

        int mode;
        if(!Command::parseGamemode(parsed.args[0], mode))
        {
            setReply(reply, size, "Gamemode must be survival or creative");
            return;
        }

        world_task.setGamemode(mode);
        // The mode is part of the save file, so changing it changes the world.
        world.setDirty();
        setReply(reply, size, "Gamemode %s", Command::gamemodeName(mode));
    }

    /**
     * The player's score, which the death screen shows. Bare, it reports the
     * current value; with a number it sets one, which is the debug stand-in for
     * vanilla's `/scoreboard players set`.
     */
    void commandScore(const Command::Parsed &parsed, char *reply, unsigned int size)
    {
        if(parsed.arg_count == 0)
        {
            setReply(reply, size, "Score: %d", world_task.score());
            return;
        }

        int amount;
        if(!Command::parseInt(parsed.args[0], amount))
        {
            setReply(reply, size, "Score must be a number");
            return;
        }

        world_task.setScore(amount);
        setReply(reply, size, "Score: %d", world_task.score());
    }
}

void runCommand(const char *line, char *reply, unsigned int reply_size)
{
    if(reply == nullptr || reply_size == 0)
        return;

    reply[0] = '\0';

    Command::Parsed parsed;
    const Command::Result result = Command::parse(line, parsed);

    if(result == Command::Result::Empty)
        return;

    if(result == Command::Result::TooLong)
    {
        setReply(reply, reply_size, "Command too long");
        return;
    }

    switch(Command::commandIndex(parsed.name))
    {
    case 0: commandHelp(parsed, reply, reply_size); break;
    case 1: commandGive(parsed, reply, reply_size); break;
    case 2: commandTime(parsed, reply, reply_size); break;
    case 3: commandWeather(parsed, reply, reply_size); break;
    case 4: commandTeleport(parsed, reply, reply_size); break;
    case 5: commandSetblock(parsed, reply, reply_size); break;
    case 6: commandFill(parsed, reply, reply_size); break;
    case 7: commandSummon(parsed, reply, reply_size); break;
    case 8: commandSeed(reply, reply_size); break;
    case 9: commandGamemode(parsed, reply, reply_size); break;
    case 10: commandEnchant(parsed, reply, reply_size); break;
    case 11: commandScore(parsed, reply, reply_size); break;
    default:
        setReply(reply, reply_size, "Unknown command: %s (/help)", parsed.name);
        break;
    }
}
