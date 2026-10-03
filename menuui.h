#ifndef MENUUI_H
#define MENUUI_H

#include "gl.h"

/**
 * The look of the front-end screens: the title screen and the pause menu draw the
 * same widgets as each other, and they are meant to read as Minecraft's.
 *
 * Everything here is drawing, not state: a caller says what to paint and where,
 * and the numbers that make a menu look like Minecraft's live in one place rather
 * than being copied into each screen. The textures are the official 1.17.1 ones
 * (the widget sheet's button in its three states, the dirt backdrop, and the
 * wordmark itself); what is left as colour is the text and the splash.
 *
 * The geometry is vanilla's, at the GUI scale the engine runs at: a button is
 * 200x20 with a 4-pixel gap, the title screen's first button is a quarter of the
 * way down plus 48 pixels, and the wordmark is the image `title/minecraft.png`
 * rather than a wordmark drawn out of the text font. `uiScale()` is 1 on the
 * calculator's 320x240 and 2 on the desktop's 640x480, so every number below is
 * stated at scale 1 and multiplied on the way out.
 *
 * Colours are the engine's own RGB565, built by rgb() so that the values read as
 * the colours they are.
 */
namespace MenuUI
{
    /** 8-bit channels to the engine's 565 pixel. */
    constexpr COLOR rgb(int r, int g, int b)
    {
        return static_cast<COLOR>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }

    // The front-end's palette. The buttons and the background are the official
    // textures, so only what is drawn as colour is here.
    constexpr COLOR Text = rgb(255, 255, 255);
    constexpr COLOR TextShadow = rgb(63, 63, 63);
    constexpr COLOR TextDisabled = rgb(160, 160, 160);
    /** Vanilla's splash is pure yellow, 0xFFFF00. */
    constexpr COLOR Splash = rgb(255, 255, 0);
    /**
     * The shadow vanilla gives the splash: `(0xFFFF00 & 0xFCFCFC) >> 2`, the
     * dark-yellow the font derives for a shadowed line. The button labels and the
     * headings use the white text's shadow, 0x3F3F3F, which is TextShadow.
     */
    constexpr COLOR SplashShadow = rgb(63, 63, 0);
    /** The grey detail lines of a world row, vanilla's 0x808080. */
    constexpr COLOR EntrySub = rgb(128, 128, 128);
    /** The edit box's frame, vanilla's 0xA0A0A0, and the label grey beside it. */
    constexpr COLOR EditBoxBorder = rgb(160, 160, 160);
    /** The placeholder text inside an empty edit box, vanilla's 0x7F7F7F. */
    constexpr COLOR EditBoxHint = rgb(127, 127, 127);
    constexpr COLOR Black = rgb(0, 0, 0);
    /** The world list's scrollbar: a black track and this thumb. */
    constexpr COLOR ScrollbarThumb = rgb(128, 128, 128);
    /** The one-pixel lit edge vanilla gives the scrollbar thumb. */
    constexpr COLOR ScrollbarEdge = rgb(192, 192, 192);

