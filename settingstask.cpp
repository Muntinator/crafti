#include "settingstask.h"

#include "audio_manager.h"
#include "audio_output.h"
#include "font.h"
#include "texturetools.h"
#include "villagegen.h"
#include "worldclock.h"
#include "worldtask.h"

#include "textures/selection.h"

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
    settings.push_back({"Music volume", nullptr, 101, 45, 0, 10});
    settings.push_back({"Effects volume", nullptr, 101, 70, 0, 10});
    settings.push_back({"Ambience volume", nullptr, 101, 70, 0, 10});
    // Appended last so older save files keep loading (see the comment above).
    // GPIO4 audio takes over the interrupt vector, so it stays opt-in.
    settings.push_back({"GPIO4 audio", fastmode_values, 2, 0, 0, 1});
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

    background = newTexture(background_width, background_height, 0, false);
}

bool SettingsTask::isVolumeEntry(unsigned int entry) const
{
    return entry >= AUDIO_MASTER && entry < AUDIO_GPIO4;
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
    deleteTexture(background);
}

void SettingsTask::makeCurrent()
{
    if(!background_saved)
        saveBackground();

    settings[DISTANCE].current_value = world.fieldOfView();

    changed_something = false;

    Task::makeCurrent();
}

void SettingsTask::render()
{
    drawBackground();

    const unsigned int x = (SCREEN_WIDTH - background->width) / 2;
    unsigned int y = (SCREEN_HEIGHT - background->height) / 2;
    drawTextureOverlay(*background, 0, 0, *screen, x, y, background->width, background->height);
    drawString("Settings", 0xFFFF, *screen, x, y - fontHeight());

    y += 8;

    for(unsigned int i = 0; i < settings.size(); ++i)
    {
        SettingsEntry &e = settings[i];

        if(i == current_selection)
            drawTexture(selection, *screen, 0, 0, selection.width, selection.height, x + 5, y, selection.width, selection.height);

        drawString(e.name, 0xFFFF, *screen, x + selection.width + 10, y);

        if(!isVolumeEntry(i))
        {
            if(e.values == nullptr)
            {
                char number[10];
                snprintf(number, sizeof(number), "%u", e.current_value);
                drawString(number, 0xFFFF, *screen, x + 100, y);
            }
            else
                drawString(e.values[e.current_value], 0xFFFF, *screen, x + 100, y);
        }
        else
        {
            char volume_text[12];
            snprintf(volume_text, sizeof(volume_text), "%u%%", e.current_value);
            drawString(volume_text, 0xFFFF, *screen, x + 150, y);
        }

        y += fontHeight() + 5;
    }

}

void SettingsTask::logic(GLFix /*dt*/)
{
    if(key_held_down)
        key_held_down = keyPressed(KEY_NSPIRE_ESC) || keyPressed(KEY_NSPIRE_UP) || keyPressed(KEY_NSPIRE_DOWN) || keyPressed(KEY_NSPIRE_2) || keyPressed(KEY_NSPIRE_8) || keyPressed(KEY_NSPIRE_LEFT) || keyPressed(KEY_NSPIRE_4) || keyPressed(KEY_NSPIRE_RIGHT) || keyPressed(KEY_NSPIRE_6);
    else if(keyPressed(KEY_NSPIRE_ESC))
    {
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
    else if(keyPressed(KEY_NSPIRE_UP) || keyPressed(KEY_NSPIRE_8))
    {
        if(current_selection == 0)
            current_selection = settings.size() - 1;
        else
            --current_selection;

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_DOWN) || keyPressed(KEY_NSPIRE_2))
    {
        ++current_selection;
        if(current_selection >= settings.size())
            current_selection = 0;

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_LEFT) || keyPressed(KEY_NSPIRE_4))
    {
        SettingsEntry &entry = settings[current_selection];
        if(isVolumeEntry(current_selection))
        {
            entry.current_value = entry.current_value < entry.step ? 0 : entry.current_value - entry.step;
            applyAudioSettings();
            changed_something = true;
            key_held_down = true;
            return;
        }
        if(entry.current_value < entry.min_value + entry.step)
            entry.current_value = entry.values_count - 1;
        else
            entry.current_value -= entry.step;

        changed_something = true;

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_RIGHT) || keyPressed(KEY_NSPIRE_6))
    {
        SettingsEntry &entry = settings[current_selection];
        entry.current_value += entry.step;
        if(isVolumeEntry(current_selection))
        {
            if(entry.current_value > entry.values_count - 1)
                entry.current_value = entry.values_count - 1;
        }
        else if(entry.current_value >= entry.values_count)
            entry.current_value = entry.min_value;

        if(isVolumeEntry(current_selection) || current_selection == AUDIO_GPIO4)
            applyAudioSettings();
        if(current_selection == VILLAGE_FREQUENCY || current_selection == DAY_LENGTH)
            applyGameplaySettings();
        changed_something = true;

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

    if(settings[AUDIO_GPIO4].current_value != 0)
        GameAudioOutput::enableGpio4();
    else
        GameAudioOutput::disableGpio4();
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
