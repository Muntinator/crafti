#include "settingstask.h"

#include <cstdio>

#include "audio_manager.h"
#include "audio_output.h"
#include "menuui.h"
#include "villagegen.h"
#include "worldclock.h"
#include "worldtask.h"

#ifndef _TINSPIRE
#include <SDL/SDL.h>
#endif

SettingsTask settings_task;

const char *leaves_values[] = {
    "Opaque",
    "Transparent"
};

const char *speed_values[] = {
    "Slow",
    "Normal",
    "Fast"
};

const char *fastmode_values[] = {
    "Off",
    "On"
};

const char *world_static_values[] = {
    "Static (no ticks)",
    "Dynamic"
};

// Must stay in the order of Village::Frequency.
const char *village_values[] = {
    "Off",
    "Rare",
    "Normal",
    "Common"
};

const char *day_length_values[] = {
    "10 min",
    "20 min",
    "40 min"
};

// The audio output backends. Values 0 and 1 are the old "UART audio" toggle's
// off/on, so save files from before GPIO output load unchanged.
const char *audio_output_values[] = {
    "Off",
    "UART pin 4",
    "GPIO 22 (pin 18)",
    "GPIO 22 (buzzer)",
    // Dock pin 6: the same bit stream as GPIO 22, on USB Data+. Unplug any
    // cradle or host first -- with one attached that pin is the calculator's
    // USB data line, not a spare output.
    "USB D+ (pin 6)",
    "USB D+ (buzzer)"
};

// Must stay in the order of day_length_values.
const unsigned int day_length_seconds[] = { 10 * 60, 20 * 60, 40 * 60 };
constexpr unsigned int day_length_default = 1; // 20 minutes

SettingsTask::SettingsTask()
{
    //Must have the same order as the "Settings" enum
    //When changing something in an incompatible way (order, meaning),
    //increment savefile_version in task.cpp and add handling to SettingsTask::loadFromFile.
    //Not needed when just adding or removing entries at the end, except when adding after
    //removing entries in the past.
    settings.push_back({"Leaves", leaves_values, 2, 0, 0, 1});
    settings.push_back({"Speed", speed_values, 3, 1, 0, 1});
    settings.push_back({"Distance", nullptr, 10, 2, 1, 1});
    settings.push_back({"Fast mode", fastmode_values, 2, 0, 0, 1});
    settings.push_back({"Near plane", nullptr, 512+1, 160, 128, 16});
    settings.push_back({"World", world_static_values, 2, 1, 0, 1});
    settings.push_back({"Show FPS", fastmode_values, 2, 0, 0, 1});
    settings.push_back({"Block indicator", fastmode_values, 2, 0, 0, 1});
    settings.push_back({"Coord indicator", fastmode_values, 2, 0, 0, 1});
    settings.push_back({"Audio master", nullptr, 101, 100, 0, 10});
    // Vanilla's defaults: every slider at full except music, which sits at 60.
    // The old 45/70/70 came out of the engine's own tuning and left the dock
    // output about 3 dB under the rail for most material.
    settings.push_back({"Music volume", nullptr, 101, 60, 0, 10});
    settings.push_back({"Effects volume", nullptr, 101, 100, 0, 10});
    settings.push_back({"Ambience volume", nullptr, 101, 100, 0, 10});
    // Appended last so older save files keep loading (see the comment above).
    // Both outputs take over an interrupt vector, so both stay opt-in. GPIO 22
    // exists because this calculator's dock pin 4 (UART Tx) is broken; USB D+ is
    // the same bit stream on dock pin 6.
    settings.push_back({"Audio output", audio_output_values, 6, 0, 0, 1});
    // How often the infinite world places a village. Only chunks generated
    // after a change pick up the new value; already loaded terrain keeps the
    // village it was generated with.
    settings.push_back({"Villages", village_values, 4, 2, 0, 1});
    // Day/night can be turned off entirely: that keeps the sky at its daytime
    // colour and skips the per-texel light tint, which is the cheapest way to
    // buy back frame time on a slow CX.
    settings.push_back({"Day/night", fastmode_values, 2, 1, 0, 1});
    // How long one in-game day lasts in real time. Only affects how fast the
    // clock runs, never the save file.
    settings.push_back({"Day length", day_length_values, 3, day_length_default, 0, 1});
    // Rain and thunderstorms. They are derived from the day/night clock, so this
    // only applies while that cycle is on.
    settings.push_back({"Weather", fastmode_values, 2, 1, 0, 1});
    // The front-end's own scale, added by the vanilla port. Appended last so older
    // save files keep loading; "Auto" is the largest scale the screen can hold.
    settings.push_back({"GUI scale", MenuUI::guiScaleValues,
                        static_cast<unsigned int>(MenuUI::guiScaleValueCount), 0, 0, 1});
}

