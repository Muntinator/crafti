#ifndef COMMANDTASK_H
#define COMMANDTASK_H

#include "task.h"

/**
 * The in-game command console: the way a debug command is typed on the
 * calculator.
 *
 * The world task opens it with '/' (the divide key), which is the character a
 * command is written after, and closes it again with ESC. While it is open it is
 * the current task, so the world stops being simulated and nothing the player was
 * doing is lost; the frame the world had drawn is kept behind the panel.
 *
 * It is a shell around worldcommands.cpp. Everything the console does with a line
 * is: hand it to runCommand(), show the reply, and remember it for the up-arrow.
 * The parsing, the names and the effects all live on that side, so a command run
 * from here and a command run from anywhere else mean the same thing.
 *
 * Text is typed with the calculator's own letter and digit keys (Task::
 * textKeyPressed()), not with a virtual keyboard: the CX has a real keyboard, and
 * an on-screen one would cover the whole screen to spell "give".
 */
class CommandTask : public Task
{
public:
    /** Opens the console over the world, keeping the frame behind it. */
    void open();

    virtual void makeCurrent() override;
    virtual void render() override;
    virtual void logic(GLFix dt) override;

    /**
     * Longest line the console accepts. 60 characters is about 300 pixels in this
     * font, which is what fits beside the prompt on a 320-wide screen; a longer
     * line is refused rather than silently truncated.
     */
    static constexpr unsigned int max_line = 60;
    /** Commands remembered for the up-arrow. */
    static constexpr unsigned int history_size = 8;

private:
    /** Filled rectangle straight into the framebuffer; the panel has no texture. */
    static void fillPanel(int x, int y, int width, int height, unsigned short color);
    /** Width of a string in pixels, using the same metrics drawString() uses. */
    static unsigned int textWidth(const char *str);

    /** Runs the current line and adds its reply to the log. */
    void submit();
    /** Adds a line to the reply log, scrolling the older ones up. */
    void pushLog(const char *text);

    void clearLine();
    void recall(int offset);

    char line[max_line + 1] = {};
    unsigned int length = 0;

    char history[history_size][max_line + 1] = {};
    unsigned int history_count = 0;
    /** -1 while typing, otherwise how far back the up-arrow has stepped. */
    int history_cursor = -1;

    /**
     * The last few replies, newest last. Three lines is what the panel shows: the
     * reply to the command that was just run, and enough of the one before it to
     * see the effect of a command whose answer was "done".
     */
    static constexpr unsigned int log_size = 3;
    char log[log_size][96] = {};
    unsigned int log_count = 0;

    unsigned int blinked = 0;

    // Edge state for the console's own keys. A held key must not repeat, which is
    // why each one remembers whether it was down on the previous frame.
    bool esc_was_down = false;
    bool enter_was_down = false;
    bool back_was_down = false;
    bool up_was_down = false;
    bool down_was_down = false;
};

extern CommandTask command_task;

#endif // COMMANDTASK_H
