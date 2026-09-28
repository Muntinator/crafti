#include "menuui.h"

#include <algorithm>

#include "font.h"

namespace MenuUI
{
    // The front-end's own strings. The tasks draw these and nothing else, so the
    // labels, the wordmark and the splashes are in one place and a host test can
    // measure them against the boxes they are drawn in.
    const char *const titleWordmark = "MUNTCRAFT";
    const char *const versionText = "Muntcraft 1.8.9";
    const char *const creditText = "Copyright Munt. Do not distribute!";
    const char *const hintText = "Up/Down to move, 5 to select";

    const char *const titleLabels[] = {
        "Continue",
        "New Flat World",
        "New Terrain World",
        "Graphing Mode",
        "Quit Game"
    };
    const int titleLabelCount = static_cast<int>(sizeof(titleLabels) / sizeof(titleLabels[0]));

    const char *const pauseLabels[] = {
        "Back to Game",
        "Options...",
        "Help",
        "Save World",
        "Sound Test...",
        "Save and Quit"
    };
    const int pauseLabelCount = static_cast<int>(sizeof(pauseLabels) / sizeof(pauseLabels[0]));

    const char *const splashLines[] = {
        "Also try Minecraft!",
        "100% block!",
        "Now with beds!",
        "Watch out for creepers!",
        "Dig dig dig!",
        "Villagers included!",
        "Diamonds are forever!",
        "As seen on a calculator!",
        "Enchanted!",
        "Rain, snow and lightning!",
        "Do not drink the lava!",
        "Snow drifts ahead!"
    };
    const int splashLineCount = static_cast<int>(sizeof(splashLines) / sizeof(splashLines[0]));

    int uiScale()
    {
        // The front-end was laid out for the calculator's 320x240, and a desktop
        // window is twice that in both directions, so one factor covers both.
        const int scale = SCREEN_WIDTH / 320;
        return scale < 1 ? 1 : scale;
    }

    void fillRect(TEXTURE &tex, int x, int y, int w, int h, COLOR color)
    {
        if(x < 0)
        {
            w += x;
            x = 0;
        }
        if(y < 0)
        {
            h += y;
            y = 0;
        }
        if(x + w > static_cast<int>(tex.width))
            w = static_cast<int>(tex.width) - x;
        if(y + h > static_cast<int>(tex.height))
            h = static_cast<int>(tex.height) - y;
        if(w <= 0 || h <= 0)
            return;

        for(int row = 0; row < h; ++row)
        {
            COLOR *line = tex.bitmap + (y + row) * tex.width + x;
            for(int col = 0; col < w; ++col)
                line[col] = color;
        }
    }

    void shadeRect(TEXTURE &tex, int x, int y, int w, int h, int keep_percent)
    {
        if(x < 0)
        {
            w += x;
            x = 0;
        }
        if(y < 0)
        {
            h += y;
            y = 0;
        }
        if(x + w > static_cast<int>(tex.width))
            w = static_cast<int>(tex.width) - x;
        if(y + h > static_cast<int>(tex.height))
            h = static_cast<int>(tex.height) - y;
        if(w <= 0 || h <= 0)
            return;

        if(keep_percent < 0)
            keep_percent = 0;
        if(keep_percent > 100)
            keep_percent = 100;

        // Halving every channel is one mask and one shift however the pixel is
        // packed, and half brightness is what the menus dim by, so that case is
        // worth its own loop: the pause screen shades the whole world behind it.
        if(keep_percent == 50)
        {
            for(int row = 0; row < h; ++row)
            {
                COLOR *line = tex.bitmap + (y + row) * tex.width + x;
                for(int col = 0; col < w; ++col)
                    line[col] = static_cast<COLOR>((line[col] & 0xF7DE) >> 1);
            }
            return;
        }

        for(int row = 0; row < h; ++row)
        {
            COLOR *line = tex.bitmap + (y + row) * tex.width + x;
            for(int col = 0; col < w; ++col)
            {
                const COLOR c = line[col];
                const int r = ((c >> 11) & 0x1F) * keep_percent / 100;
                const int g = ((c >> 5) & 0x3F) * keep_percent / 100;
                const int b = (c & 0x1F) * keep_percent / 100;
                line[col] = static_cast<COLOR>((r << 11) | (g << 5) | b);
            }
        }
    }

    void drawButton(TEXTURE &tex, int x, int y, int w, int h, bool focused, bool enabled)
    {
        const COLOR top = !enabled ? ButtonDisabledTop : (focused ? ButtonFocusedTop : ButtonTop);
        const COLOR bottom = !enabled ? ButtonDisabledBottom : (focused ? ButtonFocusedBottom : ButtonBottom);

        // Dark edge, then the gradient two halves (which is all the vanilla widget
        // sheet is: a light top half over a darker bottom one).
        fillRect(tex, x, y, w, h, ButtonEdge);
        fillRect(tex, x + 1, y + 1, w - 2, (h - 2) / 2, top);
        fillRect(tex, x + 1, y + 1 + (h - 2) / 2, w - 2, h - 2 - (h - 2) / 2, bottom);

        // The button the player is on is the one with a bright border.
        if(focused && enabled)
        {
            fillRect(tex, x, y, w, 1, Text);
            fillRect(tex, x, y + h - 1, w, 1, Text);
            fillRect(tex, x, y, 1, h, Text);
            fillRect(tex, x + w - 1, y, 1, h, Text);
        }
    }