SettingsTask::RowKind SettingsTask::rowKind(unsigned int entry) const
{
    const SettingsEntry &e = settings[entry];
    if(e.values == nullptr)
        return RowKind::Slider;
    if(e.values == fastmode_values)
        return RowKind::Toggle;
    return RowKind::Cycle;
}

bool SettingsTask::isToggleEntry(unsigned int entry) const
{
    return settings[entry].values == fastmode_values;
}

bool SettingsTask::isVolumeEntry(unsigned int entry) const
{
    return entry >= AUDIO_MASTER && entry < AUDIO_OUTPUT;
}

bool SettingsTask::isAudioEntry(unsigned int entry) const
{
    return entry >= AUDIO_MASTER && entry <= AUDIO_OUTPUT;
}

void SettingsTask::formatValue(unsigned int entry, char *out, unsigned int size) const
{
    const SettingsEntry &e = settings[entry];

    if(isVolumeEntry(entry))
    {
        snprintf(out, size, "%u%%", e.current_value);
        return;
    }

    if(e.values == nullptr)
    {
        snprintf(out, size, "%u", e.current_value);
        return;
    }

    if(e.current_value < e.values_count)
        snprintf(out, size, "%s", e.values[e.current_value]);
    else
        out[0] = '\0';
}

void SettingsTask::applyGameplaySettings()
{
    Village::setFrequency(static_cast<int>(settings[VILLAGE_FREQUENCY].current_value));

    unsigned int index = settings[DAY_LENGTH].current_value;
    if(index >= sizeof(day_length_seconds) / sizeof(day_length_seconds[0]))
        index = day_length_default;
    WorldClock::setDayLengthSeconds(day_length_seconds[index]);
}

SettingsTask::~SettingsTask()
{
}

void SettingsTask::makeCurrent()
{
    settings[DISTANCE].current_value = world.fieldOfView();

    // Show the scale the front-end is actually running at, so the row is truthful
    // even after an "Auto" choice.
    settings[GUI_SCALE].current_value = static_cast<unsigned int>(MenuUI::guiScale());

    current_selection = 0;
    scroll = 0;
    changed_something = false;

    // The point the pointer already sits at is not a move, so opening the screen
    // does not let a resting pointer steal the focus from the keyboard -- and on
    // the calculator the press that opened it cannot take a row of its own.
    Pointer::seed();
    Task::makeCurrent();
}

void SettingsTask::drawEntry(unsigned int entry, int x, int y, int w, int h)
{
    SettingsEntry &e = settings[entry];
    const bool focused = (current_selection == entry);
    const RowKind kind = rowKind(entry);

    char value[16];
    formatValue(entry, value, sizeof(value));

    char label[64];
    MenuUI::formatOptionLabel(label, sizeof(label), e.name, value);

    if(kind == RowKind::Slider)
    {
        // Vanilla's slider is a button whose label sits on the track and whose
        // handle marks the value.
        MenuUI::drawSlider(*screen, x, y, w, h, e.current_value, e.min_value,
                           e.values_count - 1, focused);
        MenuUI::drawButtonLabel(label, *screen, x, y, w, h, focused);
        return;
    }

    MenuUI::drawButton(*screen, x, y, w, h, focused);

    if(kind == RowKind::Toggle)
    {
        // Vanilla words an on/off option as "Name: On"; the checkbox at the right
        // is the same state drawn as a widget.
        const int scale = MenuUI::uiScale();
        const int box = 20 * scale;
        const int gap = 4 * scale;
        MenuUI::drawCheckbox(*screen, x + w - box - gap, y + (h - box) / 2, box,
                             e.current_value != 0, focused);
        MenuUI::drawButtonLabel(label, *screen, x, y, w - box - gap, h, focused);
        return;
    }

    // A list row: the label carries the current value and left/right cycles it.
    MenuUI::drawButtonLabel(label, *screen, x, y, w, h, focused);
}

