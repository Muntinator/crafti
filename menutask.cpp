#include "menutask.h"

#include "audio_manager.h"
#include "audiotesttask.h"

#include "texturetools.h"
#include "worldtask.h"
#include "helptask.h"
#include "menuui.h"
#include "settingstask.h"
#include "starttask.h"
#include "font.h"

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

    // The world behind the menu, dimmed the way vanilla dims it. Only the world's
    // half of the screen is touched; the fade of a wake-up or a death is not this
    // screen's business.
    MenuUI::shadeRect(*screen, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 50);

    // Vanilla's pause menu is buttons and nothing else -- no heading, because the
    // world behind it is still on screen and says where the player is.
    const MenuUI::ButtonColumn buttons = MenuUI::pauseMenuLayout();

    for(int i = 0; i < MENU_ITEM_MAX; ++i)
    {
        const int y = buttons.buttonY(i);
        const bool focused = (i == menu_selected_item);

        MenuUI::drawButton(*screen, buttons.x, y, buttons.w, buttons.h, focused);
        MenuUI::drawButtonLabel(MenuUI::pauseLabels[i], *screen, buttons.x, y, buttons.w, buttons.h,
                                focused);
    }
}

void MenuTask::logic(GLFix /*dt*/)
{
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
        GameAudio::play(GameAudio::EventMenuSelect);
        switch(menu_selected_item)
        {
        case RESUME:
            world_task.makeCurrent();
            break;

        case SETTINGS:
            settings_task.makeCurrent();
            break;

        case HELP:
            help_task.makeCurrent();
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

        case QUIT_TO_TITLE:
            #ifndef _TINSPIRE
                save();
            #endif
            start_task.makeCurrent();
            break;

        default:
            world_task.makeCurrent();
            break;
        }

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_MENU) || keyPressed(KEY_NSPIRE_ESC))
    {
        world_task.makeCurrent();
        key_held_down = true;
    }
}
