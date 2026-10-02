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
    constexpr COLOR Outline = rgb(30, 30, 30);
    constexpr COLOR Splash = rgb(255, 255, 60);

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
     * buttons are full width and the last two share a row of half-width buttons,
     * `height / 4 + 48 + 72 + 12` down. Vanilla's language and accessibility icon
     * buttons would flank that row; this engine has neither screen, so the two
     * half buttons stand alone, as the rest of the port drops what does not exist.
     */
    constexpr int TitleFullButtons = 3;
    constexpr int TitleHalfWidth = 98;     ///< the widget sheet's half button
    constexpr int TitleBottomOffset = 72 + 12;
    /**
     * The title wordmark is drawn at `width / 2 - 137` -- vanilla's own x, where
     * the 274-pixel image (two 155-pixel halves joined) comes out centred -- and
     * the `Java Edition` strip at `width / 2 - 49`, 67 pixels down, over the lower
     * band of the wordmark where vanilla keeps it. The splash is centred at
     * `width / 2 + 90`, its centre line 70 pixels down, and tilted 20 degrees.
     */
    constexpr int TitleEditionTop = 67;
    constexpr int TitleSplashOffsetX = 90;
    constexpr int TitleSplashCentreY = 70;
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

    /** Every string the front-end shows, so the tasks and the tests agree on them. */
    extern const char *const versionText;
    extern const char *const creditText;
    extern const char *const pauseHeading;
    extern const char *const titleLabels[];
    extern const int titleLabelCount;
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
        /** The bottom row: the last two entries, half width, side by side. */
        int bottom_y = 0;
        int bottom_w = 0, bottom_h = 0;
        int bottom_left_x = 0, bottom_right_x = 0;
        int logo_x = 0, logo_y = 0, logo_w = 0, logo_h = 0;
        int edition_x = 0, edition_y = 0, edition_w = 0, edition_h = 0;
        int splash_x = 0, splash_y = 0; ///< the splash's left edge, on its centre line
        int splash_scale = 100;         ///< percent, before the pulse
        int version_x = 0, credit_x = 0, version_y = 0;

        /**
         * The box of the button at `index`, wherever it is drawn: the full-width
         * column for the first entries, the half-width row for the last two. The
         * task draws through this and the mouse is tested against it, so the two
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