    // Vanilla's own numbers, at GUI scale 1. The title screen is laid out from
    // these, which is what makes it match the real one rather than merely
    // resemble it.
    constexpr int ButtonWidth = 200;   ///< the widget sheet's button is 200 wide
    constexpr int ButtonHeight = 20;
    constexpr int ButtonPitch = 24;    ///< height + the 4-pixel gap between buttons
    constexpr int TitleLogoTop = 30;   ///< where the wordmark's top edge sits
    constexpr int TitleButtonOffset = 48;  ///< first button = SCREEN_HEIGHT/4 + this
    /**
     * The title screen's own row break, which is vanilla's: the first three
     * buttons are full width, and the row `height / 4 + 48 + 72 + 12` down holds
     * the half-width "Options..." and "Quit Game" flanked by vanilla's two 20x20
     * icon buttons -- the language globe at `width / 2 - 124` and the
     * accessibility figure at `width / 2 + 104`. Those two open screens this
     * engine does not have, so they are drawn in vanilla's greyed state: present
     * and in their vanilla places, but not choosable.
     */
    constexpr int TitleFullButtons = 3;
    constexpr int TitleHalfWidth = 98;     ///< the widget sheet's half button
    constexpr int TitleBottomOffset = 72 + 12;
    constexpr int TitleIconSize = 20;      ///< vanilla's icon button is 20x20
    constexpr int TitleIconLeftOffset = -124;  ///< language = width / 2 + this
    constexpr int TitleIconRightOffset = 104;  ///< accessibility = width / 2 + this
    /**
     * The title buttons' order, which is vanilla's widget order: the three
     * full-width text buttons, the two half-width text buttons, then the two icon
     * buttons. The label table has one entry per slot -- the icon slots carry
     * empty strings -- so drawing and hit-testing can walk one list.
     */
    constexpr int TitleLeftHalfButton = TitleFullButtons;
    constexpr int TitleRightHalfButton = TitleFullButtons + 1;
    constexpr int TitleLeftIconButton = TitleFullButtons + 2;
    constexpr int TitleRightIconButton = TitleFullButtons + 3;
    constexpr int TitleButtonCount = TitleFullButtons + 4;
    /**
     * The title wordmark is drawn at `width / 2 - 137` -- vanilla's own x, where
     * the 274-pixel image (two 155-pixel halves joined) comes out centred -- and
     * the `Java Edition` strip at `width / 2 - 49`, 67 pixels down, over the lower
     * band of the wordmark where vanilla keeps it. The splash is centred at
     * `width / 2 + 90` and tilted 20 degrees, with its text centre 66 pixels down:
     * vanilla translates to y 70 and then draws the line with its top at local
     * y -8, so the 8-pixel line occupies 62..70 and is centred on 66.
     */
    constexpr int TitleEditionTop = 67;
    constexpr int TitleSplashOffsetX = 90;
    constexpr int TitleSplashCentreY = 66;
    constexpr int TitleSmallPrintBottom = 10; ///< both bottom lines sit this far up
    constexpr int HeadingTop = 15;         ///< where a screen's title sits, at scale 1

    /**
     * The pause screen's own grid, which is vanilla's `PauseScreen`:
     * `width / 2 - 102` wide (204) at the top, then three rows of two 98-pixel
     * buttons at `width / 2 - 102` and `width / 2 + 4`, then another 204-wide
     * button -- every one 20 high, on a 24-pixel pitch from `height / 4 + 8`.
     * Vanilla heads it "Game Menu", centred at y 40.
     */
    constexpr int PauseHeadingY = 40;
    constexpr int PauseButtonTop = 8;      ///< first button = SCREEN_HEIGHT / 4 + this
    constexpr int PauseWideWidth = 204;
    constexpr int PauseHalfWidth = 98;
    constexpr int PauseLeftOffset = -102;  ///< left column = width / 2 + this
    constexpr int PauseRightOffset = 4;    ///< right column = width / 2 + this
    constexpr int PauseRowPitch = 24;
    constexpr int PauseGridRows = 3;       ///< the two-column rows between the wide ones

    // The options screen's grid, which is vanilla's: two 150-pixel columns of the
    // same 20-pixel widgets, at the same 24-pixel pitch, starting from
    // `height / 6 - 12` and with a 200-pixel "Done" under them. A 320-pixel screen
    // cannot show all of the settings at once, so the screen scrolls the grid and
    // keeps the selection in view.
    constexpr int OptionsButtonWidth = 150;
    constexpr int OptionsLeftOffset = 155;  ///< left column = width/2 - this
    constexpr int OptionsRightOffset = 5;   ///< right column = width/2 + this
    constexpr int OptionsTopOffset = -12;   ///< first row = height/6 + this
    constexpr int OptionsDoneWidth = 200;   ///< vanilla's "Done" is a wide button

