// The in-game command console.
//
// This file is only the shell: a line, a log of the last few replies and the
// keys that move between them. The line is handed to worldcommands.cpp, which is
// where a command means something, so nothing here knows what /fill does.
//
// Drawing is done straight into the framebuffer for the panel and with
// drawString() for the text, which is the same split worldsky.cpp and
// worldweather.cpp use: the panel is a solid rectangle of a few thousand pixels,
// and a texture for it would be one more thing to allocate and free for no gain
// on a 320x240 screen.
//
// Two rules make typing bearable on a calculator. The first is that the console
// never asks for a modifier: every character a command needs has its own key
// (Task::textKeyPressed() lists them). The second is that a key only types once,
// however long it is held, and that the keys held when the console opened are
// forgotten -- otherwise the '/' that opened it would be the first character of
// the line.

#include "commandtask.h"

#include <stdio.h>
#include <string.h>

#include "command.h"
#include "font.h"
#include "gl.h"
#include "worldcommands.h"
#include "worldtask.h"

extern unsigned char font_dat[];

CommandTask command_task;

namespace
{
    /** Text colour: white, which is what every other overlay uses. */
    constexpr COLOR text_color = 0xFFFF;
    /** A dimmer line, for the hint list. */
    constexpr COLOR hint_color = 0xB5B6;

    /** Frames a cursor blink lasts. The logic loop is a few hertz, so this is slow enough to read. */
    constexpr unsigned int blink_frames = 6;

    /**
     * The console's own key, edge detected: the console is driven by the level of
     * a key, and a held Shift must close the console once rather than every frame.
     */
    bool justPressed(const t_key &key, bool &previous)
    {
        const bool down = Task::keyPressed(key);
        const bool pressed = down && !previous;
        previous = down;
        return pressed;
    }
}

unsigned int CommandTask::textWidth(const char *str)
{
    unsigned int width = 0;

    while(*str != '\0')
        width += font_dat[17 + static_cast<unsigned char>(*str++)];

    return width;
}

void CommandTask::fillPanel(int x, int y, int width, int height, unsigned short color)
{
    if(screen == nullptr || screen->bitmap == nullptr)
        return;

    unsigned short *pixels = screen->bitmap;

    for(int row = 0; row < height; ++row)
    {
        const int py = y + row;
        if(py < 0 || py >= SCREEN_HEIGHT)
            continue;

        for(int column = 0; column < width; ++column)
        {
            const int px = x + column;
            if(px < 0 || px >= SCREEN_WIDTH)
                continue;

            pixels[px + py * SCREEN_WIDTH] = color;
        }
    }
}

void CommandTask::dimPanel(int x, int y, int width, int height)
{
    if(screen == nullptr || screen->bitmap == nullptr)
        return;

    unsigned short *pixels = screen->bitmap;

    for(int row = 0; row < height; ++row)
    {
        const int py = y + row;
        if(py < 0 || py >= SCREEN_HEIGHT)
            continue;

        for(int column = 0; column < width; ++column)
        {
            const int px = x + column;
            if(px < 0 || px >= SCREEN_WIDTH)
                continue;

            // (c & 0xF7DE) >> 1 is a per-channel halving of a 565 pixel.
            pixels[px + py * SCREEN_WIDTH] = static_cast<unsigned short>(
                (pixels[px + py * SCREEN_WIDTH] & 0xF7DE) >> 1);
        }
    }
}

void CommandTask::open()
{
    // A key that is already down when the console opens belongs to the world, not
    // to the line: the '/' itself must not be typed, and neither must a direction
    // key the player was walking with.
    Task::resetTextKeys();
    clearLine();
    history_cursor = -1;

    // The log starts empty rather than showing the previous session's replies,
    // which would look like something had just been run.
    log_count = 0;
    for(unsigned int i = 0; i < log_size; ++i)
        log[i][0] = '\0';

    menu_was_down = enter_was_down = erase_was_down = up_was_down = down_was_down = false;

    makeCurrent();
}

void CommandTask::makeCurrent()
{
    // The frame the world just drew is what the panel sits on, exactly as the
    // settings and inventory overlays do it.
    if(!background_saved)
        saveBackground();

    Task::makeCurrent();
}

void CommandTask::clearLine()
{
    length = 0;
    line[0] = '\0';
}

void CommandTask::recall(int offset)
{
    if(history_count == 0)
        return;

    if(offset < 0)
        offset = 0;
    if(offset >= static_cast<int>(history_count))
        offset = static_cast<int>(history_count) - 1;

    history_cursor = offset;

    // Newest first: offset 0 is the command just run.
    const unsigned int index = history_count - 1 - static_cast<unsigned int>(offset);
    strncpy(line, history[index], max_line);
    line[max_line] = '\0';
    length = static_cast<unsigned int>(strlen(line));
}

void CommandTask::pushLog(const char *text)
{
    // The oldest line falls off the top, so the newest reply is always the last
    // one drawn.
    if(log_count == log_size)
    {
        for(unsigned int i = 1; i < log_size; ++i)
            memcpy(log[i - 1], log[i], sizeof(log[0]));
        --log_count;
    }

    strncpy(log[log_count], text, sizeof(log[0]) - 1);
    log[log_count][sizeof(log[0]) - 1] = '\0';
    ++log_count;
}

