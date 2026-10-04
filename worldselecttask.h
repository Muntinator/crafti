#ifndef WORLDSELECTTASK_H
#define WORLDSELECTTASK_H

#include "task.h"
#include "world.h"

/**
 * Vanilla's "Select World" screen (`WorldSelectionScreen`), as far as this
 * engine can stand it in. The title screen's "Singleplayer" opens it: a search
 * box under the "Select World" heading, the world list with its 32x32 icons and
 * vanilla's row geometry, and vanilla's own button block under the list -- the
 * two 150-wide buttons ("Play Selected World", "Create New World") and the four
 * 72-wide ones ("Edit", "Delete", "Re-Create", "Cancel").
 *
 * The screen has the three dialogs vanilla opens from it: the create dialog
 * (`CreateWorldScreen` -- the name field, the Game Mode and World Type buttons
 * with the mode's help lines), the edit dialog (the same name field, to rename),
 * and the delete question (`ConfirmScreen`). The geometry of every one of them
 * is MenuUI's, so the drawing and the hit tests cannot disagree about where a
 * button is.
 *
 * This engine keeps one save file rather than a saves folder, so the list is the
 * saved world (when there is one) plus the world kinds the engine can make, and
 * any world made this session; deleting a row takes it out of the list without
 * touching the file on disk. The rows, the selection box, the scrollbar, the
 * search filter and the double-click-to-play are vanilla's behaviour.
 */
class WorldSelectTask : public Task
{
public:
    /** The screen's faces: the list and the three dialogs it opens. */
    enum View
    {
        LIST_VIEW = 0,
        CREATE_VIEW,
        EDIT_VIEW,
        DELETE_VIEW
    };

    static constexpr int max_worlds = 8;
    static constexpr int max_name_len = 23;

    /** One row of the list: the name, the two detail lines, and what play does. */
    struct WorldEntry
    {
        char name[max_name_len + 1];
        char sub[48];  ///< vanilla's `levelId (date)` line
        char info[48]; ///< vanilla's version/mode line
        World::WorldType type;
        int game_mode; ///< what the world opens in: 0 survival, 1 creative
        bool saved;    ///< the world in the save file: playing keeps its state
        bool is_new;   ///< made this session, which the row tags "New!"
    };

    WorldSelectTask();
    virtual ~WorldSelectTask();

    virtual void makeCurrent() override;
    virtual void render() override;
    virtual void logic(GLFix dt) override;

    /** The worlds the list offers; the host test measures the rows' strings. */
    int worldCount() const { return world_count; }
    const WorldEntry &worldAt(int index) const { return worlds[index]; }

    /** Wraps the delete question's warning into `lines` of at most 2 rows. */
    static int wrapWarning(const char *name, char lines[][80], int max_lines);

private:
    /** Rebuilds the built-in rows around the ones made this session. */
    void rebuildWorlds();
    /** Fills a row's two detail lines from what the row is. */
    void rebuildDetails(WorldEntry &entry);

    /** The rows the search filter leaves, and the entry behind one of them. */
    int filteredCount() const;
    int entryIndex(int row) const;

    void renderList();
    void renderForm(bool create);
    void renderDelete();

    void logicList();
    void logicForm(bool create);
    void logicDelete();

    /** Runs one of the screen's six buttons. */
    void activateButton(int action);
    /** The form's confirm button: creates the world or saves the rename. */
    void confirmForm(bool create);
    /** Takes the row pending deletion out of the list and returns to it. */
    void confirmDelete();
    /** Opens the selected world the way vanilla's play button does. */
    void playSelected(bool re_create);
    /** The selected entry, or nullptr when the filter has left nothing. */
    WorldEntry *selectedWorld();

    /** One key press into a text field: a typed character or a backspace. */
    void editField(char *text, unsigned int capacity, char typed, bool &changed);

    View view = LIST_VIEW;

    WorldEntry worlds[max_worlds];
    int world_count = 0;

    char search[24];
    char name_field[max_name_len + 1];
    int game_mode = 0;
    int world_type_index = 0;

    int selected = 0;    ///< the selected row, in filtered order
    int scroll = 0;      ///< the first row inside the window
    bool typing = false; ///< the search box has the keyboard: characters filter
    int focus_button = -1; ///< which of the six buttons the keyboard is on, or -1
    int hover_row = -1;  ///< the row under the pointer, if any
    bool hover_icon = false; ///< and whether it is on the row's icon
    int form_focus = 0;  ///< name field, the option buttons, then the bottom row
    int delete_focus = 1; ///< starts on Cancel, so a stray key cannot delete
    int edit_target = -1;
    int delete_target = -1;

    char delete_message[2][80];
    int delete_message_lines = 0;

    /** The row last clicked and when, for vanilla's double-click-to-play. */
    int last_click_row = -1;
    unsigned long last_click_ms = 0;
};

extern WorldSelectTask worldselect_task;

#endif // WORLDSELECTTASK_H