    // The loading screen's own numbers: a centred label with a progress bar under
    // it, on the same dirt every menu is tiled with. Vanilla draws this while a
    // world is loaded; the label is its own string and the bar is drawn as colour
    // because this engine has no `bars.png` crop.
    constexpr int LoadingBarWidth = 200;   ///< the bar's width at scale 1
    constexpr int LoadingBarHeight = 6;    ///< and its height
    constexpr int LoadingBarBorder = 1;    ///< the frame around it, at scale 1
    constexpr COLOR LoadingBarFrame = rgb(160, 160, 160);
    constexpr COLOR LoadingBarTrack = rgb(30, 30, 30);
    constexpr COLOR LoadingBarFill = rgb(255, 255, 255);

    /**
     * Vanilla draws the splash at `180% * 100 / (width + 32)` -- a scale that makes
     * any line about the same size on the screen -- and pulses it by up to ten
     * percent (`1.8 - |sin| * 0.1`). A narrow screen cannot hold that where vanilla
     * puts it, so the title screen slides the anchor left until the line fits; the
     * offset above is still the one vanilla asks for whenever there is room for it.
     */
    constexpr int SplashPulse = 10;

    /** A filled rectangle, clipped to the texture. drawRectangle() only outlines. */
    void fillRect(TEXTURE &tex, int x, int y, int w, int h, COLOR color);

    /**
     * Dims a rectangle in place by about a third, which is what Minecraft does to
     * the dirt behind its menus so that the text on top stays readable.
     */
    void shadeRect(TEXTURE &tex, int x, int y, int w, int h, int keep_percent);

    /**
     * A Minecraft button: the widget sheet's 200x20 button, picked from the row
     * for its state (plain, highlighted or greyed out) and stretched into the box,
     * which is exactly how vanilla draws one. A disabled button goes grey, the way
     * vanilla greys out an option that cannot be chosen (there is no world to
     * continue yet, for instance).
     */
    void drawButton(TEXTURE &tex, int x, int y, int w, int h, bool focused, bool enabled = true);

    /**
     * A centred label in the vanilla style: white with a dark shadow one pixel
     * down and right, drawn inside `y`..`y + h` of a button.
     */
    void drawButtonLabel(const char *text, TEXTURE &tex, int x, int y, int w, int h,
                         bool focused, bool enabled = true);

    /**
     * A vanilla icon button: the ordinary button art with a 15x15 icon centred
     * on it, which is how vanilla draws its language and accessibility buttons.
     * The icon is magnified with the GUI scale, leaving the button's own border
     * around it at every scale.
     */    void drawIconButton(const TEXTURE &icon, TEXTURE &tex, int x, int y, int w, int h,
                       bool focused, bool enabled = true);

    /**
     * Vanilla's edit box: a black field inside a one-pixel grey frame, with the
     * text four pixels in (or the grey placeholder when it is empty) and the
     * caret at the end of the text while the box holds the focus.
     */
    void drawEditBox(TEXTURE &tex, int x, int y, int w, int h,
                     const char *text, const char *hint, bool focused);

    /**
     * One world row's text: the name in white over the two grey detail lines,
     * beside the row's icon at (x, y). Vanilla draws these with the font's
     * plain draw -- no shadow -- at the row's own 1/12/21 pixel offsets.
     */
    void drawWorldEntry(const char *name, const char *sub, const char *info,
                        TEXTURE &tex, int x, int y);

    /**
     * The yellow splash line with its outline, its top-left at (x, y). It is drawn
     * into a texture of its own by the title screen so that the whole line can be
     * rotated when it is put on the screen.
     */
    void drawSplash(const char *text, TEXTURE &tex, int x, int y);

    /** A left-aligned line of the vanilla small print (the version, the credits). */
    void drawSmallPrint(const char *text, TEXTURE &tex, int x, int y);