void SettingsTask::render()
{
    // Vanilla's options screen is the dirt backdrop, a title, and a two-column
    // grid of widgets with a wide "Done" under it.
    MenuUI::drawMenuBackground(*screen);

    const int total = static_cast<int>(settings.size());
    MenuUI::OptionsLayout layout = MenuUI::optionsLayout(total, scroll);
    scroll = layout.first_visible;

    MenuUI::drawHeading(MenuUI::optionsHeading, *screen, layout.title_y);

    // Rows are stored top to bottom, so once one is off the top there is nothing
    // below it to draw either.
    for(int row = layout.first_visible; row < layout.rows && layout.rowVisible(row); ++row)
    {
        const int y = layout.rowY(row);
        for(int col = 0; col < 2; ++col)
        {
            const int index = row * 2 + col;
            if(index >= total)
                break;
            drawEntry(static_cast<unsigned int>(index), layout.columnX(col), y,
                      layout.button_w, layout.button_h);
        }
    }

    const bool done_focused = (current_selection >= settings.size());
    MenuUI::drawButton(*screen, layout.done_x, layout.done_y, layout.done_w, layout.done_h,
                       done_focused);
    MenuUI::drawButtonLabel(MenuUI::optionsDoneLabel, *screen, layout.done_x, layout.done_y,
                            layout.done_w, layout.done_h, done_focused);
}

void SettingsTask::moveSelection(int delta)
{
    const int total = static_cast<int>(settings.size());
    int selection = static_cast<int>(current_selection) + delta;

    if(selection < 0)
        selection = total; // above the first entry is the "Done" button
    if(selection > total)
        selection = 0;
    current_selection = static_cast<unsigned int>(selection);

    // The "Done" button is not part of the grid, so selecting it leaves the scroll
    // alone.
    if(current_selection >= settings.size())
        return;

    const MenuUI::OptionsLayout layout = MenuUI::optionsLayout(total, scroll);
    scroll = MenuUI::optionsScrollFor(static_cast<int>(current_selection) / 2,
                                      layout.visible_rows, layout.rows, scroll);
}

void SettingsTask::changeValue(int delta)
{
    if(current_selection >= settings.size())
        return; // "Done" has no value

    SettingsEntry &e = settings[current_selection];

    if(isToggleEntry(current_selection))
    {
        // Left/right flips an on/off row as well, which is what a keyboard user
        // expects from a checkbox.
        e.current_value = e.current_value == 0 ? 1 : 0;
    }
    else
    {
        const int step = e.step > 0 ? static_cast<int>(e.step) : 1;
        const int min = static_cast<int>(e.min_value);
        const int max = static_cast<int>(e.values_count) - 1;

        int value = static_cast<int>(e.current_value) + delta * step;
        if(value < min)
            value = max;
        if(value > max)
            value = min;
        e.current_value = static_cast<unsigned int>(value);
    }

    changed_something = true;

    if(isAudioEntry(current_selection))
        applyAudioSettings();
    if(current_selection == VILLAGE_FREQUENCY || current_selection == DAY_LENGTH)
        applyGameplaySettings();
    if(current_selection == GUI_SCALE)
        MenuUI::setGuiScale(static_cast<int>(settings[GUI_SCALE].current_value));
}

void SettingsTask::activate()
{
    if(current_selection >= settings.size())
    {
        leave();
        return;
    }

    SettingsEntry &e = settings[current_selection];
    const RowKind kind = rowKind(current_selection);

    if(kind == RowKind::Slider)
        return; // a slider changes with left/right only

    if(kind == RowKind::Toggle)
        e.current_value = e.current_value == 0 ? 1 : 0;
    else
        e.current_value = (e.current_value + 1) % e.values_count;

    changed_something = true;

    if(isAudioEntry(current_selection))
        applyAudioSettings();
    if(current_selection == VILLAGE_FREQUENCY || current_selection == DAY_LENGTH)
        applyGameplaySettings();
    if(current_selection == GUI_SCALE)
        MenuUI::setGuiScale(static_cast<int>(settings[GUI_SCALE].current_value));
}

void SettingsTask::openFrom(Task *from)
{
    return_task = from;
    makeCurrent();
}

void SettingsTask::leave()
{
    Task *back = return_task;
    return_task = nullptr;

    if(back != nullptr)
        back->makeCurrent();
    else
        world_task.makeCurrent();

    if(changed_something)
    {
        world.setDirty();
        world.setFieldOfView(settings[DISTANCE].current_value);

        nglSetNearPlane(settings[NEARPLANE_Z].current_value);
    }

    applyAudioSettings();
    applyGameplaySettings();

    key_held_down = true;
}

