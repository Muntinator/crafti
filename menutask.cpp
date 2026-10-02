#include "menutask.h"

#include "audio_manager.h"
#include "audiotesttask.h"

#include "texturetools.h"
#include "worldtask.h"
#include "blocklisttask.h"
#include "helptask.h"
#include "inventorytask.h"
#include "menuui.h"
#include "settingstask.h"
#include "starttask.h"
#include "font.h"

#ifndef _TINSPIRE
#include <SDL/SDL.h>
#endif

MenuTask menu_task;

MenuTask::MenuTask()
{
}

MenuTask::~MenuTask()
{
}

void MenuTask::makeCurrent()
{
    GameAudio::stopMusic();
    menu_selected_item = RESUME;

    if(!background_saved)
        saveBackground();

    Task::makeCurrent();
}

void MenuTask::render()
{
    drawBackground();

    // The world behind the menu, washed the way vanilla washes it: a dark
    // gradient, not a half shade. Only the world's half of the screen is touched;
    // the fade of a wake-up or a death is not this screen's business.
    MenuUI::drawPauseOverlay(*screen);

    const MenuUI::PauseLayout layout = MenuUI::pauseMenuLayout();

    // Vanilla heads the pause screen "Game Menu", centred at y 40, and then lays
    // its button grid out under it.
    MenuUI::drawHeading(MenuUI::pauseHeading, *screen, layout.heading_y);

    for(int i = 0; i < MENU_ITEM_MAX; ++i)
    {
        int x = 0, y = 0, w = 0, h = 0;
        layout.buttonRect(i, x, y, w, h);
        const bool focused = (i == menu_selected_item);

        MenuUI::drawButton(*screen, x, y, w, h, focused);
        MenuUI::drawButtonLabel(MenuUI::pauseLabels[i], *screen, x, y, w, h, focused);
    }
}

void MenuTask::activate()
{
    GameAudio::uiClick();

    switch(menu_selected_item)
    {
    case RESUME:
        world_task.makeCurrent();
        break;

    case HELP:
        // Opened as a child of this screen, so closing it returns here rather
        // than dropping the player straight into the game -- vanilla's own shape.
        help_task.openFrom(this);
        break;

    case BLOCK_LIST:
        block_list_task.openFrom(this);
        break;

    case SETTINGS:
        settings_task.openFrom(this);
        break;

    case SAVE_WORLD:
        if(save())
            world_task.setMessage("World saved.");
        else
            world_task.setMessage("Failed to save world.");
        world_task.makeCurrent();
        break;

    case AUDIO_TEST:
        audio_test_task.openFrom(this);
        break;

    case PLAYER_INVENTORY:
        inventory_task.openPlayerInventoryFrom(this);
        break;

    case QUIT_TO_TITLE:
        // The button says "Save and Quit to Title", so the world is written on
        // both machines before the title screen replaces it.
        save();
        start_task.makeCurrent();
        break;

    default:
        world_task.makeCurrent();
        break;
    }
}

void MenuTask::logic(GLFix /*dt*/)
{
#ifndef _TINSPIRE
    // A desktop has a mouse, so the pause menu gets vanilla's focus-by-hover: the
    // pointer picks the button, lighting it, and a click takes it. The calculator
    // keeps the keys.
    SDL_PumpEvents();
    int mouse_x = 0, mouse_y = 0;
    const Uint8 buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    const bool left_down = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;

    const MenuUI::PauseLayout layout = MenuUI::pauseMenuLayout();
    int hovered = -1;
    for(int i = 0; i < MENU_ITEM_MAX; ++i)
    {
        int x = 0, y = 0, w = 0, h = 0;
        layout.buttonRect(i, x, y, w, h);
        if(mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h)
            hovered = i;
    }

    if(hovered >= 0)
        menu_selected_item = hovered;

    if(left_down && !left_mouse_was_down)
    {
        left_mouse_was_down = true;
        if(hovered >= 0)
        {
            activate();
            return;
        }
    }
    if(!left_down)
        left_mouse_was_down = false;
#endif

    if(key_held_down)
        key_held_down = keyPressed(KEY_NSPIRE_ESC) || keyPressed(KEY_NSPIRE_MENU) || keyPressed(KEY_NSPIRE_UP) || keyPressed(KEY_NSPIRE_DOWN) || keyPressed(KEY_NSPIRE_2) || keyPressed(KEY_NSPIRE_8) || keyPressed(KEY_NSPIRE_5) || keyPressed(KEY_NSPIRE_CLICK) || keyPressed(KEY_NSPIRE_ENTER);
    else if(keyPressed(KEY_NSPIRE_8) || keyPressed(KEY_NSPIRE_UP))
    {
        --menu_selected_item;
        if(menu_selected_item < 0)
            menu_selected_item = MENU_ITEM_MAX - 1;

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_2) || keyPressed(KEY_NSPIRE_DOWN))
    {
        ++menu_selected_item;
        if(menu_selected_item == MENU_ITEM_MAX)
            menu_selected_item = 0;

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_5) || keyPressed(KEY_NSPIRE_CLICK))
    {
        activate();
        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_MENU) || keyPressed(KEY_NSPIRE_ESC))
    {
        world_task.makeCurrent();
        key_held_down = true;
    }
}