    /** A screen's title, centred with a shadow, the way vanilla heads its menus. */
    void drawHeading(const char *text, TEXTURE &tex, int y);

    /**
     * A screen's title at a whole-number magnification, which is how vanilla
     * doubles the death screen's "You Died!". The shadow is drawn before the
     * text at both sizes, so the two stay a pixel apart at every scale.
     */
    void drawHeadingScaled(const char *text, TEXTURE &tex, int y, int scale);

    /** Where drawHeading() puts a screen's title (vanilla's y 15, at the scale). */
    int headingY();

    /**
     * A vanilla slider: the button art as its track and the widget sheet's handle
     * at the value's position. `value` is clamped into `min`..`max`, which is the
     * whole of what the widget needs to know.
     */
    void drawSlider(TEXTURE &tex, int x, int y, int w, int h,
                    unsigned int value, unsigned int min, unsigned int max,
                    bool focused, bool enabled = true);

    /**
     * A vanilla checkbox: the official 2x2 checkbox sheet, where the column picks
     * unchecked or ticked and the row picks plain or highlighted. `size` is the
     * box's side, which the sheet's own 20 pixels are stretched to.
     */
    void drawCheckbox(TEXTURE &tex, int x, int y, int size, bool checked, bool focused,
                      bool enabled = true);

    /**
     * Builds the label a row carries -- vanilla's "Name: Value" -- so the options
     * screen and its test agree on the string that has to fit the row.
     */
    void formatOptionLabel(char *out, unsigned int size, const char *name, const char *value);

    /** The scale the front-end should use on this screen (1 on the CX, 2 on a desktop). */
    int uiScale();
    /** The largest scale that fits this screen, which is what "Auto" means. */
    int maxUiScale();
    /** The player's GUI-scale choice: 0 is vanilla's Auto. */
    int guiScale();
    void setGuiScale(int scale);

    /** The title screen's wordmark, as it appears in `title/minecraft.png`. */
    void logoScreenSize(int &w, int &h);
    /** The "Java Edition" strip, as it appears in `title/edition.png`. */
    void editionScreenSize(int &w, int &h);

    // ------------------------------------------------------------ the screens

    /**
     * A centred column of buttons, which is the shape every vanilla menu has. The
     * geometry lives here rather than in each task so that the labels and the
     * box they have to fit in are decided in the same place -- and so that a host
     * test can check that they do fit, on both screen sizes.
     */
    struct ButtonColumn
    {
        int x = 0;
        int w = 0;
        int h = 0;
        int gap = 0;
        int top = 0;
        int count = 0;

        int buttonY(int index) const { return top + index * (h + gap); }
        int bottom() const { return count > 0 ? buttonY(count - 1) + h : top; }
    };

    /** The height a column of `count` vanilla buttons takes up. */
    int buttonBlockHeight(int count);

    /** A column whose first button is at `top`. */
    ButtonColumn buttonColumnAt(int count, int top);

    /** A column centred on `centre_y`. */
    ButtonColumn centeredButtonColumn(int count, int centre_y);

    // ------------------------------------------------- the world-select screen