void SettingsTask::setValueFromX(unsigned int entry, int mouse_x, int box_x, int box_w)
{
    SettingsEntry &e = settings[entry];
    const int max = static_cast<int>(e.values_count) - 1;
    const int min = static_cast<int>(e.min_value);
    if(max <= min || box_w <= 0)
        return;

    int value = min + (max - min) * (mouse_x - box_x) / box_w;

    // Sliders move in their own step (the near plane steps by 16, the volumes by
    // 10), so the value snaps to the nearest step the arrow keys would reach.
    if(e.step > 0)
    {
        const int step = static_cast<int>(e.step);
        value = min + ((value - min + step / 2) / step) * step;
    }

    if(value < min)
        value = min;
    if(value > max)
        value = max;
    if(static_cast<unsigned int>(value) == e.current_value)
        return;

    e.current_value = static_cast<unsigned int>(value);
    changed_something = true;

    if(isAudioEntry(entry))
        applyAudioSettings();
    if(entry == VILLAGE_FREQUENCY || entry == DAY_LENGTH)
        applyGameplaySettings();
    if(entry == GUI_SCALE)
        MenuUI::setGuiScale(static_cast<int>(settings[GUI_SCALE].current_value));
}

void SettingsTask::logic(GLFix /*dt*/)
{
    // Vanilla's pointer, on both machines: hovering a row lights it and a click
    // takes it, exactly as the pause menu does. On the calculator the pointer is
    // the touchpad's virtual cursor -- which is what makes the audio rows
    // reachable at all, since every other way into this screen is a key.
    Pointer::poll();
    const int mouse_x = Pointer::x(), mouse_y = Pointer::y();
    const bool left_down = Pointer::down();

    const int total = static_cast<int>(settings.size());
    const MenuUI::OptionsLayout layout = MenuUI::optionsLayout(total, scroll);

    int hovered = -1;
    for(int row = layout.first_visible; row < layout.rows && layout.rowVisible(row); ++row)
    {
        const int y = layout.rowY(row);
        for(int col = 0; col < 2; ++col)
        {
            const int index = row * 2 + col;
            if(index >= total)
                break;
            const int x = layout.columnX(col);
            if(mouse_x >= x && mouse_x < x + layout.button_w
                && mouse_y >= y && mouse_y < y + layout.button_h)
                hovered = index;
        }
    }
    if(mouse_x >= layout.done_x && mouse_x < layout.done_x + layout.done_w
        && mouse_y >= layout.done_y && mouse_y < layout.done_y + layout.done_h)
        hovered = total;

    if(Pointer::moved() && hovered >= 0)
        current_selection = static_cast<unsigned int>(hovered);

    if(Pointer::clicked() && hovered >= 0)
    {
        current_selection = static_cast<unsigned int>(hovered);

        if(hovered >= total)
            leave();
        else if(rowKind(hovered) == RowKind::Slider)
            setValueFromX(static_cast<unsigned int>(hovered), mouse_x,
                          layout.columnX(hovered % 2), layout.button_w);
        else
            activate();
        return;
    }

    // Holding the button on a slider drags the handle, which is vanilla's own
    // behaviour (a slider responds to a drag, not just a single click).
    if(left_down && hovered >= 0 && hovered < total && rowKind(hovered) == RowKind::Slider)
        setValueFromX(static_cast<unsigned int>(hovered), mouse_x,
                      layout.columnX(hovered % 2), layout.button_w);

    if(key_held_down)
        key_held_down = keyPressed(KEY_NSPIRE_ESC) || keyPressed(KEY_NSPIRE_UP) || keyPressed(KEY_NSPIRE_DOWN)
            || keyPressed(KEY_NSPIRE_2) || keyPressed(KEY_NSPIRE_8) || keyPressed(KEY_NSPIRE_LEFT)
            || keyPressed(KEY_NSPIRE_4) || keyPressed(KEY_NSPIRE_RIGHT) || keyPressed(KEY_NSPIRE_6)
            || keyPressed(KEY_NSPIRE_5) || keyPressed(KEY_NSPIRE_ENTER) || keyPressed(KEY_NSPIRE_CLICK);
    else if(keyPressed(KEY_NSPIRE_ESC))
        leave();
    else if(keyPressed(KEY_NSPIRE_UP) || keyPressed(KEY_NSPIRE_8))
    {
        moveSelection(-1);
        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_DOWN) || keyPressed(KEY_NSPIRE_2))
    {
        moveSelection(+1);
        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_LEFT) || keyPressed(KEY_NSPIRE_4))
    {
        changeValue(-1);
        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_RIGHT) || keyPressed(KEY_NSPIRE_6))
    {
        changeValue(+1);
        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_5) || keyPressed(KEY_NSPIRE_ENTER) || keyPressed(KEY_NSPIRE_CLICK))
    {
        activate();
        key_held_down = true;
    }
}

