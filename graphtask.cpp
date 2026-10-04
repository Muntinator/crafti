#include "graphtask.h"

#include "controls.h"

#include <cstring>

#ifndef _TINSPIRE
#include <SDL/SDL.h>
#endif

#include "font.h"
#include "menuui.h"
#include "world.h"
#include "worldtask.h"
#include "starttask.h"

GraphTask graph_task;

namespace {
struct GraphPreset {
    const char *label;
    const char *expr;
};

static const GraphPreset k_graph_presets[] = {
    {"Sine Wave", "sin(x)"},
    {"Cosine Waves", "cos(x*y)"},
    {"Ripple", "sin(sqrt(x*x+y*y))"},
    {"Paraboloid", "(x*x+y*y)/120"},
    {"Saddle", "(x*x-y*y)/120"},
    {"Checker Wave", "sin(x)*cos(y)"},
    {"Complex z^2 (domain)", "c:z^2"},
    {"Complex z^3-1 (domain)", "c:z^3-1"},
    {"Complex 1/z (domain)", "c:1/z"},
    {"Complex sin(z) (domain)", "c:sin(z)"},
    {"Complex mobius (domain)", "c:(z^2+1)/(z^2-1)"},
    {"Implicit Sphere", "i:sphere"},
    {"Implicit Toroid", "i:toroid"},
};

static constexpr unsigned int k_graph_presets_count = sizeof(k_graph_presets) / sizeof(*k_graph_presets);
}

GraphTask::GraphTask()
{
}

GraphTask::~GraphTask()
{
}

void GraphTask::makeCurrent()
{
    std::strncpy(expression, world.graphExpression(), max_expr_len);
    expression[max_expr_len] = '\0';
    expression_len = std::strlen(expression);
    charset_index = 0;
    preset_index = 0;
    for(unsigned int i = 0; i < k_graph_presets_count; ++i)
    {
        if(std::strcmp(expression, k_graph_presets[i].expr) == 0)
        {
            preset_index = i;
            break;
        }
    }
    Task::makeCurrent();
}

void GraphTask::render()
{
    const int scale = MenuUI::uiScale();

    // The dirt all the vanilla menus use, a vanilla heading, and the expression
    // box drawn the way vanilla draws a text field: a black field inside a light
    // grey border.
    MenuUI::drawMenuBackground(*screen);
    MenuUI::drawHeading("Graphing Mode", *screen, MenuUI::headingY());
    drawStringCenter("Type z=f(x,y) and press Enter", MenuUI::Text, *screen, SCREEN_WIDTH / 2, 22 * scale);

    const int box_x = 12 * scale;
    const int box_y = 40 * scale;
    const int box_w = SCREEN_WIDTH - 24 * scale;
    const int box_h = 20 * scale;
    MenuUI::fillRect(*screen, box_x, box_y, box_w, box_h, 0x0000);
    drawRectangle(*screen, box_x, box_y, box_w, box_h, MenuUI::TextDisabled);
    drawString("z=", MenuUI::Text, *screen, box_x + 4 * scale, box_y + 6 * scale);
    drawString(expression, MenuUI::Text, *screen, box_x + 22 * scale, box_y + 6 * scale);

    static const char charset[] = "xyz0123456789+-*/^()., sincoartbpe";
    const char selected = charset[charset_index];

    char selected_text[20] = "Current char: ' '";
    selected_text[15] = selected;
    drawStringCenter(selected_text, MenuUI::Text, *screen, SCREEN_WIDTH / 2, 80 * scale);

    char preset_text[64];
    snprintf(preset_text, sizeof(preset_text), "Preset: %s", k_graph_presets[preset_index].label);
    drawStringCenter(preset_text, MenuUI::Text, *screen, SCREEN_WIDTH / 2, 92 * scale);

    drawString("Pad keys: char  Click: append  7: backspace", MenuUI::Text, *screen, 10 * scale, 106 * scale);
    drawString("Left/Right: preset  1: apply preset", MenuUI::Text, *screen, 10 * scale, 120 * scale);
    char fill_text[48];
    snprintf(fill_text, sizeof(fill_text), "+/-: fill depth n = %d", world.graphFillDepth());
    drawString(fill_text, MenuUI::Text, *screen, 10 * scale, 132 * scale);
    drawString("9: clear   Enter/T: start graph", MenuUI::Text, *screen, 10 * scale, 146 * scale);
    drawString("Shift: back", MenuUI::TextDisabled, *screen, 10 * scale, 160 * scale);

    drawString("Range: x,y in [-30,30]", MenuUI::Text, *screen, 10 * scale, 174 * scale);
    drawString("Tip: c:* for domain, i:* for implicit", MenuUI::Text, *screen, 10 * scale, 188 * scale);
}