    /**
     * Vanilla's `WorldSelectionScreen`, at GUI scale 1: a "Select World" heading
     * at y 8, a 200-wide search box at y 22, the world list from y 48 down to
     * `height - 64`, and two rows of buttons at `height - 52` and `height - 28`.
     * The first row is the two 150-wide buttons ("Play Selected World", "Create
     * New World"), the second the four 72-wide ones ("Edit", "Delete",
     * "Re-Create", "Cancel"). A list row is 36 tall: a 32x32 icon at the left
     * and the world's name over two grey detail lines beside it, with the
     * selected row wearing vanilla's black box under a one-pixel grey frame.
     */
    constexpr int WorldHeadingY = 8;
    constexpr int WorldSearchOffsetX = -100; ///< search box x = width / 2 + this
    constexpr int WorldSearchY = 22;
    constexpr int WorldSearchWidth = 200;
    constexpr int WorldSearchHeight = 20;
    constexpr int WorldListTop = 48;
    constexpr int WorldListBottomOffset = 64; ///< the list's bottom = height - this
    constexpr int WorldRowPitch = 36;
    constexpr int WorldRowHeight = 32;    ///< the icon and text block inside a row
    constexpr int WorldRowWidth = 270;    ///< vanilla's getRowWidth() = 220 + 50
    constexpr int WorldRowLeftOffset = 2; ///< row left = width/2 - rowWidth/2 + this
    constexpr int WorldIconSize = 32;
    constexpr int WorldRowTextOffset = 35; ///< text x = row left + icon + 3
    constexpr int WorldRowNameY = 1;
    constexpr int WorldRowSubY = 12;
    constexpr int WorldRowInfoY = 21;
    constexpr int WorldRow1YOffset = 52; ///< the button rows = height - these
    constexpr int WorldRow2YOffset = 28;
    constexpr int WorldRow1Width = 150;
    constexpr int WorldRow2Width = 72;
    constexpr int WorldRow1LeftOffset = -154, WorldRow1RightOffset = 4;
    constexpr int WorldRow2Offsets[4] = { -154, -76, 4, 82 };
    constexpr int WorldScrollbarWidth = 6; ///< vanilla's scrollbar at the list's right
    constexpr int WorldFadeHeight = 4;     ///< the black fades at the list's edges

    /** The screen's six buttons, in the order vanilla lays them out. */
    enum WorldAction
    {
        WorldPlay = 0,
        WorldCreate,
        WorldEdit,
        WorldDelete,
        WorldRecreate,
        WorldCancel,
        WorldActionCount
    };

    /**
     * The world-select screen's geometry. Everything is derived from the screen
     * size and the GUI scale, and `buttonRect()` is what the drawing and the hit
     * test both read, so they cannot disagree about where a button is. The list
     * scrolls whole rows: `scroll` is the first row in the window.
     */
    struct WorldSelectLayout
    {
        int scale = 1;
        int heading_y = 0;
        int search_x = 0, search_y = 0, search_w = 0, search_h = 0;
        int list_top = 0, list_bottom = 0;
        int row_left = 0, row_w = 0, row_pitch = 0, row_h = 0;
        int box_x = 0, box_w = 0; ///< the selection frame, a pixel around the row
        int icon_size = 0, text_x = 0;
        int button_h = 0, row1_y = 0, row2_y = 0;
        int row1_w = 0, row1_left_x = 0, row1_right_x = 0;
        int row2_w = 0, row2_x[4];

        /** The top of row `index` as drawn, with `scroll` rows above the window. */
        int rowY(int index, int scroll) const
        {
            // Vanilla's rows start four pixels under the list's top edge.
            return list_top + 4 * scale + (index - scroll) * row_pitch;
        }
        /** The box of the button at `action`. */
        void buttonRect(int action, int &x, int &y, int &w, int &h) const;
        /** The rows the window holds whole; a partial row is scrolled into view. */
        int visibleRows() const { return (list_bottom - list_top - 4 * scale) / row_pitch; }
    };

    /** The world-select screen's layout for this screen size. */
    WorldSelectLayout worldSelectLayout();

    // ------------------------------------------- the create/edit/confirm dialogs