void CommandTask::submit()
{
    // An empty line is not an error, it is a way of dismissing the log.
    if(length == 0)
        return;

    char reply[96];
    runCommand(line, reply, sizeof(reply));

    // Every line is remembered, including a typo: the up-arrow is how a mistyped
    // command is corrected, and eating it would make the console lie about what
    // was run.
    if(history_count < history_size)
    {
        strncpy(history[history_count], line, max_line);
        history[history_count][max_line] = '\0';
        ++history_count;
    }
    else
    {
        // Full: the oldest command falls off the top, so the newest is always the
        // first one the up-arrow reaches.
        for(unsigned int i = 1; i < history_size; ++i)
            memcpy(history[i - 1], history[i], sizeof(history[0]));
        strncpy(history[history_size - 1], line, max_line);
        history[history_size - 1][max_line] = '\0';
    }

    if(reply[0] != '\0')
        pushLog(reply);

    history_cursor = -1;
    clearLine();
}

void CommandTask::logic(GLFix dt)
{
    (void)dt;

    ++blinked;

    // Every key is polled before anything acts on one, so a frame that runs a
    // command still updates the state of the rest: otherwise the letters held
    // while Enter is pressed would type themselves on the following frame.
    const bool menu = justPressed(KEY_NSPIRE_SHIFT, menu_was_down);
    const bool enter = justPressed(KEY_NSPIRE_ENTER, enter_was_down);
    const bool back = justPressed(KEY_NSPIRE_BAR, erase_was_down);
    const bool up = justPressed(KEY_NSPIRE_UP, up_was_down);
    const bool down = justPressed(KEY_NSPIRE_DOWN, down_was_down);
    const char typed = Task::textKeyPressed();

    if(menu)
    {
        world_task.makeCurrent();
        return;
    }

    if(enter)
    {
        submit();
        return;
    }

    if(back)
    {
        if(length > 0)
            line[--length] = '\0';
        return;
    }

    // The up-arrow walks back through what has been run, the down-arrow forward
    // again, and stepping past the newest line returns to an empty prompt.
    if(up)
    {
        if(history_cursor + 1 < static_cast<int>(history_count))
            recall(history_cursor + 1);
        return;
    }

    if(down)
    {
        if(history_cursor > 0)
            recall(history_cursor - 1);
        else
        {
            history_cursor = -1;
            clearLine();
        }
        return;
    }

    if(typed != 0 && length < max_line)
    {
        line[length++] = typed;
        line[length] = '\0';
    }
}

void CommandTask::render()
{
    drawBackground();

    const unsigned int line_height = fontHeight() + 2;
    // One prompt line, the log, and the hint line.
    const unsigned int rows = log_size + 2;
    const int panel_height = static_cast<int>(rows * line_height) + 4;

    // Vanilla's chat is a translucent black box over the world; the dim is that
    // box, baked in because nGL cannot blend.
    dimPanel(0, 0, SCREEN_WIDTH, panel_height);

    // The prompt. Long lines scroll rather than overflow the screen: the end of
    // what is being typed is what matters.
    char prompt[max_line + 3];
    snprintf(prompt, sizeof(prompt), "> %s", line);

    unsigned int available = SCREEN_WIDTH - 4;
    unsigned int width = textWidth(prompt);
    const char *drawn = prompt;
    while(width > available && drawn[1] != '\0')
    {
        width -= font_dat[17 + static_cast<unsigned char>(*drawn)];
        ++drawn;
    }

    drawString(drawn, text_color, *screen, 2, 2);

    // The cursor, blinking after the text.
    if((blinked / blink_frames) % 2 == 0 && textWidth(drawn) < available)
    {
        const int cursor_x = 2 + static_cast<int>(textWidth(drawn));
        fillPanel(cursor_x, 2, 4, static_cast<int>(fontHeight()), text_color);
    }

    const unsigned int log_row = 1;
    for(unsigned int i = 0; i < log_count; ++i)
    {
        const int y = static_cast<int>(line_height * (log_row + i)) + 2;
        drawString(log[i], text_color, *screen, 2, y);
    }

    // The hint line: what the current word could still become. It is the console's
    // only affordance on a machine with no completion list, so it is always drawn,
    // and it shortens as the line is typed rather than waiting to be asked.
    char prefix[Command::MaxTokenLength];
    prefix[0] = '\0';

    if(length > 0)
    {
        const char *space = strchr(line, ' ');
        const unsigned int word_length = space == nullptr
            ? length
            : static_cast<unsigned int>(space - line);

        if(word_length < sizeof(prefix))
        {
            memcpy(prefix, line, word_length);
            prefix[word_length] = '\0';
        }
    }

    // Once a command name is settled, suggesting other commands is noise; the
    // argument the player is on is then the world's business, not the console's.
    if(strchr(line, ' ') == nullptr)
    {
        char hints[128];
        hints[0] = '\0';

        for(int i = 0; i < Command::commandCount(); ++i)
        {
            if(!Command::commandMatchesPrefix(i, prefix))
                continue;

            char entry[32];
            snprintf(entry, sizeof(entry), "%s/%s", hints[0] == '\0' ? "" : " ", Command::commandName(i));

            // The line is measured as it is built: the list of all ten commands is
            // wider than the screen, and a hint that runs off the edge is worse
            // than a shorter one.
            if(textWidth(hints) + textWidth(entry) > SCREEN_WIDTH - 6)
                break;

            strncat(hints, entry, sizeof(hints) - strlen(hints) - 1);
        }

        if(hints[0] != '\0')
        {
            const int y = static_cast<int>(line_height * (log_row + log_size)) + 2;
            drawString(hints, hint_color, *screen, 2, y);
        }
    }
}
