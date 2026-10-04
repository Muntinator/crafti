#include "deathtask.h"

#include "controls.h"

#include <stdio.h>

#include "texturetools.h"
#include "font.h"
#include "menuui.h"

#include "worldtask.h"
#include "starttask.h"

DeathTask death_task;

DeathTask::DeathTask()
{
}

DeathTask::~DeathTask()
{
}

void DeathTask::makeCurrent()
{
    death_selected_item = RESPAWN;

    if(!background_saved)
        saveBackground();

    Task::makeCurrent();
}

void DeathTask::render()
{
    drawBackground();

    // Vanilla washes the world in red when the player dies; the gradient is the
    // same dark-to-bright red its DeathScreen fills, baked in because nGL cannot
    // blend.
    MenuUI::drawDeathOverlay(*screen);

    const MenuUI::DeathLayout layout = MenuUI::deathLayout();

    // "You Died!" at twice the GUI scale, which is the size vanilla draws it.
    MenuUI::drawHeadingScaled(MenuUI::deathHeading, *screen, layout.title_y, layout.title_scale);

    // Where vanilla prints the cause of death. A bed that was slept in is easy to
    // forget about, so this line says which spawn the player is going back to
    // instead -- the same slot, carrying the one fact the screen needs to carry.
    drawStringCenter(world_task.bedSpawn().valid ? "You will respawn at your bed" : "You will respawn at the world spawn",
                     MenuUI::Text, *screen, SCREEN_WIDTH / 2, layout.message_y);

    // Vanilla's score line under the cause of death. It shows the player's score
    // (vanilla's `Player.getScore()`), not their experience: a world with no
    // scoreboard -- which is every world here -- starts at zero, exactly as
    // vanilla's does.
    char score[32];
    snprintf(score, sizeof(score), "Score: %d", world_task.score());
    drawStringCenter(score, MenuUI::Text, *screen, SCREEN_WIDTH / 2, layout.score_y);

    const char *items[DEATH_ITEM_MAX] = { MenuUI::deathRespawnLabel, MenuUI::deathTitleLabel };
    const MenuUI::ButtonColumn &buttons = layout.buttons;

    for(int i = 0; i < DEATH_ITEM_MAX; ++i)
    {
        const int y = buttons.buttonY(i);
        const bool focused = (i == death_selected_item);

        MenuUI::drawButton(*screen, buttons.x, y, buttons.w, buttons.h, focused);
        MenuUI::drawButtonLabel(items[i], *screen, buttons.x, y, buttons.w, buttons.h, focused);
    }
}

void DeathTask::logic(GLFix /*dt*/)
{
    if(key_held_down)
        key_held_down = Controls::cursorUp() || Controls::cursorDown() || Controls::activate() || Controls::menu();
    else if(Controls::cursorUp())
    {
        --death_selected_item;
        if(death_selected_item < 0)
            death_selected_item = DEATH_ITEM_MAX - 1;
        key_held_down = true;
    }
    else if(Controls::cursorDown())
    {
        ++death_selected_item;
        if(death_selected_item == DEATH_ITEM_MAX)
            death_selected_item = 0;
        key_held_down = true;
    }
    else if(Controls::activate())
    {
        switch(death_selected_item)
        {
        case RESPAWN:
            world_task.respawnPlayer();
            world_task.makeCurrent();
            break;

        case QUIT_TO_TITLE:
            start_task.makeCurrent();
            break;

        default:
            world_task.respawnPlayer();
            world_task.makeCurrent();
            break;
        }

        key_held_down = true;
    }
    else if(Controls::menu())
    {
        // Shift acts like "Respawn" to mirror the feel of the in-game menu.
        world_task.respawnPlayer();
        world_task.makeCurrent();
        key_held_down = true;
    }
}