    /**
     * The dialogs the screen opens, on vanilla's own numbers: `CreateWorldScreen`
     * and `EditWorldScreen` share the name field at y 60 under its grey label,
     * the "Will be saved in:" line at y 85 and the two-button row at
     * `height - 28`; the delete question is vanilla's `ConfirmScreen`, whose
     * title sits at y 70, its message at y 90 and its buttons at
     * `height / 6 + 96`.
     */
    constexpr int FormHeadingY = 20;
    constexpr int FormLabelY = 47;
    constexpr int FormFieldY = 60;
    constexpr int FormFieldWidth = 200;
    constexpr int FormFieldHeight = 20;
    constexpr int FormResultY = 85;
    constexpr int FormOptionY = 100;
    constexpr int FormHelpY = 122;
    constexpr int FormHelpPitch = 12;
    constexpr int FormButtonWidth = 150;
    constexpr int FormLeftOffset = -155, FormRightOffset = 5;
    constexpr int FormBottomYOffset = 28; ///< the bottom row = height - this
    constexpr int ConfirmTitleY = 70;
    constexpr int ConfirmMessageY = 90;
    constexpr int ConfirmLinePitch = 9;
    constexpr int ConfirmButtonYBase = 96;  ///< buttons = height/6 + this, at the least
    constexpr int ConfirmButtonYMax = 24;   ///< and never below height - this
    constexpr int ConfirmButtonWidth = 150;
    constexpr int ConfirmLeftOffset = -155, ConfirmRightOffset = 5;

    /** The create and edit dialogs' geometry (they share the name field). */
    struct WorldFormLayout
    {
        int scale = 1;
        int heading_y = 0, label_y = 0;
        int field_x = 0, field_y = 0, field_w = 0, field_h = 0;
        int result_y = 0, option_y = 0, help_y = 0, help_pitch = 0;
        int option_w = 0, button_h = 0, left_x = 0, right_x = 0, bottom_y = 0;

        /** The box of option button `index` (the mode/type rows, 0 and 1). */
        void optionRect(int index, int &x, int &y, int &w, int &h) const;
        /** The box of bottom button `index` (0 left, 1 right). */
        void bottomRect(int index, int &x, int &y, int &w, int &h) const;
    };

    WorldFormLayout worldFormLayout();

    /** The delete confirmation's geometry for a message of `message_lines` lines. */
    struct ConfirmLayout
    {
        int scale = 1;
        int title_y = 0, message_y = 0, line_pitch = 0, button_y = 0;
        int button_w = 0, button_h = 0, left_x = 0, right_x = 0;

        void buttonRect(int index, int &x, int &y, int &w, int &h) const;
    };

    ConfirmLayout confirmLayout(int message_lines);

    /** Every string the front-end shows, so the tasks and the tests agree on them. */
    extern const char *const versionText;
    extern const char *const creditText;
    extern const char *const pauseHeading;
    extern const char *const titleLabels[];
    extern const int titleLabelCount;
    extern const char *const worldSelectHeading;
    /** The six buttons, in WorldAction order: play, create, edit, delete, re-create, cancel. */
    extern const char *const worldSelectActionLabels[];
    extern const int worldSelectActionCount;
    extern const char *const worldSelectSearchHint; ///< what an empty search box shows
    /** The world's own lines: the name, the folder+date line, the game mode. */
    extern const char *const worldSelectDefaultName; ///< "New World", vanilla's default
    extern const char *const worldSelectNeverPlayed; ///< a world with no save yet
    extern const char *const worldSelectNewTag;      ///< a world that was never saved
    extern const char *const worldSelectWorldWord;   ///< vanilla's unnamed-world word
    extern const char *const survivalModeLabel;
    extern const char *const creativeModeLabel;
    /** The create/edit dialogs' strings. */
    extern const char *const createHeading;
    extern const char *const editHeading;
    extern const char *const nameLabel;         ///< "World Name"
    extern const char *const resultFolderLabel; ///< "Will be saved in:"
    extern const char *const gameModeLabel;     ///< "Game Mode"
    extern const char *const gameModeValues[];  ///< "Survival", "Creative"
    extern const char *const gameModeHelp[][2]; ///< vanilla's two help lines per mode
    extern const int gameModeCount;
    extern const char *const worldTypeLabel;    ///< "World Type"
    extern const char *const worldTypeValues[]; ///< the engine's world kinds
    extern const int worldTypeCount;
    extern const char *const createConfirmLabel; ///< "Create New World"
    extern const char *const saveLabel;          ///< "Save"
    extern const char *const cancelLabel;        ///< "Cancel"
    extern const char *const deleteQuestion;
    /** The warning's format: the world's name goes where %s is. */
    extern const char *const deleteWarningFormat;
    extern const char *const deleteConfirmLabel; ///< "Delete"
    extern const char *const pauseLabels[];
    extern const int pauseLabelCount;
    extern const char *const splashLines[];
    extern const int splashLineCount;

