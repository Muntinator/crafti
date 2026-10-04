#include "task.h"
#include <zlib.h>

#ifndef _TINSPIRE
#include <SDL/SDL.h>
#endif

#include "texturetools.h"
#include "blocklisttask.h"
#include "cheststore.h"
#include "grounddrops.h"
#include "worldtask.h"
#include "settingstask.h"
#include "worldclock.h"
#include "inventory.h"

//The values have to stay somewhere
Task *Task::current_task;
bool Task::key_held_down, Task::running, Task::background_saved, Task::has_touchpad, Task::keys_inverted;
TEXTURE *Task::screen, *Task::background;
const char *Task::savefile;

namespace Pointer
{
namespace
{
    int pointer_x = SCREEN_WIDTH / 2;
    int pointer_y = SCREEN_HEIGHT / 2;
    int last_x = pointer_x;
    int last_y = pointer_y;
    bool button_down = false;
    bool button_edge = false;

#ifdef _TINSPIRE
    /** The touchpad's last contact, so a drag is a delta and a lift is an edge. */
    bool had_contact = false;
    uint16_t last_touch_x = 0, last_touch_y = 0;
    /**
     * Touchpad units per screen pixel. The pad's own axes are much finer than
     * the 320x240 screen, so a 1:1 mapping would run the cursor off the edge in
     * a single swipe; this keeps one swipe across the pad worth about one screen
     * width, which is what the inventory screen's own divisor was tuned to.
     */
    constexpr int TouchUnitsPerPixel = 10;
#else
    bool seeded = false;
#endif

    void clampToScreen()
    {
        if(pointer_x < 0) pointer_x = 0;
        if(pointer_y < 0) pointer_y = 0;
        if(pointer_x > SCREEN_WIDTH - 1) pointer_x = SCREEN_WIDTH - 1;
        if(pointer_y > SCREEN_HEIGHT - 1) pointer_y = SCREEN_HEIGHT - 1;
    }
}

void poll()
{
#ifdef _TINSPIRE
    touchpad_report_t touchpad;
    touchpad_scan(&touchpad);

    // A finger down and dragging moves the cursor; the pad reports a delta, so
    // the previous position is what makes it absolute again.
    if(had_contact && touchpad.contact)
    {
        pointer_x += (static_cast<int>(touchpad.x) - static_cast<int>(last_touch_x)) / TouchUnitsPerPixel;
        // The pad's Y grows downwards like the screen's, so no inversion here.
        pointer_y += (static_cast<int>(touchpad.y) - static_cast<int>(last_touch_y)) / TouchUnitsPerPixel;
        clampToScreen();
    }

    // A press is only a click in the pad's own button areas, exactly as the
    // inventory screen reads it: the arrows step the cursor, the click selects.
    const bool pressed = touchpad.pressed;
    button_edge = pressed && !button_down;
    button_down = pressed;

    had_contact = touchpad.contact;
    last_touch_x = touchpad.x;
    last_touch_y = touchpad.y;

    last_x = pointer_x;
    last_y = pointer_y;
#else
    SDL_PumpEvents();
    int mx = 0, my = 0;
    const Uint8 buttons = SDL_GetMouseState(&mx, &my);

    // The window is SCREEN_WIDTH x SCREEN_HEIGHT device pixels -- 640x480 on the
    // desktop, 320x240 on the calculator -- and the layouts are written against
    // that, scaling themselves by the GUI scale. So the pointer is kept in the
    // same pixels the pointer really is in, with no rescaling here.
    pointer_x = mx;
    pointer_y = my;
    clampToScreen();

    const bool pressed = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    button_edge = pressed && !button_down;
    button_down = pressed;
    seeded = true;
#endif
}

int x() { return pointer_x; }
int y() { return pointer_y; }
bool down() { return button_down; }
bool clicked() { return button_edge; }

bool moved()
{
    const bool changed = pointer_x != last_x || pointer_y != last_y;
    last_x = pointer_x;
    last_y = pointer_y;
    return changed;
}

void seed()
{
    last_x = pointer_x;
    last_y = pointer_y;
#ifdef _TINSPIRE
    had_contact = false;
    button_down = false;
    button_edge = false;
    touchpad_report_t touchpad;
    touchpad_scan(&touchpad);
    last_touch_x = touchpad.x;
    last_touch_y = touchpad.y;
#else
    // The point the pointer already sits at is not a move, and the button that
    // opened this screen must not also press something on it.
    seeded = false;
    SDL_PumpEvents();
    int mx = 0, my = 0;
    const Uint8 buttons = SDL_GetMouseState(&mx, &my);
    pointer_x = mx;
    pointer_y = my;
    clampToScreen();
    button_down = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    button_edge = false;
    seeded = true;
#endif
}
}