void SettingsTask::applyAudioSettings()
{
    const unsigned int effects = settings[AUDIO_EFFECTS].current_value;

    GameAudio::setMasterVolume(settings[AUDIO_MASTER].current_value);
    GameAudio::setCategoryVolume(GameAudio::CategoryMusic, settings[AUDIO_MUSIC].current_value);
    GameAudio::setCategoryVolume(GameAudio::CategoryUI, effects);
    GameAudio::setCategoryVolume(GameAudio::CategoryPlayer, effects);
    GameAudio::setCategoryVolume(GameAudio::CategoryBlocks, effects);
    GameAudio::setCategoryVolume(GameAudio::CategoryFootsteps, effects);
    GameAudio::setCategoryVolume(GameAudio::CategoryMobs, effects);
    GameAudio::setCategoryVolume(GameAudio::CategoryCombat, effects);

    const unsigned int ambience = settings[AUDIO_AMBIENCE].current_value;
    GameAudio::setCategoryVolume(GameAudio::CategoryAmbience, ambience);
    GameAudio::setCategoryVolume(GameAudio::CategoryWeather, ambience);

    switch(settings[AUDIO_OUTPUT].current_value)
    {
    case 1:
        GameAudioOutput::enableUartTx();
        break;
    case 2:
        GameAudioOutput::enableGpio(GameAudioOutput::GpioLineDock18, false);
        break;
    case 3: // the square-wave drive for a piezoelectric buzzer on the pin
        GameAudioOutput::enableGpio(GameAudioOutput::GpioLineDock18, true);
        break;
    case 4: // the same bit stream, out of USB Data+ on dock pin 6
        GameAudioOutput::enableGpio(GameAudioOutput::GpioLineUsbDataPlus, false);
        break;
    case 5: // ...and into a buzzer wired straight to it
        GameAudioOutput::enableGpio(GameAudioOutput::GpioLineUsbDataPlus, true);
        break;
    default:
        GameAudioOutput::disableUartTx();
        GameAudioOutput::disableGpio();
        break;
    }
}

unsigned int SettingsTask::getValue(unsigned int entry) const
{
    return settings[entry].current_value;
}

bool SettingsTask::loadFromFile(gzFile file, int version)
{
    // If some setting wasn't loaded, it keeps the current value.

    // Previous versions didn't have settings yet
    if(version < 5)
        return true;

    //World doesn't care about DISTANCE being saved and loaded here as well

    unsigned int entries_in_file;
    if(gzfread(&entries_in_file, sizeof(entries_in_file), 1, file) != 1)
        return false;

    for(unsigned int i = 0; i < entries_in_file; ++i)
    {
        unsigned int value;
        if(gzfread(&value, sizeof(unsigned int), 1, file) != 1)
            return false;

        if(i < settings.size()
            && value >= settings[i].min_value
            && value < settings[i].values_count)
            settings[i].current_value = value;
    }

    // The GUI scale is part of the saved settings; an older file that has no such
    // entry leaves it at "Auto", which is the default.
    MenuUI::setGuiScale(static_cast<int>(settings[GUI_SCALE].current_value));

    applyAudioSettings();
    applyGameplaySettings();

    world.setDirty();

    nglSetNearPlane(settings[NEARPLANE_Z].current_value);

    return true;
}

bool SettingsTask::saveToFile(gzFile file)
{
    unsigned int size = settings.size();
    if(gzfwrite(&size, sizeof(size), 1, file) != 1)
        return false;

    for(unsigned int i = 0; i < size; ++i)
    {
        if(gzfwrite(&settings[i].current_value, sizeof(unsigned int), 1, file) != 1)
            return false;
    }

    return true;
}