    /**
     * The whole title screen's geometry, derived from the screen size and the
     * font. Everything that has to fit -- the wordmark, the edition line, the
     * splash and the buttons -- is placed here in one pass.
     */
    struct TitleLayout
    {
        /** The full-width buttons: the first `TitleFullButtons` entries. */
        ButtonColumn buttons;
        /** The bottom row: the half-width pair, and the icon buttons on its ends. */
        int bottom_y = 0;
        int bottom_w = 0, bottom_h = 0;
        int bottom_left_x = 0, bottom_right_x = 0;
        int icon_left_x = 0, icon_right_x = 0, icon_w = 0;
        int logo_x = 0, logo_y = 0, logo_w = 0, logo_h = 0;
        int edition_x = 0, edition_y = 0, edition_w = 0, edition_h = 0;
        int splash_x = 0, splash_y = 0; ///< the splash's left edge, on its centre line
        int splash_scale = 100;         ///< percent, before the pulse
        int version_x = 0, credit_x = 0, version_y = 0;

        /**
         * The box of the button at `index`, wherever it is drawn: the full-width
         * column for the first entries, the half-width row for the next two, and
         * the 20x20 icon buttons at the row's two ends for the last. The task
         * draws through this and the mouse is tested against it, so the two
         * cannot disagree about where a button is.
         */
        void buttonRect(int index, int &x, int &y, int &w, int &h) const;
    };

    /** The title screen's layout for a given splash line (the line is random). */
    TitleLayout titleLayout(const char *splash);

    /**
     * The pause screen's geometry: the heading's y, and a box for every button.
     * The first and last entries are the full-width buttons and the rest sit two
     * to a row, which is vanilla's own shape; `buttonRect()` is what both the
     * drawing and the hit test read, so they cannot disagree.
     */
    struct PauseLayout
    {
        int scale = 1;
        int heading_y = 0;
        int count = 0;
        int top = 0;
        int wide_x = 0, wide_w = 0;
        int left_x = 0, right_x = 0, half_w = 0;
        int button_h = 0;

        /** The top of `row`, where row 0 is the first full-width button. */
        int rowY(int row) const { return top + row * PauseRowPitch * scale; }
        /** The box of the button at `index`, wherever the grid puts it. */
        void buttonRect(int index, int &x, int &y, int &w, int &h) const;
        /** The bottom edge of the lowest button. */
        int bottom() const { return rowY(PauseGridRows + 1) + button_h; }
    };

    /** The pause menu's grid. */
    PauseLayout pauseMenuLayout();

    /** The rows the options grid needs for `entry_count` entries over two columns. */
    int optionRows(int entry_count);

    /**
     * The options screen's geometry: the two columns, the pitch, how many rows fit
     * above the "Done" button and which row the visible window starts at. The
     * caller keeps the selection in view by passing the scroll it was given back;
     * `optionsScrollFor()` is the helper that works out what it should be.
     */
    struct OptionsLayout
    {
        int title_y = 0;
        int first_row_y = 0;   ///< the y of row 0, before scrolling
        int pitch = 0;
        int button_w = 0, button_h = 0;
        int left_x = 0, right_x = 0;
        int rows = 0;          ///< total rows, not the visible ones
        int visible_rows = 0;
        int first_visible = 0; ///< the scroll: the top row on screen
        int done_x = 0, done_y = 0, done_w = 0, done_h = 0;

