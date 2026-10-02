#ifndef STARTTASK_H
#define STARTTASK_H

#include "task.h"

class StartTask : public Task
{
public:
    /**
     * The title screen's buttons, in the order they are drawn. There is no sound
     * test here: it belongs with the options, which is where vanilla puts it, and
     * the pause menu already has one.
     */
    enum STARTITEM {
        CONTINUE = 0,
        NEW_FLAT,
        NEW_TERRAIN,
        NEW_GRAPH,
        EXIT,
        START_ITEM_MAX
    };

    /** True when the button does nothing: there is no saved world to continue. */
    bool itemEnabled(int item) const { return item != CONTINUE || has_saved_world; }

    StartTask();
    virtual ~StartTask();

    virtual void makeCurrent() override;
    virtual void render() override;
    virtual void logic(GLFix dt) override;

    void setHasSavedWorld(bool exists) { has_saved_world = exists; }

private:
    /** Runs the focused button: the click cue, then whatever it opens. */
    void activate();

    int selected_item = NEW_TERRAIN;
    bool has_saved_world = false;
};

extern StartTask start_task;

#endif // STARTTASK_H