void Task::makeCurrent()
{
    current_task = this;
}

bool Task::keyPressed(const t_key &key)
{
    #ifdef _TINSPIRE
        if(has_touchpad)
        {
            if(key.tpad_arrow != TPAD_ARROW_NONE)
                return touchpad_arrow_pressed(key.tpad_arrow);
            else
                return !(*reinterpret_cast<volatile uint16_t*>(0x900E0000 + key.tpad_row) & key.tpad_col) == keys_inverted;
        }
        else
            return (*reinterpret_cast<volatile uint16_t*>(0x900E0000 + key.row) & key.col) == 0;
    #else
        static bool quit_requested = false;

        SDL_PumpEvents();
        SDL_Event event;
        while(SDL_PollEvent(&event))
        {
            if(event.type == SDL_QUIT)
            {
                quit_requested = true;
                running = false;
            }
        }

        if(quit_requested)
            return key.row == KEY_NSPIRE_ESC.row && key.col == KEY_NSPIRE_ESC.col;

        const Uint8 *keys = SDL_GetKeyState(nullptr);
        const Uint8 mouse = SDL_GetMouseState(nullptr, nullptr);

        const auto is_down = [keys](SDLKey sdl_key) {
            return keys[sdl_key] != 0;
        };

        if(key.row == KEY_NSPIRE_ESC.row && key.col == KEY_NSPIRE_ESC.col)
            return is_down(SDLK_ESCAPE);

        if(key.row == KEY_NSPIRE_8.row && key.col == KEY_NSPIRE_8.col)
            return is_down(SDLK_w) || is_down(SDLK_UP);
        if(key.row == KEY_NSPIRE_2.row && key.col == KEY_NSPIRE_2.col)
            return is_down(SDLK_s) || is_down(SDLK_DOWN);
        if(key.row == KEY_NSPIRE_4.row && key.col == KEY_NSPIRE_4.col)
            return is_down(SDLK_a) || is_down(SDLK_LEFT);
        if(key.row == KEY_NSPIRE_6.row && key.col == KEY_NSPIRE_6.col)
            return is_down(SDLK_d) || is_down(SDLK_RIGHT);

        if(key.row == KEY_NSPIRE_5.row && key.col == KEY_NSPIRE_5.col)
            return is_down(SDLK_SPACE) || is_down(SDLK_RETURN);

        if(key.row == KEY_NSPIRE_7.row && key.col == KEY_NSPIRE_7.col)
            return (mouse & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0 || is_down(SDLK_e);
        if(key.row == KEY_NSPIRE_9.row && key.col == KEY_NSPIRE_9.col)
            return (mouse & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0 || is_down(SDLK_q);

        if(key.row == KEY_NSPIRE_1.row && key.col == KEY_NSPIRE_1.col)
            return is_down(SDLK_1) || is_down(SDLK_LEFTBRACKET);
        if(key.row == KEY_NSPIRE_3.row && key.col == KEY_NSPIRE_3.col)
            return is_down(SDLK_3) || is_down(SDLK_RIGHTBRACKET);

        if(key.row == KEY_NSPIRE_PERIOD.row && key.col == KEY_NSPIRE_PERIOD.col)
            return is_down(SDLK_r) || is_down(SDLK_PERIOD);
        if(key.row == KEY_NSPIRE_MENU.row && key.col == KEY_NSPIRE_MENU.col)
            return is_down(SDLK_TAB);

        if(key.row == KEY_NSPIRE_UP.row && key.col == KEY_NSPIRE_UP.col)
            return is_down(SDLK_UP);
        if(key.row == KEY_NSPIRE_DOWN.row && key.col == KEY_NSPIRE_DOWN.col)
            return is_down(SDLK_DOWN);
        if(key.row == KEY_NSPIRE_LEFT.row && key.col == KEY_NSPIRE_LEFT.col)
            return is_down(SDLK_LEFT);
        if(key.row == KEY_NSPIRE_RIGHT.row && key.col == KEY_NSPIRE_RIGHT.col)
            return is_down(SDLK_RIGHT);

        if(key.row == KEY_NSPIRE_PLUS.row && key.col == KEY_NSPIRE_PLUS.col)
            return is_down(SDLK_EQUALS) || is_down(SDLK_PLUS);
        if(key.row == KEY_NSPIRE_MINUS.row && key.col == KEY_NSPIRE_MINUS.col)
            return is_down(SDLK_MINUS);
        if(key.row == KEY_NSPIRE_CTRL.row && key.col == KEY_NSPIRE_CTRL.col)
            return is_down(SDLK_LCTRL) || is_down(SDLK_RCTRL) || is_down(SDLK_LMETA) || is_down(SDLK_RMETA);

        // Text entry keys: the calculator's own letters and digits are unused by
        // the game's controls, so the console can have them whole. The return and
        // delete keys are mapped as well, because typing a line on the desktop has
        // to end somewhere, and the calculator reaches them through the same
        // constants below.
        if(key.row == KEY_NSPIRE_RET.row && key.col == KEY_NSPIRE_RET.col)
            return is_down(SDLK_RETURN) || is_down(SDLK_KP_ENTER);
        if(key.row == KEY_NSPIRE_ENTER.row && key.col == KEY_NSPIRE_ENTER.col)
            return is_down(SDLK_RETURN) || is_down(SDLK_KP_ENTER);
        if(key.row == KEY_NSPIRE_DEL.row && key.col == KEY_NSPIRE_DEL.col)
            return is_down(SDLK_BACKSPACE);
        if(key.row == KEY_NSPIRE_DIVIDE.row && key.col == KEY_NSPIRE_DIVIDE.col)
            return is_down(SDLK_SLASH);

        return false;
    #endif
}

namespace
{
    /**
     * One text key: what it is on the calculator, what it types, and what it is on
     * a desktop keyboard.
     *
     * `sdl_key` is the keycode the desktop build asks SDL for. For printable ASCII
     * SDL 1.2 uses the character's own code, so only the few characters that need
     * a modifier to type differ -- and those are given 0 rather than being made to
     * share a key with the unshifted character that a command already uses.
     */
    struct TextKey
    {
        t_key key;
        char character;
        int sdl_key;
    };

    const TextKey text_keys[] = {
        // Digits first: a character that could be reached two ways has to resolve
        // to the one that was actually wanted, and the digits are the more common.
        {KEY_NSPIRE_1, '1', '1'}, {KEY_NSPIRE_2, '2', '2'}, {KEY_NSPIRE_3, '3', '3'},
        {KEY_NSPIRE_4, '4', '4'}, {KEY_NSPIRE_5, '5', '5'}, {KEY_NSPIRE_6, '6', '6'},
        {KEY_NSPIRE_7, '7', '7'}, {KEY_NSPIRE_8, '8', '8'}, {KEY_NSPIRE_9, '9', '9'},
        {KEY_NSPIRE_0, '0', '0'},

        {KEY_NSPIRE_A, 'a', 'a'}, {KEY_NSPIRE_B, 'b', 'b'}, {KEY_NSPIRE_C, 'c', 'c'},
        {KEY_NSPIRE_D, 'd', 'd'}, {KEY_NSPIRE_E, 'e', 'e'}, {KEY_NSPIRE_F, 'f', 'f'},
        {KEY_NSPIRE_G, 'g', 'g'}, {KEY_NSPIRE_H, 'h', 'h'}, {KEY_NSPIRE_I, 'i', 'i'},
        {KEY_NSPIRE_J, 'j', 'j'}, {KEY_NSPIRE_K, 'k', 'k'}, {KEY_NSPIRE_L, 'l', 'l'},
        {KEY_NSPIRE_M, 'm', 'm'}, {KEY_NSPIRE_N, 'n', 'n'}, {KEY_NSPIRE_O, 'o', 'o'},
        {KEY_NSPIRE_P, 'p', 'p'}, {KEY_NSPIRE_Q, 'q', 'q'}, {KEY_NSPIRE_R, 'r', 'r'},
        {KEY_NSPIRE_S, 's', 's'}, {KEY_NSPIRE_T, 't', 't'}, {KEY_NSPIRE_U, 'u', 'u'},
        {KEY_NSPIRE_V, 'v', 'v'}, {KEY_NSPIRE_W, 'w', 'w'}, {KEY_NSPIRE_X, 'x', 'x'},
        {KEY_NSPIRE_Y, 'y', 'y'}, {KEY_NSPIRE_Z, 'z', 'z'},

        {KEY_NSPIRE_SPACE, ' ', ' '},

        // The punctuation a command line needs. The calculator has a key for each
        // of these, which is what makes a line typeable without a shift state.
        {KEY_NSPIRE_PERIOD, '.', '.'},
        {KEY_NSPIRE_COMMA, ',', ','},
        {KEY_NSPIRE_MINUS, '-', '-'},
        {KEY_NSPIRE_PLUS, '+', '+'},
        {KEY_NSPIRE_DIVIDE, '/', '/'},
        {KEY_NSPIRE_MULTIPLY, '*', '*'},
        {KEY_NSPIRE_COLON, ':', ';'}, // SDL asks for the physical ';' key
        {KEY_NSPIRE_NEGATIVE, '~', '`'},
        {KEY_NSPIRE_EQU, '=', '='},
        {KEY_NSPIRE_CAT, '^', 0},
        {KEY_NSPIRE_APOSTROPHE, '\'', '\''},
        {KEY_NSPIRE_LP, '(', 0},
        {KEY_NSPIRE_RP, ')', 0},
        // Keys whose touchpad mapping is a dummy register (quote, less-than,
        // greater-than, question and exclamation) are deliberately left out: the
        // touchpad scan would read a register that has no bit for them, and a
        // shifted character is worth less than a key that cannot misreport itself.
        // None of the commands needs them -- a name with a space in it is typed
        // without the space, which the name compare folds away.
    };

    constexpr unsigned int text_key_count = sizeof(text_keys) / sizeof(text_keys[0]);

    /** Which text keys were down on the previous call, for edge detection. */
    bool text_key_was_down[text_key_count] = {};

    bool textKeyIsDown(unsigned int index)
    {
    #ifdef _TINSPIRE
        return Task::keyPressed(text_keys[index].key);
    #else
        if(text_keys[index].sdl_key == 0)
            return false;
        const Uint8 *keys = SDL_GetKeyState(nullptr);
        return keys[text_keys[index].sdl_key] != 0;
    #endif
    }
}

char Task::textKeyPressed()
{
#ifndef _TINSPIRE
    // Once for the whole scan: textKeyIsDown() reads SDL's key array directly, so
    // the events only need to be pumped here rather than per key.
    SDL_PumpEvents();
#endif

    char typed = 0;

    for(unsigned int i = 0; i < text_key_count; ++i)
    {
        const bool down = textKeyIsDown(i);

        // A key that is newly down types; a key that is being held does not, which
        // is the difference between typing "stone" and "stooooone".
        if(down && !text_key_was_down[i] && typed == 0)
            typed = text_keys[i].character;

        text_key_was_down[i] = down;
    }

    return typed;
}

void Task::resetTextKeys()
{
    for(unsigned int i = 0; i < text_key_count; ++i)
        text_key_was_down[i] = textKeyIsDown(i);
}

void Task::initializeGlobals(const char *savefile)
{
    running = true;

    screen = newTexture(SCREEN_WIDTH, SCREEN_HEIGHT);
    nglSetBuffer(screen->bitmap);

    has_touchpad = is_touchpad;
    keys_inverted = is_classic;

    background = newTexture(SCREEN_WIDTH, SCREEN_HEIGHT);
    background_saved = false;

    Task::savefile = savefile;
}

void Task::deinitializeGlobals()
{
    if(screen)
    {
        deleteTexture(screen);
        screen = nullptr;
    }
    if(background)
    {
        deleteTexture(background);
        background = nullptr;
    }
}

void Task::saveBackground()
{
    copyTexture(*screen, *background);

    background_saved = true;
}

void Task::drawBackground()
{
    copyTexture(*background, *screen);
}

/* Version 2: First in git
 * Version 3 (31d5ee3a): Blocks are stored as 16bit BLOCK_WDATA
 * Version 4 (1ebc685a): Add inventory
 * Version 5 (710e7269): Add settings
 * Version 6 (d52f3992): BLOCK_SIZE changed from 120 to 128,
 *                       gzip compression introduced shortly afterwards
 * Version 7: Inventory stacks now store per-slot item counts
 * Version 8: Inventory expanded to 36 slots (9 hotbar + 27 storage)
 * Version 9: Day/night clock (time of day and day counter) is saved
 * Version 10: Per-slot item wear, worn armour, and chest contents
 * Version 11: The play mode chosen with /gamemode (survival or creative)
 * Version 12: The bed that was slept in, i.e. the respawn point
 * Version 13: Enchantments on the inventory slots and on the worn armour
 * Version 14: The offhand stack
 *
 * The order of the fields matters: load() reads them in exactly the order save()
 * writes them, and anything added to the world task's own section has to sit
 * before world.saveToFile(), whose chunk list ends the file.
 *
 * The version-13 enchantments used to be read after the chests, which is not
 * where save() writes them (it writes them directly after the armour, before the
 * chests). load() now reads them there too, so the file it accepts is the one
 * save() produces -- for a version-13 file as much as for a version-14 one.
 */
static constexpr int savefile_version = 14;

#define LOAD_FROM_FILE(var) if(gzfread(&var, sizeof(var), 1, file) != 1) { gzclose(file); return false; }
#define SAVE_TO_FILE(var) if(gzfwrite(&var, sizeof(var), 1, file) != 1) { gzclose(file); return false; }

bool Task::load()
{
    // Versions before 6 (and 6 for a short time) were uncompressed,
    // but gzopen detects and handles uncompressed files transparently.
    // Previous versions read the gzip magic as savefile version and bail out.
    gzFile file = gzopen(savefile, "rb");
    if(!file)
        return false;

    int version;
    LOAD_FROM_FILE(version);

    static_assert(savefile_version == 14, "Adjust loading code for backward compatibility");

    if(version < 4 || version > savefile_version)
    {
        printf("Save file version %d not supported!\n", version);
        gzclose(file);
        return false;
    }

    if(!settings_task.loadFromFile(file, version))
    {
        gzclose(file);
        return false;
    }

    if(version >= 8)
    {
        LOAD_FROM_FILE(current_inventory.entries)
        LOAD_FROM_FILE(current_inventory.counts)
    }
    else
    {
        BLOCK_WDATA old_entries[5] = {};
        unsigned int old_counts[5] = {};

        if(gzfread(old_entries, sizeof(old_entries), 1, file) != 1)
        {
            gzclose(file);
            return false;
        }

        if(version >= 7)
        {
            if(gzfread(old_counts, sizeof(old_counts), 1, file) != 1)
            {
                gzclose(file);
                return false;
            }
        }

        for(int i = 0; i < Inventory::slot_count; ++i)
            current_inventory.setSlot(i, BLOCK_AIR, 0);

        for(int i = 0; i < 5; ++i)
        {
            const unsigned int count = (version >= 7) ? old_counts[i] : 1;
            current_inventory.setSlot(i, old_entries[i], count);
        }
    }

    current_inventory.importLegacyCounts();

    LOAD_FROM_FILE(world_task.xr)
    LOAD_FROM_FILE(world_task.yr)
    LOAD_FROM_FILE(world_task.x)
    LOAD_FROM_FILE(world_task.y)
    LOAD_FROM_FILE(world_task.z)
    // Previous versions used BLOCK_SIZE 120
    if(version < 6)
    {
        world_task.x = world_task.x * BLOCK_SIZE / 120;
        world_task.y = world_task.y * BLOCK_SIZE / 120;
        world_task.z = world_task.z * BLOCK_SIZE / 120;
    }

    LOAD_FROM_FILE(current_inventory.current_slot)

    LOAD_FROM_FILE(block_list_task.current_selection)

    // Version 9: a world remembers what time of day it was left at. Loading an
    // older file leaves the clock where the previous world left it, which is
    // harmless because the clock is not part of world generation.
    if(version >= 9)
    {
        unsigned int clock_time, clock_days;
        LOAD_FROM_FILE(clock_time)
        LOAD_FROM_FILE(clock_days)
        WorldClock::restore(clock_time, clock_days);
    }

    // Version 10: every stack carries its wear, and the player can be wearing
    // armour. Read here, after the clock, because that is where save() writes
    // them. An older file leaves both at zero, which is exactly how those items
    // behaved then (no tool could break).
    if(version >= 10)
    {
        LOAD_FROM_FILE(current_inventory.damage)
        LOAD_FROM_FILE(current_inventory.armor)
        LOAD_FROM_FILE(current_inventory.armor_counts)
        LOAD_FROM_FILE(current_inventory.armor_damage)
    }
    else
    {
        for(int i = 0; i < Inventory::slot_count; ++i)
            current_inventory.damage[i] = 0;
        current_inventory.clearArmor();
    }

    // Version 14: the offhand stack, with the wear of whatever is in it. An older
    // file leaves it empty, which is what those worlds had.
    if(version >= 14)
    {
        LOAD_FROM_FILE(current_inventory.offhand)
        LOAD_FROM_FILE(current_inventory.offhand_count)
        LOAD_FROM_FILE(current_inventory.offhand_damage)
    }
    else
    {
        current_inventory.offhand = BLOCK_AIR;
        current_inventory.offhand_count = 0;
        current_inventory.offhand_damage = 0;
    }

    // Version 13: the enchantments on the carried items and on the worn armour.
    // They are written as the packed entry bytes, because an enchantment is one
    // byte (five bits of id, three of level) and the whole set travels with the
    // stack. An older save leaves every item unenchanted, which is what it was.
    // Read here, directly after the armour, because that is where save() writes
    // them: before the chests, not after them.
    if(version >= 13)
    {
        for(int i = 0; i < Inventory::slot_count; ++i)
            LOAD_FROM_FILE(current_inventory.enchant[i].entries)
        for(int i = 0; i < Inventory::slot_count; ++i)
        {
            unsigned char count;
            LOAD_FROM_FILE(count)
            current_inventory.enchant[i].count = count > Enchanting::MaxPerItem
                ? Enchanting::MaxPerItem : count;
        }
        for(int i = 0; i < Inventory::armor_slot_count; ++i)
            LOAD_FROM_FILE(current_inventory.armor_enchant[i].entries)
        for(int i = 0; i < Inventory::armor_slot_count; ++i)
        {
            unsigned char count;
            LOAD_FROM_FILE(count)
            current_inventory.armor_enchant[i].count = count > Enchanting::MaxPerItem
                ? Enchanting::MaxPerItem : count;
        }
    }

    // Version 10: the chests and what is inside them. Like the clock this is
    // written before the world, because the world's own section is a chunk list
    // terminated by the end of the file.
    ChestStore::clear();
    if(version >= 10)
    {
        unsigned int chest_count;
        LOAD_FROM_FILE(chest_count)

        for(unsigned int i = 0; i < chest_count; ++i)
        {
            ChestStore::Chest chest;
            LOAD_FROM_FILE(chest)
            ChestStore::insertRecord(chest);
        }
    }

    // Version 11: the play mode. An older file was written before creative mode
    // existed, so it is survival, which is what those worlds were. The mode is set
    // directly rather than through setGamemode(), whose message belongs to a
    // command being typed and not to a world being opened.
    if(version >= 11)
    {
        int gamemode;
        LOAD_FROM_FILE(gamemode)
        world_task.gamemode = gamemode == 1 ? 1 : 0;
    }
    else
        world_task.gamemode = 0;

    // Version 12: the bed the player last slept in, which is where they respawn
    // after dying. Always a real position, with `valid` saying whether there is
    // one at all, so that a save from an older version (or a world whose bed was
    // broken) reads as "no bed" rather than as the origin. Four ints rather than
    // the struct: a bool in there would leave padding bytes in the file.
    if(version >= 12)
    {
        int bed_valid, bed_x, bed_y, bed_z;
        LOAD_FROM_FILE(bed_valid)
        LOAD_FROM_FILE(bed_x)
        LOAD_FROM_FILE(bed_y)
        LOAD_FROM_FILE(bed_z)
        world_task.restoreBedSpawn(bed_valid != 0, bed_x, bed_y, bed_z);
    }
    else
        world_task.restoreBedSpawn(false, 0, 0, 0);

    // Dropped items are not saved: a stack on the ground is a short-lived thing
    // (five minutes), and a world that is left mid-pickup should not come back
    // littered with the contents of its chests.
    clearGroundDrops();

    const bool ret = world.loadFromFile(file);

    gzclose(file);

    world.setPosition(world_task.x, world_task.y, world_task.z);

    return ret;
}

bool Task::save()
{
    gzFile file = gzopen(savefile, "wb");
    if(!file)
        return false;

    SAVE_TO_FILE(savefile_version)
    if(!settings_task.saveToFile(file))
    {
        gzclose(file);
        return false;
    }
    SAVE_TO_FILE(current_inventory.entries)
    SAVE_TO_FILE(current_inventory.counts)
    SAVE_TO_FILE(world_task.xr)
    SAVE_TO_FILE(world_task.yr)
    SAVE_TO_FILE(world_task.x)
    SAVE_TO_FILE(world_task.y)
    SAVE_TO_FILE(world_task.z)
    SAVE_TO_FILE(current_inventory.current_slot)
    SAVE_TO_FILE(block_list_task.current_selection)

    // Written before the world data on purpose: the world's own section is a
    // chunk list terminated by end of file, so nothing may follow it.
    const unsigned int clock_time = WorldClock::time();
    SAVE_TO_FILE(clock_time)
    const unsigned int clock_days = WorldClock::dayCount();
    SAVE_TO_FILE(clock_days)

    SAVE_TO_FILE(current_inventory.damage)
    SAVE_TO_FILE(current_inventory.armor)
    SAVE_TO_FILE(current_inventory.armor_counts)
    SAVE_TO_FILE(current_inventory.armor_damage)

    // The offhand stack, read back directly after the armour.
    SAVE_TO_FILE(current_inventory.offhand)
    SAVE_TO_FILE(current_inventory.offhand_count)
    SAVE_TO_FILE(current_inventory.offhand_damage)

    // Enchantments: the packed entries and then the counts, so the file never
    // contains padding and an unenchanted inventory costs one zero per slot.
    for(int i = 0; i < Inventory::slot_count; ++i)
        SAVE_TO_FILE(current_inventory.enchant[i].entries)
    for(int i = 0; i < Inventory::slot_count; ++i)
    {
        const unsigned char count = current_inventory.enchant[i].count;
        SAVE_TO_FILE(count)
    }
    for(int i = 0; i < Inventory::armor_slot_count; ++i)
        SAVE_TO_FILE(current_inventory.armor_enchant[i].entries)
    for(int i = 0; i < Inventory::armor_slot_count; ++i)
    {
        const unsigned char count = current_inventory.armor_enchant[i].count;
        SAVE_TO_FILE(count)
    }

    // Chests, again before the world section, which ends the file.
    const unsigned int chest_count = static_cast<unsigned int>(ChestStore::all().size());
    SAVE_TO_FILE(chest_count)
    for(const ChestStore::Chest &chest : ChestStore::all())
        SAVE_TO_FILE(chest)

    // The play mode. The weather override is deliberately left out: it is a
    // look-at-the-rain switch, and a reloaded world should have the weather its
    // seed and clock imply.
    SAVE_TO_FILE(world_task.gamemode)

    // The respawn bed, before the world section, which ends the file.
    const int bed_valid = world_task.bedSpawn().valid ? 1 : 0;
    SAVE_TO_FILE(bed_valid)
    const int bed_x = world_task.bedSpawn().x;
    SAVE_TO_FILE(bed_x)
    const int bed_y = world_task.bedSpawn().y;
    SAVE_TO_FILE(bed_y)
    const int bed_z = world_task.bedSpawn().z;
    SAVE_TO_FILE(bed_z)

    const bool ret = world.saveToFile(file);

    gzclose(file);

    return ret;
}
