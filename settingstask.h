#ifndef SETTINGSTASK_H
#define SETTINGSTASK_H

#include "task.h"

#include <vector>
#include <zlib.h>

class SettingsTask : public Task
{
public:
    struct SettingsEntry {
        const char *name;
        const char *const *values; //If nullptr, the numeric value is used
        unsigned int values_count;
        unsigned int current_value;
        unsigned int min_value; //Only makes sense if values == nullptr
        unsigned int step;
    };

    enum Settings {
        LEAVES = 0,
        SPEED,
        DISTANCE, //Managed by World, but can be changed here as well
        FAST_MODE,
        NEARPLANE_Z,
        TICKS_ENABLED,
        SHOW_FPS,
        BLOCK_INDICATOR,
        COORD_INDICATOR,
        AUDIO_MASTER,
        AUDIO_MUSIC,
        AUDIO_EFFECTS,
        AUDIO_AMBIENCE,
        AUDIO_GPIO4,
        VILLAGE_FREQUENCY,
        DAY_NIGHT,
        DAY_LENGTH,
        WEATHER,
        // The front-end's own scale. Appended last so older save files keep
        // loading (see the comment in the constructor).
        GUI_SCALE,
    };

    SettingsTask();
    virtual ~SettingsTask();

    virtual void makeCurrent() override;

    /**
     * Opens the screen from `from`, so leaving it hands control back there. The
     * pause menu opens it as a child screen, the way vanilla's "Options..." opens
     * the options screen with the pause menu underneath: "Done" (or Esc) comes
     * back to the pause menu, not straight into the game.
     */
    void openFrom(Task *from);

    virtual void render() override;
    virtual void logic(GLFix dt) override;

    unsigned int getValue(unsigned int entry) const;

    bool loadFromFile(gzFile file, int version);
    bool saveToFile(gzFile file);

private:
    /**
     * How a row is drawn. Vanilla uses a slider for a number, a small checkbox for
     * an on/off option and a plain button that cycles through its named values for
     * everything else; which one applies is decided by the entry's own shape.
     */
    enum class RowKind { Slider, Toggle, Cycle };

    RowKind rowKind(unsigned int entry) const;
    /** True for the on/off entries, which carry the shared `fastmode_values`. */
    bool isToggleEntry(unsigned int entry) const;
    /** Volume rows are shown as percentages. */
    bool isVolumeEntry(unsigned int entry) const;
    /** Every row from the master volume down to the GPIO4 switch is audio. */
    bool isAudioEntry(unsigned int entry) const;

    /** The value a slider row carries: its number, or its percentage for volumes. */
    void formatValue(unsigned int entry, char *out, unsigned int size) const;

    void moveSelection(int delta);
    void changeValue(int delta);
    /** The action key: toggles an on/off row, cycles a list, closes the screen. */
    void activate();
    /** Commits the settings and hands the screen back where it was opened from. */
    void leave();

    void drawEntry(unsigned int entry, int x, int y, int w, int h);

    void applyAudioSettings();
    void applyGameplaySettings();

    /** Where leaving goes: the screen that opened this one, or the world. */
    Task *return_task = nullptr;

    std::vector<SettingsEntry> settings;
    unsigned int current_selection = 0;
    /** The top row of the grid that is on screen; see MenuUI::optionsLayout(). */
    int scroll = 0;
    bool changed_something;
};

extern SettingsTask settings_task;

#endif // SETTINGSTASK_H
