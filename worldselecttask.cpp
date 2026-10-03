#include "worldselecttask.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "audio_manager.h"

#ifndef _TINSPIRE
#include <SDL/SDL.h>
#endif

#include "font.h"
#include "graphtask.h"
#include "menuui.h"
#include "starttask.h"
#include "worldtask.h"

#include "textures/world_icon.h"
#include "textures/world_icon_overlay.h"

WorldSelectTask worldselect_task;

namespace
{
    /** Vanilla's green "New!" tag, 0x55FF55. */
    constexpr COLOR NewTag = MenuUI::rgb(85, 255, 85);

    /**
     * A nearest-neighbour blit of one rectangle of a source texture into a box of
     * the destination, skipping the source's transparent colour. The same helper
     * MenuUI keeps privately for its button art, needed here for the row icons.
     */
    void blit(const TEXTURE &src, TEXTURE &dest,
              int src_x, int src_y, int src_w, int src_h,
              int dest_x, int dest_y, int dest_w, int dest_h)
    {
        if(src_w <= 0 || src_h <= 0 || dest_w <= 0 || dest_h <= 0)
            return;

        for(int y = 0; y < dest_h; ++y)
        {
            const int py = dest_y + y;
            if(py < 0 || py >= static_cast<int>(dest.height))
                continue;
            const int sy = src_y + y * src_h / dest_h;
            if(sy < 0 || sy >= static_cast<int>(src.height))
                continue;

            COLOR *out = dest.bitmap + py * dest.width;
            const COLOR *in = src.bitmap + sy * src.width;
            for(int x = 0; x < dest_w; ++x)
            {
                const int px = dest_x + x;
                if(px < 0 || px >= static_cast<int>(dest.width))
                    continue;
                const int sx = src_x + x * src_w / dest_w;
                if(sx < 0 || sx >= static_cast<int>(src.width))
                    continue;

                const COLOR c = in[sx];
                if(src.has_transparency && c == src.transparent_color)
                    continue;
                out[px] = c;
            }
        }
    }

    /** The engine's world kinds, in the order the World Type button offers them. */
    World::WorldType typeForIndex(int index)
    {
        switch(index)
        {
        case 1: return World::WorldType::Flat;
        case 2: return World::WorldType::Graph;
        default: return World::WorldType::Terrain;
        }
    }