void GraphTask::logic(GLFix /*dt*/)
{
    static const char charset[] = "xyz0123456789+-*/^()., sincoartbpe";
    const unsigned int charset_len = sizeof(charset) - 1;

    bool desktop_t_down = false;
#ifndef _TINSPIRE
    const Uint8 *keys = SDL_GetKeyState(nullptr);
    desktop_t_down = keys[SDLK_t] != 0;
#endif

    const bool submit_down = keyPressed(KEY_NSPIRE_ENTER) || desktop_t_down;
    // The pad's click key appends the selected character; on the desktop that
    // same key is the space bar, so it needs no separate case any more.
    const bool append_down = Controls::jump();

    if(key_held_down)
    {
        key_held_down = Controls::menu() || Controls::cursorUp() || Controls::cursorDown()
            || Controls::cursorLeft() || Controls::cursorRight()
            || keyPressed(KEY_NSPIRE_1) || keyPressed(KEY_NSPIRE_PLUS) || keyPressed(KEY_NSPIRE_MINUS)
            || keyPressed(KEY_NSPIRE_7) || keyPressed(KEY_NSPIRE_9)
            || append_down || submit_down;
        return;
    }

    if(Controls::menu())
    {
        start_task.makeCurrent();
        key_held_down = true;
        return;
    }

    if(Controls::cursorUp())
    {
        if(charset_index == 0)
            charset_index = charset_len - 1;
        else
            --charset_index;
        key_held_down = true;
        return;
    }

    if(Controls::cursorDown())
    {
        ++charset_index;
        if(charset_index >= charset_len)
            charset_index = 0;
        key_held_down = true;
        return;
    }

    if(Controls::cursorLeft())
    {
        if(preset_index == 0)
            preset_index = k_graph_presets_count - 1;
        else
            --preset_index;
        key_held_down = true;
        return;
    }

    if(Controls::cursorRight())
    {
        ++preset_index;
        if(preset_index >= k_graph_presets_count)
            preset_index = 0;
        key_held_down = true;
        return;
    }

    if(keyPressed(KEY_NSPIRE_1))
    {
        std::strncpy(expression, k_graph_presets[preset_index].expr, max_expr_len);
        expression[max_expr_len] = '\0';
        expression_len = std::strlen(expression);
        key_held_down = true;
        return;
    }

    if(keyPressed(KEY_NSPIRE_PLUS))
    {
        world.setGraphFillDepth(world.graphFillDepth() + 1);
        key_held_down = true;
        return;
    }

    if(keyPressed(KEY_NSPIRE_MINUS))
    {
        world.setGraphFillDepth(world.graphFillDepth() - 1);
        key_held_down = true;
        return;
    }

    if(keyPressed(KEY_NSPIRE_7))
    {
        if(expression_len > 0)
            expression[--expression_len] = '\0';
        key_held_down = true;
        return;
    }

    if(keyPressed(KEY_NSPIRE_9))
    {
        expression[0] = '\0';
        expression_len = 0;
        key_held_down = true;
        return;
    }

    if(append_down)
    {
        if(expression_len < max_expr_len)
        {
            expression[expression_len++] = charset[charset_index];
            expression[expression_len] = '\0';
        }
        key_held_down = true;
        return;
    }

    if(submit_down)
    {
        if(expression_len == 0)
        {
            std::strcpy(expression, "sin(x)");
            expression_len = 6;
        }

        if(!world.setGraphExpression(expression))
        {
            world_task.setMessage("Invalid graph function");
            key_held_down = true;
            return;
        }

        world.setWorldType(World::WorldType::Graph);
        world_task.resetWorld();
        world_task.makeCurrent();
        key_held_down = true;
    }
}