    void drawButtonLabel(const char *text, TEXTURE &tex, int x, int y, int w, int h,
                         bool focused, bool enabled)
    {
        (void)focused; // vanilla labels the focused button the same as the rest

        const int text_w = static_cast<int>(measureString(text));
        const int text_x = x + (w - text_w) / 2;
        const int text_y = y + (h - static_cast<int>(fontHeight())) / 2 + 1;

        // Vanilla labels always carry a shadow, which is what keeps them legible
        // on both halves of the gradient.
        drawString(text, TextShadow, tex, text_x + 1, text_y + 1);
        drawString(text, enabled ? Text : TextDisabled, tex, text_x, text_y);
    }

    void drawLogo(const char *text, TEXTURE &tex, int center_x, int y, int scale)
    {
        if(scale < 1)
            scale = 1;

        const int width = static_cast<int>(measureString(text)) * scale;
        const int height = static_cast<int>(fontHeight()) * scale;
        const int left = center_x - width / 2;
        const int outline = scale / 2 > 0 ? scale / 2 : 1;

        // The shadow first, down and to the right, which is what gives the
        // wordmark the depth the vanilla logo has; then a black outline all the
        // way round; then the stone face on top.
        drawStringScaled(text, LogoExtrude, tex, left + outline * 2, y + outline * 2, scale);

        for(int dx = -outline; dx <= outline; ++dx)
            for(int dy = -outline; dy <= outline; ++dy)
                if(dx != 0 || dy != 0)
                    drawStringScaled(text, LogoOutline, tex,
                                     left + dx, y + dy, scale);

        drawStringScaled(text, LogoFace, tex, left, y, scale);

        // A one-pixel highlight along the top of every stroke, which is the bevel
        // that makes the vanilla logotype read as stone rather than as flat text.
        for(int x = 0; x < width; ++x)
        {
            for(int row = 0; row < height; ++row)
            {
                const int px = left + x;
                const int py = y + row;
                if(px < 0 || py < 0 || px >= static_cast<int>(tex.width) || py >= static_cast<int>(tex.height))
                    continue;
                if(tex.bitmap[px + py * tex.width] != LogoFace)
                    continue;

                const bool face_above = py > 0 && tex.bitmap[px + (py - 1) * tex.width] == LogoFace;
                if(!face_above)
                    tex.bitmap[px + py * tex.width] = Text;
            }
        }
    }

    void drawSplash(const char *text, TEXTURE &tex, int center_x, int y)
    {
        const int width = static_cast<int>(measureString(text));
        const int left = center_x - width / 2;

        // Outlined rather than shadowed: the splash sits on dirt, where a single
        // shadow is not enough to keep yellow readable.
        drawString(text, LogoOutline, tex, left - 1, y);
        drawString(text, LogoOutline, tex, left + 1, y);
        drawString(text, LogoOutline, tex, left, y - 1);
        drawString(text, LogoOutline, tex, left, y + 1);
        drawString(text, Splash, tex, left, y);
    }

    void drawSmallPrint(const char *text, TEXTURE &tex, int x, int y)
    {
        drawString(text, LogoOutline, tex, x + 1, y + 1);
        drawString(text, Text, tex, x, y);
    }

    ButtonColumn centeredButtonColumn(int count, int centre_y)
    {
        ButtonColumn column;
        column.count = count < 0 ? 0 : count;
        column.w = SCREEN_WIDTH * 2 / 5;
        column.h = 20 * uiScale();
        column.gap = 4 * uiScale();
        column.x = (SCREEN_WIDTH - column.w) / 2;

        const int block_h = column.count * column.h + (column.count > 0 ? (column.count - 1) * column.gap : 0);
        column.top = centre_y - block_h / 2;
        return column;
    }

    int logoMargin(int scale)
    {
        const int outline = scale / 2 > 0 ? scale / 2 : 1;
        return outline * 3;
    }

    int logoWidthFor(const char *text, int scale)
    {
        return static_cast<int>(measureString(text)) * (scale < 1 ? 1 : scale);
    }

    int logoHeightFor(int scale)
    {
        return static_cast<int>(fontHeight()) * (scale < 1 ? 1 : scale);
    }

    int logoScaleFor(const char *text, int width_percent)
    {
        const int width = static_cast<int>(measureString(text));
        if(width <= 0)
            return 1;

        const int wanted = SCREEN_WIDTH * width_percent / 100;
        const int scale = wanted / width;
        return scale < 1 ? 1 : scale;
    }

    TitleLayout titleLayout()
    {
        TitleLayout layout;

        layout.logo_scale = logoScaleFor(titleWordmark, 62);
        layout.logo_width = logoWidthFor(titleWordmark, layout.logo_scale);
        layout.logo_height = logoHeightFor(layout.logo_scale);
        layout.logo_y = SCREEN_HEIGHT / 12;

        // The splash overlaps the tail of the wordmark by a line, the way vanilla
        // tucks it under the logo's right-hand corner.
        layout.splash_y = layout.logo_y + layout.logo_height - fontHeight();

        // The buttons sit below the middle, so the wordmark and the splash have the
        // top of the screen and the small print has the bottom.
        layout.buttons = centeredButtonColumn(titleLabelCount, SCREEN_HEIGHT / 2 + SCREEN_HEIGHT / 24);

        layout.version_y = SCREEN_HEIGHT - fontHeight() - 1;
        layout.hint_y = layout.version_y - fontHeight() - 1;
        layout.audio_y = layout.version_y - (fontHeight() + 1) * 2;
        return layout;
    }

    ButtonColumn pauseMenuLayout()
    {
        return centeredButtonColumn(pauseLabelCount, SCREEN_HEIGHT / 2);
    }
}
