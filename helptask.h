#ifndef HELPTASK_H
#define HELPTASK_H

#include "task.h"

#include "gl.h"

class HelpTask : public Task
{
public:
    HelpTask();
    virtual ~HelpTask();

    virtual void makeCurrent() override;

    /**
     * Opens the screen from `from`, so closing it hands control back there. The
     * pause menu opens it as a child screen, the way vanilla opens Advancements
     * from the pause menu rather than from the world: closing it comes back to the
     * pause menu, not straight into the game.
     */
    void openFrom(Task *from);

    virtual void render() override;
    virtual void logic(GLFix dt) override;

private:
    /** Esc hands control back to the screen that opened this one, or the world. */
    void close();
    Task *return_task = nullptr;
};

extern HelpTask help_task;

#endif // HELPTASK_H
