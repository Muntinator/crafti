#ifndef MENUUI_H
#define MENUUI_H

#include "gl.h"

/**
 * The look of the front-end screens: the title screen and the pause menu draw the
 * same widgets as each other, and they are meant to read as Minecraft's.
 *
 * Everything here is drawing, not state: a caller says what to paint and where,
 * and the numbers that make a menu look like Minecraft's live in one place rather
 * than being copied into each screen. The palette is Minecraft 1.8's — the dirt
 * background darkened to about two thirds, a stone-grey wordmark with a black
 * outline and a dark extrusion, yellow splash text, and the grey gradient button
 * whose focused state is a white border.
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

    // The title screen's palette.
    constexpr COLOR DirtShade = rgb(150, 150, 150); ///< multiplier applied to the dirt (see shade())
    constexpr COLOR Text = rgb(255, 255, 255);
    constexpr COLOR TextShadow = rgb(63, 63, 63);
    constexpr COLOR ButtonTop = rgb(150, 150, 150);
    constexpr COLOR ButtonBottom = rgb(108, 108, 108);
    constexpr COLOR ButtonEdge = rgb(48, 48, 48);
    constexpr COLOR ButtonFocusedTop = rgb(190, 190, 190);
    constexpr COLOR ButtonFocusedBottom = rgb(150, 150, 150);
    constexpr COLOR ButtonDisabledTop = rgb(90, 90, 90);
    constexpr COLOR ButtonDisabledBottom = rgb(70, 70, 70);
    constexpr COLOR TextDisabled = rgb(160, 160, 160);
    constexpr COLOR LogoFace = rgb(214, 214, 214);
    constexpr COLOR LogoExtrude = rgb(94, 94, 94);
    constexpr COLOR LogoOutline = rgb(30, 30, 30);
    constexpr COLOR Splash = rgb(255, 255, 60);

    /** A filled rectangle, clipped to the texture. drawRectangle() only outlines. */
    void fillRect(TEXTURE &tex, int x, int y, int w, int h, COLOR color);

    /**
     * Dims a rectangle in place by about a third, which is what Minecraft does to
     * the dirt behind its menus so that the text on top stays readable.
     */
    void shadeRect(TEXTURE &tex, int x, int y, int w, int h, int keep_percent);

    /**
     * A Minecraft button: a grey vertical gradient inside a dark 1-pixel edge, and
     * a white border when it is the one the player is on. A disabled button goes
     * dark, the way vanilla greys out an option that cannot be chosen (there is no
     * world to continue yet, for instance).
     */
    void drawButton(TEXTURE &tex, int x, int y, int w, int h, bool focused, bool enabled = true);

    /**
     * A centred label in the vanilla style: white with a dark shadow one pixel
     * down and right, drawn inside `y`..`y + h` of a button.
     */
    void drawButtonLabel(const char *text, TEXTURE &tex, int x, int y, int w, int h,
                         bool focused, bool enabled = true);

    /**
     * The wordmark: the text at `scale`, outlined in black, extruded down-right in
     * dark stone and faced in light stone, with a highlight along the top -- the
     * same three passes Minecraft's logo is made of. Centred on `center_x`, with
     * its top at `y`.
     */
    void drawLogo(const char *text, TEXTURE &tex, int center_x, int y, int scale);

    /** The yellow splash line, outlined so it stays readable over dirt. */
    void drawSplash(const char *text, TEXTURE &tex, int center_x, int y);

    /** A left-aligned line of the vanilla small print (the version, the credits). */
    void drawSmallPrint(const char *text, TEXTURE &tex, int x, int y);

    /** The scale the front-end should use on this screen (1 on the CX, 2 on a desktop). */
    int uiScale();

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

    /** A column centred on `centre_y`. */
    ButtonColumn centeredButtonColumn(int count, int centre_y);

    /** Every string the front-end shows, so the tasks and the tests agree on them. */
    extern const char *const titleWordmark;
    extern const char *const versionText;
    extern const char *const creditText;
    extern const char *const hintText;
    extern const char *const titleLabels[];
    extern const int titleLabelCount;
    extern const char *const pauseLabels[];
    extern const int pauseLabelCount;
    extern const char *const splashLines[];
    extern const int splashLineCount;

    /**
     * The whole title screen's geometry, derived from the screen size and the
     * font. Everything that has to fit -- the wordmark, the splash, the buttons
     * and the three lines of small print -- is placed here in one pass.
     */
    struct TitleLayout
    {
        ButtonColumn buttons;
        int logo_scale = 1;
        int logo_y = 0;
        int logo_height = 0;
        int logo_width = 0;
        int splash_y = 0;
        int version_y = 0;
        int hint_y = 0;
        int audio_y = 0;
    };

    TitleLayout titleLayout();
    /** The pause menu's button column. */
    ButtonColumn pauseMenuLayout();

    /** The margin the wordmark needs around it for its outline and its bevel. */
    int logoMargin(int scale);

    /**
     * The wordmark's scale: the letter height that makes the text about
     * `width_percent` of the screen wide, which is how vanilla sizes its logo.
     */
    int logoScaleFor(const char *text, int width_percent);
    int logoWidthFor(const char *text, int scale);
    int logoHeightFor(int scale);
}

#endif // MENUUI_H
