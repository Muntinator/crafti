#ifndef MENUTASK_H
#define MENUTASK_H

#include "gl.h"

#include "task.h"

class MenuTask : public Task
{
public:
    /**
     * The entries fill vanilla 1.17.1's pause grid, in its own order: a full-width
     * button, three rows of two, then a full-width button. This game's screens
     * stand in where vanilla's do not exist -- `HELP` takes "Advancements" (the
     * in-game guide), `BLOCK_LIST` takes "Statistics" (the catalogue of blocks and
     * items), and Save World / Sound Test / Player Inventory are this game's own,
     * sitting where vanilla keeps its Send Feedback, Report Bugs and Share to LAN.
     */
    enum MENUITEM {
        RESUME = 0,
        HELP,
        BLOCK_LIST,
        SAVE_WORLD,
        AUDIO_TEST,
        SETTINGS,
        PLAYER_INVENTORY,
        QUIT_TO_TITLE,
        MENU_ITEM_MAX
    };

    MenuTask();
    virtual ~MenuTask();

    virtual void makeCurrent() override;

    virtual void render() override;
    virtual void logic(GLFix dt) override;

private:
    /** Runs the focused entry: the click cue, then whatever it opens. */
    void activate();

    int menu_selected_item = 0;
#ifndef _TINSPIRE
#endif
};

extern MenuTask menu_task;

#endif // MENUTASK_H