    /** Case-insensitive substring test, which is what vanilla's search does. */
    bool matches(const char *name, const char *query)
    {
        if(query == nullptr || query[0] == '\0')
            return true;

        for(const char *n = name; *n != '\0'; ++n)
        {
            const char *a = n, *b = query;
            while(*a != '\0' && *b != '\0')
            {
                char ca = *a, cb = *b;
                if(ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
                if(cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
                if(ca != cb)
                    break;
                ++a;
                ++b;
            }
            if(*b == '\0')
                return true;
        }
        return false;
    }

    /** Milliseconds on the clock, for vanilla's double-click window. */
    unsigned long now_ms()
    {
        return static_cast<unsigned long>(clock()) * 1000UL
            / static_cast<unsigned long>(CLOCKS_PER_SEC);
    }
}

WorldSelectTask::WorldSelectTask()
{
    search[0] = '\0';
    name_field[0] = '\0';
}

WorldSelectTask::~WorldSelectTask()
{
}

void WorldSelectTask::rebuildDetails(WorldEntry &entry)
{
    // Vanilla's `LevelSummary` lines: `levelId (date)` under the name, and the
    // version/mode line under that. This engine keeps no play dates, so every
    // row reads vanilla's own never-played subtitle.
    snprintf(entry.sub, sizeof(entry.sub), "%s (%s)",
             MenuUI::worldSelectWorldWord, MenuUI::worldSelectNeverPlayed);
    snprintf(entry.info, sizeof(entry.info), "Version: 1.17.1, %s mode",
             MenuUI::gameModeValues[entry.game_mode]);
}

void WorldSelectTask::rebuildWorlds()
{
    // The worlds made this session survive a rebuild; the built-in rows are
    // rebuilt around them, since the saved world may have appeared or gone.
    // A session world keeps its place at the top -- vanilla sorts by when the
    // world was last played, which puts a world made today first.
    WorldEntry session[max_worlds];
    int session_count = 0;
    for(int i = 0; i < world_count; ++i)
        if(!worlds[i].saved && worlds[i].is_new && session_count < max_worlds)
            session[session_count++] = worlds[i];

    world_count = 0;
    for(int i = 0; i < session_count; ++i)
        worlds[world_count++] = session[i];

    // The saved world comes next when there is one; without one the same row is
    // a fresh terrain world to open.
    WorldEntry &first = worlds[world_count++];
    snprintf(first.name, sizeof(first.name), "%s", MenuUI::worldSelectDefaultName);
    first.type = World::WorldType::Terrain;
    first.game_mode = 0;
    first.saved = start_task.savedWorldExists();
    first.is_new = false;

    // The other world kinds the engine can make, as rows of their own.
    struct TemplateWorld { const char *name; World::WorldType type; };
    static const TemplateWorld templates[] = {
        { "Flat World", World::WorldType::Flat },
        { "Graphing World", World::WorldType::Graph }
    };
    for(const TemplateWorld &t : templates)
    {
        if(world_count >= max_worlds)
            break;
        WorldEntry &entry = worlds[world_count++];
        snprintf(entry.name, sizeof(entry.name), "%s", t.name);
        entry.type = t.type;
        entry.game_mode = 0;
        entry.saved = false;
        entry.is_new = false;
    }

    for(int i = 0; i < world_count; ++i)
        rebuildDetails(worlds[i]);
}

int WorldSelectTask::filteredCount() const
{
    int count = 0;
    for(int i = 0; i < world_count; ++i)
        if(matches(worlds[i].name, search))
            ++count;
    return count;
}

int WorldSelectTask::entryIndex(int row) const
{
    int seen = 0;
    for(int i = 0; i < world_count; ++i)
        if(matches(worlds[i].name, search))
        {
            if(seen == row)
                return i;
            ++seen;
        }
    return -1;
}

WorldSelectTask::WorldEntry *WorldSelectTask::selectedWorld()
{
    const int index = entryIndex(selected);
    return index < 0 ? nullptr : &worlds[index];
}

int WorldSelectTask::wrapWarning(const char *name, char lines[][80], int max_lines)
{
    char text[96];
    snprintf(text, sizeof(text), MenuUI::deleteWarningFormat, name);

    const int room = SCREEN_WIDTH - 16 * MenuUI::uiScale();
    if(max_lines < 2 || static_cast<int>(measureString(text)) <= room)
    {
        snprintf(lines[0], 80, "%.79s", text);
        return 1;
    }

    // Split at the space nearest the middle where both halves still fit, which
    // is how vanilla's MultiLineLabel breaks the warning over two lines.
    int best = -1, best_score = 1 << 30;
    for(const char *p = text; *p != '\0'; ++p)
    {
        if(*p != ' ')
            continue;

        char a[80], b[80];
        snprintf(a, sizeof(a), "%.*s", static_cast<int>(p - text), text);
        snprintf(b, sizeof(b), "%.79s", p + 1);
        const int wa = static_cast<int>(measureString(a));
        const int wb = static_cast<int>(measureString(b));
        if(wa > room || wb > room)
            continue;

        const int score = wa > wb ? wa : wb;
        if(score < best_score)
        {
            best_score = score;
            best = static_cast<int>(p - text);
        }
    }

    if(best < 0)
    {
        snprintf(lines[0], 80, "%.79s", text);
        return 1;
    }

    snprintf(lines[0], 80, "%.*s", best, text);
    snprintf(lines[1], 80, "%.79s", text + best + 1);
    return 2;
}

void WorldSelectTask::makeCurrent()
{
    rebuildWorlds();

    if(selected >= filteredCount() || selected < 0)
        selected = 0;
    if(scroll > selected)
        scroll = selected;

    focus_button = -1;
    form_focus = 0;
    typing = false;

    // A key that was already held (the one that opened the screen) must not
    // count as the first thing typed into a text field.
    Task::resetTextKeys();

#ifndef _TINSPIRE
    SDL_PumpEvents();
    SDL_GetMouseState(&last_mouse_x, &last_mouse_y);
    left_mouse_was_down = false;
    last_click_row = -1;
    hover_row = -1;
    hover_icon = false;
#endif

    Task::makeCurrent();
}

void WorldSelectTask::render()
{
    switch(view)
    {
    case CREATE_VIEW: renderForm(true); break;
    case EDIT_VIEW: renderForm(false); break;
    case DELETE_VIEW: renderDelete(); break;
    default: renderList(); break;
    }
}

void WorldSelectTask::renderList()
{
    TEXTURE &tex = *screen;
    const MenuUI::WorldSelectLayout l = MenuUI::worldSelectLayout();
    const int s = l.scale;

    // The dirt every vanilla list screen is drawn on, dimmed the menu way.
    MenuUI::drawMenuBackground(tex);
    MenuUI::drawHeading(MenuUI::worldSelectHeading, tex, l.heading_y);

    MenuUI::drawEditBox(tex, l.search_x, l.search_y, l.search_w, l.search_h,
                        search, MenuUI::worldSelectSearchHint, typing);

    // The rows sit on vanilla's own darkened dirt: the menu backdrop is at
    // vanilla's (64,64,64) strip colour and the interior drops to (32,32,32) --
    // halving what is already there -- with the four-pixel fades at both edges.
    for(int y = l.list_top; y < l.list_bottom; y += s)
    {
        int keep = 50;
        const int from_top = (y - l.list_top) / s;
        const int from_bottom = (l.list_bottom - y) / s;
        if(from_top < MenuUI::WorldFadeHeight)
            keep = 50 + (MenuUI::WorldFadeHeight - from_top) * 12;
        else if(from_bottom <= MenuUI::WorldFadeHeight)
            keep = 50 + (MenuUI::WorldFadeHeight - from_bottom) * 12;
        MenuUI::shadeRect(tex, 0, y, SCREEN_WIDTH, s, keep);
    }

    const int total = filteredCount();
    const int rows = l.visibleRows();

    for(int row = scroll; row < scroll + rows && row < total; ++row)
    {
        const int y = l.rowY(row, scroll);
        const int index = entryIndex(row);
        if(index < 0)
            continue;

        if(row == selected)
        {
            // Vanilla's selection: a grey frame two pixels around the row's
            // content box, blacked out one pixel inside it.
            MenuUI::fillRect(tex, l.box_x, y - 2 * s, l.box_w, l.row_h + 4 * s,
                             MenuUI::ScrollbarThumb);
            MenuUI::fillRect(tex, l.box_x + s, y - s, l.box_w - 2 * s, l.row_h + 2 * s,
                             MenuUI::Black);
        }

        blit(world_icon, tex, 0, 0, static_cast<int>(world_icon.width),
             static_cast<int>(world_icon.height), l.row_left, y, l.icon_size, l.icon_size);

        const WorldEntry &entry = worlds[index];
        MenuUI::drawWorldEntry(entry.name, entry.sub, entry.info, tex, l.row_left, y);

        if(entry.is_new)
        {
            // Vanilla's green "New!" tag, at the end of the name line.
            const int tag_x = l.text_x + static_cast<int>(measureString(entry.name)) + 3 * s;
            drawString(MenuUI::worldSelectNewTag, NewTag, tex, tag_x, y + MenuUI::WorldRowNameY * s);
        }

#ifndef _TINSPIRE
        if(row == hover_row)
        {
            // Vanilla darkens the icon and lays the join tile over it: the
            // bright one when the pointer is on the icon itself.
            MenuUI::shadeRect(tex, l.row_left, y, l.icon_size, l.icon_size, 37);
            const int tile_y = hover_icon ? 32 : 0;
            blit(world_icon_overlay, tex, 0, tile_y, 32, 32,
                 l.row_left, y, l.icon_size, l.icon_size);
        }
#endif
    }

    // The scrollbar, vanilla's black track and grey thumb with its lit edge.
    if(total > rows)
    {
        const int x = l.row_left + l.row_w;
        const int track_h = l.list_bottom - l.list_top;
        MenuUI::fillRect(tex, x, l.list_top, MenuUI::WorldScrollbarWidth * s, track_h,
                         MenuUI::Black);

        int thumb_h = track_h * rows / total;
        if(thumb_h < 2 * s)
            thumb_h = 2 * s;
        const int max_scroll = total - rows;
        const int thumb_y = l.list_top + (track_h - thumb_h) * scroll / max_scroll;
        MenuUI::fillRect(tex, x, thumb_y, MenuUI::WorldScrollbarWidth * s, thumb_h,
                         MenuUI::ScrollbarThumb);
        MenuUI::fillRect(tex, x, thumb_y, s, thumb_h, MenuUI::ScrollbarEdge);
    }

    // Vanilla's button block: the two 150-wide buttons, then the four 72-wide
    // ones. Play, Edit, Delete and Re-Create are greyed while there is nothing
    // in the list to point them at.
    for(int action = 0; action < MenuUI::WorldActionCount; ++action)
    {
        int x, y, w, h;
        l.buttonRect(action, x, y, w, h);

        bool enabled = true;
        if(action == MenuUI::WorldPlay || action == MenuUI::WorldEdit
           || action == MenuUI::WorldDelete || action == MenuUI::WorldRecreate)
            enabled = selectedWorld() != nullptr;

        const bool focused = action == focus_button;
        MenuUI::drawButton(tex, x, y, w, h, focused, enabled);
        MenuUI::drawButtonLabel(MenuUI::worldSelectActionLabels[action], tex, x, y, w, h,
                                focused, enabled);
    }
}

void WorldSelectTask::renderForm(bool create)
{
    TEXTURE &tex = *screen;
    const MenuUI::WorldFormLayout f = MenuUI::worldFormLayout();
    const int s = f.scale;

    MenuUI::drawMenuBackground(tex);
    MenuUI::drawHeading(create ? MenuUI::createHeading : MenuUI::editHeading, tex, f.heading_y);

    // Vanilla's grey label over the name field, and the field itself.
    drawString(MenuUI::nameLabel, MenuUI::EditBoxBorder, tex,
               SCREEN_WIDTH / 2 - 100 * s, f.label_y);
    MenuUI::drawEditBox(tex, f.field_x, f.field_y, f.field_w, f.field_h,
                        name_field, MenuUI::worldSelectDefaultName, form_focus == 0);

    char result[64];
    snprintf(result, sizeof(result), "%s %s", MenuUI::resultFolderLabel,
             name_field[0] != '\0' ? name_field : MenuUI::worldSelectDefaultName);
    drawString(result, MenuUI::EditBoxBorder, tex,
               SCREEN_WIDTH / 2 - 100 * s, f.result_y);

    if(create)
    {
        // The two cycle buttons, with the mode's own help lines under them --
        // vanilla's `CreateWorldScreen` middle block.
        char label[40];
        int x, y, w, h;

        f.optionRect(0, x, y, w, h);
        MenuUI::formatOptionLabel(label, sizeof(label), MenuUI::gameModeLabel,
                                  MenuUI::gameModeValues[game_mode]);
        MenuUI::drawButton(tex, x, y, w, h, form_focus == 1);
        MenuUI::drawButtonLabel(label, tex, x, y, w, h, form_focus == 1);

        f.optionRect(1, x, y, w, h);
        MenuUI::formatOptionLabel(label, sizeof(label), MenuUI::worldTypeLabel,
                                  MenuUI::worldTypeValues[world_type_index]);
        MenuUI::drawButton(tex, x, y, w, h, form_focus == 2);
        MenuUI::drawButtonLabel(label, tex, x, y, w, h, form_focus == 2);

        for(int line = 0; line < 2; ++line)
            drawString(MenuUI::gameModeHelp[game_mode][line], MenuUI::Text, tex,
                       SCREEN_WIDTH / 2 - 150 * s, f.help_y + line * f.help_pitch);
    }

    // The bottom row: the confirm button, greyed while the name is empty, and
    // vanilla's Cancel beside it.
    const int primary_focus = create ? 3 : 1;
    const int cancel_focus = create ? 4 : 2;
    const bool named = name_field[0] != '\0';

    int x, y, w, h;
    f.bottomRect(0, x, y, w, h);
    MenuUI::drawButton(tex, x, y, w, h, form_focus == primary_focus, named);
    MenuUI::drawButtonLabel(create ? MenuUI::createConfirmLabel : MenuUI::saveLabel,
                            tex, x, y, w, h, form_focus == primary_focus, named);

    f.bottomRect(1, x, y, w, h);
    MenuUI::drawButton(tex, x, y, w, h, form_focus == cancel_focus);
    MenuUI::drawButtonLabel(MenuUI::cancelLabel, tex, x, y, w, h, form_focus == cancel_focus);
}

void WorldSelectTask::renderDelete()
{
    TEXTURE &tex = *screen;
    const MenuUI::ConfirmLayout c = MenuUI::confirmLayout(delete_message_lines);

    MenuUI::drawMenuBackground(tex);

    // Vanilla's `ConfirmScreen`: the question centred at y 70, the loss spelled
    // out under it at y 90, and the two buttons under that.
    MenuUI::drawHeading(MenuUI::deleteQuestion, tex, c.title_y);
    for(int line = 0; line < delete_message_lines; ++line)
        drawStringCenter(delete_message[line], MenuUI::Text, tex, SCREEN_WIDTH / 2,
                         c.message_y + line * c.line_pitch);

    int x, y, w, h;
    c.buttonRect(0, x, y, w, h);
    MenuUI::drawButton(tex, x, y, w, h, delete_focus == 0);
    MenuUI::drawButtonLabel(MenuUI::deleteConfirmLabel, tex, x, y, w, h, delete_focus == 0);

    c.buttonRect(1, x, y, w, h);
    MenuUI::drawButton(tex, x, y, w, h, delete_focus == 1);
    MenuUI::drawButtonLabel(MenuUI::cancelLabel, tex, x, y, w, h, delete_focus == 1);
}

void WorldSelectTask::playSelected(bool re_create)
{
    WorldEntry *entry = selectedWorld();
    if(entry == nullptr)
        return;

    if(entry->saved && !re_create)
    {
        // The save file's world: it is already loaded, so play is a switch.
        world_task.makeCurrent();
        return;
    }

    world.setWorldType(entry->type);
    world_task.setGamemode(entry->game_mode);
    if(entry->type == World::WorldType::Graph)
    {
        // A graphing world opens the expression editor, which opens the world.
        graph_task.makeCurrent();
        return;
    }

    world_task.resetWorld();
    world_task.makeCurrent();
}

void WorldSelectTask::activateButton(int action)
{
    GameAudio::uiClick();

    switch(action)
    {
    case MenuUI::WorldPlay:
        playSelected(false);
        break;

    case MenuUI::WorldCreate:
        snprintf(name_field, sizeof(name_field), "%s", MenuUI::worldSelectDefaultName);
        game_mode = 0;
        world_type_index = 0;
        form_focus = 0;
        view = CREATE_VIEW;
        Task::resetTextKeys();
        break;

    case MenuUI::WorldEdit:
    {
        const int index = entryIndex(selected);
        if(index < 0)
            break;
        edit_target = index;
        snprintf(name_field, sizeof(name_field), "%s", worlds[index].name);
        form_focus = 0;
        view = EDIT_VIEW;
        Task::resetTextKeys();
        break;
    }

    case MenuUI::WorldDelete:
    {
        const int index = entryIndex(selected);
        if(index < 0)
            break;
        delete_target = index;
        delete_message_lines = wrapWarning(worlds[index].name, delete_message, 2);
        delete_focus = 1; // Cancel, so a stray key cannot delete a world
        view = DELETE_VIEW;
        Task::resetTextKeys();
        break;
    }

    case MenuUI::WorldRecreate:
        playSelected(true);
        break;

    case MenuUI::WorldCancel:
        start_task.makeCurrent();
        break;

    default:
        break;
    }
}

void WorldSelectTask::confirmForm(bool create)
{
    if(name_field[0] == '\0')
        return;

    GameAudio::uiClick();

    if(create)
    {
        // The new world goes to the top of the list with vanilla's "New!" tag,
        // and then opens -- vanilla's create button makes the world and drops
        // the player into it.
        if(world_count < max_worlds)
        {
            for(int i = world_count; i > 0; --i)
                worlds[i] = worlds[i - 1];
            ++world_count;
        }

        WorldEntry &entry = worlds[0];
        snprintf(entry.name, sizeof(entry.name), "%s", name_field);
        entry.type = typeForIndex(world_type_index);
        entry.game_mode = game_mode;
        entry.saved = false;
        entry.is_new = true;
        rebuildDetails(entry);

        search[0] = '\0';
        selected = 0;
        scroll = 0;
        view = LIST_VIEW;
        playSelected(false);
        return;
    }

    if(edit_target >= 0 && edit_target < world_count)
        snprintf(worlds[edit_target].name, sizeof(worlds[edit_target].name), "%s", name_field);
    view = LIST_VIEW;
}

void WorldSelectTask::confirmDelete()
{
    GameAudio::uiClick();

    if(delete_target >= 0 && delete_target < world_count)
    {
        // Deleting a row takes it out of the list; the file on disk is left
        // alone, and the row that says there is a saved world goes with the
        // flag that says so.
        const bool was_saved = worlds[delete_target].saved;
        for(int i = delete_target; i + 1 < world_count; ++i)
            worlds[i] = worlds[i + 1];
        --world_count;

        if(was_saved)
            start_task.setHasSavedWorld(false);
        rebuildWorlds();
    }

    if(selected >= filteredCount())
        selected = filteredCount() > 0 ? filteredCount() - 1 : 0;
    view = LIST_VIEW;
}

void WorldSelectTask::editField(char *text, unsigned int capacity, char typed, bool &changed)
{
    changed = false;
    if(typed < ' ' || typed >= 0x7F || capacity == 0)
        return;

    const size_t len = strlen(text);
    if(len + 1 >= capacity)
        return;

    text[len] = typed;
    text[len + 1] = '\0';
    changed = true;
}

void WorldSelectTask::logic(GLFix /*dt*/)
{
    switch(view)
    {
    case CREATE_VIEW: logicForm(true); break;
    case EDIT_VIEW: logicForm(false); break;
    case DELETE_VIEW: logicDelete(); break;
    default: logicList(); break;
    }
}

void WorldSelectTask::logicList()
{
    const MenuUI::WorldSelectLayout l = MenuUI::worldSelectLayout();
    const char typed = Task::textKeyPressed();

#ifndef _TINSPIRE
    // A desktop has a mouse, so it gets the focus-by-hover vanilla has: the
    // pointer picks the row or the button, and a click takes it. A double click
    // within vanilla's 250 ms window -- or a click on the row's icon -- plays.
    SDL_PumpEvents();
    int mouse_x = 0, mouse_y = 0;
    const Uint8 buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    const bool left_down = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;

    const int s = l.scale;
    const int mouse_total = filteredCount();

    hover_row = -1;
    hover_icon = false;
    for(int row = scroll; row < scroll + l.visibleRows() && row < mouse_total; ++row)
    {
        const int y = l.rowY(row, scroll);
        if(mouse_x >= l.box_x && mouse_x < l.box_x + l.box_w
           && mouse_y >= y - 2 * s && mouse_y < y + l.row_h + 2 * s)
        {
            hover_row = row;
            hover_icon = mouse_x < l.row_left + l.icon_size;
        }
    }

    int hover_button = -1;
    for(int action = 0; action < MenuUI::WorldActionCount; ++action)
    {
        int x, y, w, h;
        l.buttonRect(action, x, y, w, h);
        if(mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h)
            hover_button = action;
    }

    // Only a pointer that has moved takes the focus; a resting pointer leaves
    // the keyboard in charge. A click still takes whatever is under it.
    const bool mouse_moved = (mouse_x != last_mouse_x || mouse_y != last_mouse_y);
    last_mouse_x = mouse_x;
    last_mouse_y = mouse_y;

    if(mouse_moved)
    {
        if(hover_row >= 0)
        {
            selected = hover_row;
            typing = false;
        }
        if(hover_button >= 0)
            focus_button = hover_button;
    }

    if(left_down && !left_mouse_was_down)
    {
        left_mouse_was_down = true;
        bool consumed = false;

        if(hover_button >= 0)
        {
            bool enabled = true;
            if(hover_button == MenuUI::WorldPlay || hover_button == MenuUI::WorldEdit
               || hover_button == MenuUI::WorldDelete || hover_button == MenuUI::WorldRecreate)
                enabled = selectedWorld() != nullptr;
            if(enabled)
            {
                activateButton(hover_button);
                return;
            }
        }

        if(hover_row >= 0)
        {
            selected = hover_row;
            typing = false;
            consumed = true;

            const unsigned long now = now_ms();
            const bool double_click = (last_click_row == hover_row
                                       && now - last_click_ms <= 250);
            last_click_row = hover_row;
            last_click_ms = now;

            if(hover_icon || double_click)
            {
                last_click_row = -1;
                GameAudio::uiClick();
                playSelected(false);
                return;
            }
        }

        // A click on the search box gives it the keyboard.
        if(!consumed
           && mouse_x >= l.search_x && mouse_x < l.search_x + l.search_w
           && mouse_y >= l.search_y && mouse_y < l.search_y + l.search_h)
        {
            typing = true;
            focus_button = -1;
        }
    }
    if(!left_down)
        left_mouse_was_down = false;
#endif

    if(key_held_down)
    {
        key_held_down = keyPressed(KEY_NSPIRE_ESC) || keyPressed(KEY_NSPIRE_UP)
            || keyPressed(KEY_NSPIRE_DOWN) || keyPressed(KEY_NSPIRE_8)
            || keyPressed(KEY_NSPIRE_2) || keyPressed(KEY_NSPIRE_5)
            || keyPressed(KEY_NSPIRE_CLICK) || keyPressed(KEY_NSPIRE_ENTER)
            || keyPressed(KEY_NSPIRE_MENU) || keyPressed(KEY_NSPIRE_DEL);
        return;
    }

    const int total = filteredCount();
    const int rows = l.visibleRows();

    // Keeps the selection inside the window, moving as little as possible.
    const auto keep_in_view = [&]() {
        scroll = MenuUI::optionsScrollFor(selected, rows, total, scroll);
    };

    if(typing)
    {
        // While the search box has the keyboard, every character filters and the
        // arrow keys alone move the selection, so the letters can be typed.
        if(keyPressed(KEY_NSPIRE_DEL))
        {
            const size_t len = strlen(search);
            if(len > 0)
                search[len - 1] = '\0';
            selected = 0;
            scroll = 0;
            key_held_down = true;
            return;
        }
        if(keyPressed(KEY_NSPIRE_UP))
        {
            if(selected > 0)
                --selected;
            keep_in_view();
            key_held_down = true;
            return;
        }
        if(keyPressed(KEY_NSPIRE_DOWN))
        {
            if(selected + 1 < total)
                ++selected;
            keep_in_view();
            key_held_down = true;
            return;
        }
        if(keyPressed(KEY_NSPIRE_ENTER) || keyPressed(KEY_NSPIRE_CLICK))
        {
            GameAudio::uiClick();
            playSelected(false);
            key_held_down = true;
            return;
        }
        if(keyPressed(KEY_NSPIRE_ESC))
        {
            typing = false;
            key_held_down = true;
            return;
        }

        if(typed != 0)
        {
            const size_t len = strlen(search);
            if(len + 1 < sizeof(search))
            {
                search[len] = typed;
                search[len + 1] = '\0';
            }
            selected = 0;
            scroll = 0;
        }
        return;
    }

    if(keyPressed(KEY_NSPIRE_UP) || keyPressed(KEY_NSPIRE_8))
    {
        if(selected > 0)
            --selected;
        keep_in_view();
        key_held_down = true;
        return;
    }
    if(keyPressed(KEY_NSPIRE_DOWN) || keyPressed(KEY_NSPIRE_2))
    {
        if(selected + 1 < total)
            ++selected;
        keep_in_view();
        key_held_down = true;
        return;
    }
    if(keyPressed(KEY_NSPIRE_MENU))
    {
        // Tab walks the six buttons; Enter then takes the one it lands on. With
        // no button in focus, Enter plays the selected world.
        focus_button = focus_button < 0 ? 0 : (focus_button + 1) % MenuUI::WorldActionCount;
        key_held_down = true;
        return;
    }
    if(keyPressed(KEY_NSPIRE_ENTER) || keyPressed(KEY_NSPIRE_5) || keyPressed(KEY_NSPIRE_CLICK))
    {
        if(focus_button >= 0)
            activateButton(focus_button);
        else
        {
            GameAudio::uiClick();
            playSelected(false);
        }
        key_held_down = true;
        return;
    }
    if(keyPressed(KEY_NSPIRE_ESC))
    {
        start_task.makeCurrent();
        key_held_down = true;
        return;
    }

    // A character typed with no button in the way gives the search box the
    // keyboard and starts the filter.
    if(typed != 0)
    {
        typing = true;
        const size_t len = strlen(search);
        if(len + 1 < sizeof(search))
        {
            search[len] = typed;
            search[len + 1] = '\0';
        }
        selected = 0;
        scroll = 0;
    }
}

void WorldSelectTask::logicForm(bool create)
{
    const char typed = Task::textKeyPressed();
    const int focus_count = create ? 5 : 3;
    const int primary_focus = create ? 3 : 1;
    const int cancel_focus = create ? 4 : 2;

#ifndef _TINSPIRE
    SDL_PumpEvents();
    int mouse_x = 0, mouse_y = 0;
    const Uint8 buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    const bool left_down = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    const bool left_click = left_down && !left_mouse_was_down;
    left_mouse_was_down = left_down;

    const MenuUI::WorldFormLayout f = MenuUI::worldFormLayout();

    if(left_click)
    {
        int hit = -1;

        if(mouse_x >= f.field_x && mouse_x < f.field_x + f.field_w
           && mouse_y >= f.field_y && mouse_y < f.field_y + f.field_h)
            hit = 0;

        if(create)
        {
            for(int i = 0; i < 2; ++i)
            {
                int x, y, w, h;
                f.optionRect(i, x, y, w, h);
                if(mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h)
                    hit = 1 + i;
            }
        }

        for(int i = 0; i < 2; ++i)
        {
            int x, y, w, h;
            f.bottomRect(i, x, y, w, h);
            if(mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h)
                hit = i == 0 ? primary_focus : cancel_focus;
        }

        if(hit >= 0)
        {
            form_focus = hit;

            // A cycle button cycles on a click, the way vanilla's does.
            if(create && (hit == 1 || hit == 2))
            {
                if(hit == 1)
                    game_mode = (game_mode + 1) % MenuUI::gameModeCount;
                else
                    world_type_index = (world_type_index + 1) % MenuUI::worldTypeCount;
                GameAudio::uiClick();
                return;
            }
            if(hit == primary_focus)
            {
                confirmForm(create);
                return;
            }
            if(hit == cancel_focus)
            {
                GameAudio::uiClick();
                view = LIST_VIEW;
                return;
            }
            return;
        }
    }
#endif

    if(key_held_down)
    {
        key_held_down = keyPressed(KEY_NSPIRE_ESC) || keyPressed(KEY_NSPIRE_ENTER)
            || keyPressed(KEY_NSPIRE_5) || keyPressed(KEY_NSPIRE_CLICK)
            || keyPressed(KEY_NSPIRE_MENU) || keyPressed(KEY_NSPIRE_DEL)
            || keyPressed(KEY_NSPIRE_LEFT) || keyPressed(KEY_NSPIRE_RIGHT)
            || keyPressed(KEY_NSPIRE_4) || keyPressed(KEY_NSPIRE_6);
        return;
    }

    if(keyPressed(KEY_NSPIRE_DEL))
    {
        const size_t len = strlen(name_field);
        if(len > 0)
            name_field[len - 1] = '\0';
        form_focus = 0;
        key_held_down = true;
        return;
    }

    // Activation: Enter or the click key. The desktop's space bar shares the
    // click key with the calculator's 5 and belongs to the name, so a space
    // typed this frame does not activate; the digit 5 is reserved the same way.
    const bool activate = keyPressed(KEY_NSPIRE_ENTER) || keyPressed(KEY_NSPIRE_CLICK)
        || (keyPressed(KEY_NSPIRE_5) && typed != ' ');

    if(activate)
    {
        key_held_down = true;

        if(create && (form_focus == 1 || form_focus == 2))
        {
            if(form_focus == 1)
                game_mode = (game_mode + 1) % MenuUI::gameModeCount;
            else
                world_type_index = (world_type_index + 1) % MenuUI::worldTypeCount;
            return;
        }
        if(form_focus == cancel_focus)
        {
            GameAudio::uiClick();
            view = LIST_VIEW;
            return;
        }

        // The confirm button, which Enter also runs from the name field.
        confirmForm(create);
        return;
    }

    if(keyPressed(KEY_NSPIRE_ESC))
    {
        GameAudio::uiClick();
        view = LIST_VIEW;
        key_held_down = true;
        return;
    }

    // A typed character edits the name, whatever the focus is on.
    if(typed != 0 && typed != '5')
    {
        bool changed = false;
        editField(name_field, sizeof(name_field), typed, changed);
        if(changed)
            form_focus = 0;
        return;
    }

    if(keyPressed(KEY_NSPIRE_MENU))
    {
        form_focus = (form_focus + 1) % focus_count;
        key_held_down = true;
        return;
    }
    if(keyPressed(KEY_NSPIRE_LEFT) || keyPressed(KEY_NSPIRE_4))
    {
        if(create && form_focus == 1)
            game_mode = (game_mode + MenuUI::gameModeCount - 1) % MenuUI::gameModeCount;
        else if(create && form_focus == 2)
            world_type_index = (world_type_index + MenuUI::worldTypeCount - 1) % MenuUI::worldTypeCount;
        else
            form_focus = (form_focus + focus_count - 1) % focus_count;
        key_held_down = true;
        return;
    }
    if(keyPressed(KEY_NSPIRE_RIGHT) || keyPressed(KEY_NSPIRE_6))
    {
        if(create && form_focus == 1)
            game_mode = (game_mode + 1) % MenuUI::gameModeCount;
        else if(create && form_focus == 2)
            world_type_index = (world_type_index + 1) % MenuUI::worldTypeCount;
        else
            form_focus = (form_focus + 1) % focus_count;
        key_held_down = true;
        return;
    }
}

void WorldSelectTask::logicDelete()
{
    Task::textKeyPressed(); // nothing types here; keep the edges fresh

#ifndef _TINSPIRE
    SDL_PumpEvents();
    int mouse_x = 0, mouse_y = 0;
    const Uint8 buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    const bool left_down = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    const bool left_click = left_down && !left_mouse_was_down;
    left_mouse_was_down = left_down;

    const MenuUI::ConfirmLayout c = MenuUI::confirmLayout(delete_message_lines);

    if(left_click)
    {
        for(int i = 0; i < 2; ++i)
        {
            int x, y, w, h;
            c.buttonRect(i, x, y, w, h);
            if(mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h)
            {
                delete_focus = i;
                if(i == 0)
                    confirmDelete();
                else
                    view = LIST_VIEW;
                return;
            }
        }
    }
#endif

    if(key_held_down)
    {
        key_held_down = keyPressed(KEY_NSPIRE_ESC) || keyPressed(KEY_NSPIRE_ENTER)
            || keyPressed(KEY_NSPIRE_5) || keyPressed(KEY_NSPIRE_CLICK)
            || keyPressed(KEY_NSPIRE_MENU) || keyPressed(KEY_NSPIRE_LEFT)
            || keyPressed(KEY_NSPIRE_RIGHT) || keyPressed(KEY_NSPIRE_4)
            || keyPressed(KEY_NSPIRE_6);
        return;
    }

    if(keyPressed(KEY_NSPIRE_LEFT) || keyPressed(KEY_NSPIRE_4)
       || keyPressed(KEY_NSPIRE_RIGHT) || keyPressed(KEY_NSPIRE_6)
       || keyPressed(KEY_NSPIRE_MENU))
    {
        delete_focus = 1 - delete_focus;
        key_held_down = true;
        return;
    }

    if(keyPressed(KEY_NSPIRE_ENTER) || keyPressed(KEY_NSPIRE_5) || keyPressed(KEY_NSPIRE_CLICK))
    {
        key_held_down = true;
        if(delete_focus == 0)
            confirmDelete();
        else
        {
            GameAudio::uiClick();
            view = LIST_VIEW;
        }
        return;
    }

    if(keyPressed(KEY_NSPIRE_ESC))
    {
        view = LIST_VIEW;
        key_held_down = true;
    }
}
