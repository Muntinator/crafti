#include "helptask.h"

#include "font.h"
#include "menuui.h"
#include "worldtask.h"

#ifndef _TINSPIRE
#include <SDL/SDL.h>
#endif

HelpTask help_task;

HelpTask::HelpTask()
{
}

HelpTask::~HelpTask()
{
}

void HelpTask::makeCurrent()
{
    if(!background_saved)
        saveBackground();

#ifndef _TINSPIRE
    // Baseline the button too: the click that opened this screen must not also
    // close it on the first frame it is up.
    SDL_PumpEvents();
    int mouse_x = 0, mouse_y = 0;
    const Uint8 buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    left_mouse_was_down = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
#endif
    Task::makeCurrent();
}

void HelpTask::openFrom(Task *from)
{
    return_task = from;
    makeCurrent();
}

void HelpTask::close()
{
    Task *back = return_task;
    return_task = nullptr;

    if(back != nullptr)
        back->makeCurrent();
    else
        world_task.makeCurrent();
}

void HelpTask::render()
{
    // The same dirt every vanilla menu sits on, with a vanilla heading, rather
    // than the hand-drawn panel this screen used to be.
    MenuUI::drawMenuBackground(*screen);

    const int heading = MenuUI::headingY();
    MenuUI::drawHeading("Help", *screen, heading);

    const int x = 10 * MenuUI::uiScale();
    const int y = heading + static_cast<int>(fontHeight()) + 8 * MenuUI::uiScale();

    drawString("8-4-6-2: Walk around\t5: Jump\n"
               "7: Put block down   \t9: Destroy block\n"
               "1-3: Change inventory slot\n"
               "ESC: Save & Exit\n"
               ".: Open list of blocks\n"
               "    5: Change block in inventory\n"
               "    . or ESC: Close list of blocks\n"
               "Menu: Open menu\n"
               "    2-8: Move cursor\t5: Select\n"
               "Ctrl+.: Take screenshot\n"
               "\n"
               "Programmed by Fabian Vogt\n"
               "Textures from Minecraft 1.17.1 (Mojang)", MenuUI::Text, *screen, x, y);
}

void HelpTask::logic(GLFix /*dt*/)
{
#ifndef _TINSPIRE
    // The screen has no widgets, so a click anywhere dismisses it, standing in
    // for vanilla's Back button. The calculator keeps Esc/Enter.
    SDL_PumpEvents();
    int mouse_x = 0, mouse_y = 0;
    const Uint8 buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    const bool left_down = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;

    (void)mouse_x;
    (void)mouse_y;

    if(left_down && !left_mouse_was_down)
    {
        left_mouse_was_down = true;
        close();
        return;
    }
    if(!left_down)
        left_mouse_was_down = false;
#endif

    // Esc leaves the screen; Enter does too, so the calculator's Enter key acts
    // like vanilla's Back button rather than doing nothing.
    if(key_held_down)
        key_held_down = keyPressed(KEY_NSPIRE_ESC) || keyPressed(KEY_NSPIRE_ENTER);
    else if(keyPressed(KEY_NSPIRE_ESC) || keyPressed(KEY_NSPIRE_ENTER))
    {
        close();

        key_held_down = true;
    }
}
