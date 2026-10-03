#ifndef STARTTASK_H
#define STARTTASK_H

#include "task.h"

class StartTask : public Task
{
public:
    /**
     * The title screen's buttons, in vanilla's own order: the three full-width
     * text buttons, the two half-width text buttons, then vanilla's two icon
     * buttons. "Singleplayer" opens the world-select screen rather than starting
     * a world outright, because this engine's world kinds live there.
     */
    enum STARTITEM {
        SINGLEPLAYER = 0,
        MULTIPLAYER,
        REALMS,
        OPTIONS,
        QUIT,
        LANGUAGE,
        ACCESSIBILITY,
        START_ITEM_MAX
    };

    /**
     * True when the button can be chosen. Multiplayer, Minecraft Realms and the
     * language and accessibility screens do not exist on a calculator, so those
     * buttons sit in vanilla's greyed state -- present, in their vanilla places,
     * but not choosable.
     */
    bool itemEnabled(int item) const
    {
        return item == SINGLEPLAYER || item == OPTIONS || item == QUIT;
    }

    StartTask();
    virtual ~StartTask();

    virtual void makeCurrent() override;
    virtual void render() override;
    virtual void logic(GLFix dt) override;

    void setHasSavedWorld(bool exists) { has_saved_world = exists; }
    /** True when there is a world to keep playing. */
    bool savedWorldExists() const { return has_saved_world; }

private:
    /** Runs the focused button: the click cue, then whatever it opens. */
    void activate();

    /**
     * The button the keyboard is on, or -1. Vanilla's TitleScreen opens with
     * nothing focused -- no button wears the highlight until the pointer or the
     * keyboard takes one -- so the screen starts at -1 as well.
     */
    int selected_item = -1;
    bool has_saved_world = false;
};

extern StartTask start_task;

#endif // STARTTASK_H