        int columnX(int column) const { return column == 0 ? left_x : right_x; }
        /** The y of a row as drawn: rows above the window are off the top. */
        int rowY(int row) const { return first_row_y + (row - first_visible) * pitch; }
        /** True when a row is inside the visible window. */
        bool rowVisible(int row) const
        {
            return row >= first_visible && row < first_visible + visible_rows;
        }
    };

    /** The options screen's layout, scrolled so that `first_visible` is the top row. */
    OptionsLayout optionsLayout(int entry_count, int first_visible);

    /**
     * The scroll that keeps `row` on screen, moving as little as possible so the
     * grid does not jump when the selection moves within the window.
     */
    int optionsScrollFor(int row, int visible_rows, int total_rows, int current_scroll);

    /** Every string the options screen shows, so the test can measure them. */
    extern const char *const optionsHeading;
    extern const char *const optionsDoneLabel;
    extern const char *const guiScaleLabel;
    extern const char *const guiScaleValues[];
    extern const int guiScaleValueCount;
    extern const char *const loadingLabel; ///< "Loading terrain...", as vanilla words it

    /**
     * The loading screen's geometry: a label centred above a progress bar that sits
     * on the screen's own middle line. Everything is derived from the screen and the
     * font so the label and the bar cannot drift apart.
     */
    struct LoadingLayout
    {
        int label_y = 0; ///< the top of the centred label
        int bar_x = 0, bar_y = 0, bar_w = 0, bar_h = 0;
    };

    LoadingLayout loadingLayout();

    /**
     * Tiles the vanilla dirt (`options_background.png`) over a texture and dims it
     * to a quarter, which is what every vanilla menu is drawn on: the options
     * screen and the loading screen both want the same backdrop.
     */
    void drawMenuBackground(TEXTURE &tex);

    /**
     * The vanilla loading screen: the dirt, a centred label and a progress bar
     * filled to `percent` (0..100). A synchronous load reports no progress, so the
     * caller passes whatever fraction it has.
     */
    void drawLoadingScreen(TEXTURE &tex, const char *message, int percent);

    // ---------------------------------------------------------- the death screen

    /**
     * Vanilla's death screen, as `DeathScreen` lays it out and words it: a
     * doubled "You Died!" a quarter of the way down, a line where the death
     * cause sits, a score line under it, and two ordinary widget buttons --
     * "Respawn" and "Title Screen" -- starting at `height / 4 + 72`. The
     * background behind all of it is the game's red fade, not the dirt.
     */
    extern const char *const deathHeading;      ///< "You Died!"
    extern const char *const deathRespawnLabel; ///< "Respawn"
    extern const char *const deathTitleLabel;   ///< "Title Screen"

    struct DeathLayout
    {
        int title_y = 0;     ///< the top of the doubled title
        int title_scale = 2; ///< vanilla draws it twice the GUI scale
        int message_y = 0;   ///< where the death cause sits
        int score_y = 0;     ///< and the score under it
        ButtonColumn buttons;
    };

    /** The death screen's geometry, from the screen size and the GUI scale. */
    DeathLayout deathLayout();

    /**
     * The red wash vanilla throws over the world when the player dies: a vertical
     * gradient from a dark red to a brighter one. Vanilla blends it; nGL cannot,
     * so the same result is baked into the framebuffer by mixing each pixel toward
     * the gradient's red by the gradient's own alpha.
     */
    void drawDeathOverlay(TEXTURE &tex);

    /**
     * The dark wash vanilla's pause screen puts over the frozen world: the
     * `Screen.renderBackground` gradient `0xC0101010` to `0xD0101010`, mixed over
     * each pixel in place because nGL cannot blend. It is much darker than a half
     * shade -- the world reads at about a fifth through it.
     */
    void drawPauseOverlay(TEXTURE &tex);
}

#endif // MENUUI_H
