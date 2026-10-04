#include "menuui.h"

#include <algorithm>
#include <cstdio>

#include "font.h"

#include "textures/menu_button.h"
#include "textures/menu_background.h"
#include "textures/slider_handle.h"
#include "textures/checkbox.h"
#include "textures/title_logo.h"
#include "textures/edition.h"

namespace
{
    /**
     * A nearest-neighbour blit of one rectangle of a source texture into a box of
     * the destination, clipped on both sides. It is a local copy rather than
     * nGL's drawTexture() because the layout test links this file on its own: the
     * menus want the official widget art, and nothing in the front-end should drag
     * the rest of the engine into a host build to get it.
     *
     * A source pixel equal to the source's transparent colour is skipped, which is
     * how the button keeps its rounded corners and how the wordmark, which is an
     * image with a transparent background, does not draw a black box around itself.
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

    /**
     * The washes vanilla blends over a screen are per-channel maps: the pause
     * and death gradients mix each pixel toward a colour by the row's alpha, and
     * the menu screens dim each pixel by a constant. A channel has only 32 or 64
     * possible inputs, so a row's map is three small tables -- built with the
     * exact integer arithmetic the direct mix uses, so the output is bit for bit
     * what vanilla's blend asks for -- and a pixel becomes three lookups instead
     * of six integer divisions. On a calculator that is the difference between a
     * menu eating its frame budget and not.
     */
    struct MixTables
    {
        uint8_t r[32], g[64], b[32];

        /** `keep / 255` of the old channel plus `add8`, packed back to 565. */
        void build(int keep, int add_r, int add_g, int add_b)
        {
            for(int v = 0; v < 32; ++v)
                r[v] = static_cast<uint8_t>((((v * 255 / 31) * keep / 255) + add_r) >> 3);
            for(int v = 0; v < 64; ++v)
                g[v] = static_cast<uint8_t>((((v * 255 / 63) * keep / 255) + add_g) >> 2);
            for(int v = 0; v < 32; ++v)
                b[v] = static_cast<uint8_t>((((v * 255 / 31) * keep / 255) + add_b) >> 3);
        }

        /** The old channel scaled by `keep_percent / 100`. */
        void buildScaled(int keep_percent)
        {
            for(int v = 0; v < 32; ++v)
                r[v] = static_cast<uint8_t>(v * keep_percent / 100);
            for(int v = 0; v < 64; ++v)
                g[v] = static_cast<uint8_t>(v * keep_percent / 100);
            for(int v = 0; v < 32; ++v)
                b[v] = static_cast<uint8_t>(v * keep_percent / 100);
        }

        COLOR mix(COLOR c) const
        {
            return static_cast<COLOR>((r[(c >> 11) & 0x1F] << 11)
                                     | (g[(c >> 5) & 0x3F] << 5)
                                     | b[c & 0x1F]);
        }
    };
}

namespace MenuUI
{
    // The front-end's own strings. The tasks draw these and nothing else, so the
    // labels and the splashes are in one place and a host test can measure them
    // against the boxes they are drawn in.
    const char *const versionText = "Muntcraft 1.14.0";
    const char *const creditText = "Copyright Munt. Do not distribute!";
    const char *const pauseHeading = "Game Menu";

    // The options screen. Vanilla's own widgets carry vanilla's own words: the
    // screen is headed "Options...", its last button is "Done", and the one
    // setting the screen adds to this engine is the GUI scale, which vanilla
    // spells as a list from Auto up to 4.
    const char *const optionsHeading = "Options...";
    const char *const optionsDoneLabel = "Done";
    const char *const guiScaleLabel = "GUI scale";
    const char *const guiScaleValues[] = { "Auto", "1x", "2x", "3x", "4x" };
    const int guiScaleValueCount = static_cast<int>(sizeof(guiScaleValues) / sizeof(guiScaleValues[0]));

    // Vanilla's own words for the loading screen.
    const char *const loadingLabel = "Loading terrain...";

    // Vanilla's own words for the death screen, from 1.17.1's `deathScreen.*`
    // translation keys.
    const char *const deathHeading = "You Died!";
    const char *const deathRespawnLabel = "Respawn";
    const char *const deathTitleLabel = "Title Screen";

    // Vanilla's title screen in vanilla's own words and vanilla's own order: the
    // three full-width buttons, then "Options..." and "Quit Game" half-width on
    // the shared row. The two icon slots at the end carry no text -- they are
    // vanilla's language and accessibility buttons, which this engine draws in
    // their vanilla places but cannot serve, so they are greyed out.
    const char *const titleLabels[] = {
        "Singleplayer",
        "Multiplayer",
        "Minecraft Realms",
        "Options...",
        "Quit Game",
        "",
        ""
    };
    const int titleLabelCount = static_cast<int>(sizeof(titleLabels) / sizeof(titleLabels[0]));

    // Vanilla's "Select World" screen, in vanilla's own words and vanilla's own
    // order: the two 150-wide buttons under the list, then the four 72-wide ones
    // under those. The strings are 1.17.1's `selectWorld.*` translation values,
    // kept verbatim.
    const char *const worldSelectHeading = "Select World";
    const char *const worldSelectActionLabels[] = {
        "Play Selected World",
        "Create New World",
        "Edit",
        "Delete",
        "Re-Create",
        "Cancel"
    };
    const int worldSelectActionCount = static_cast<int>(sizeof(worldSelectActionLabels) / sizeof(worldSelectActionLabels[0]));
    const char *const worldSelectSearchHint = "search for worlds";
    const char *const worldSelectDefaultName = "New World";
    const char *const worldSelectNeverPlayed = "Never played!";
    const char *const worldSelectNewTag = "New!";
    const char *const worldSelectWorldWord = "World";

    // The game modes the create dialog offers, with vanilla's own words and the
    // two help lines vanilla shows under the pair for whichever is chosen.
    const char *const survivalModeLabel = "Survival";
    const char *const creativeModeLabel = "Creative";
    const char *const gameModeValues[] = { "Survival", "Creative" };
    const char *const gameModeHelp[][2] = {
        { "Search for resources, craft, gain", "levels, health and hunger" },
        { "Unlimited resources, free flying and", "destroy blocks instantly" }
    };
    const int gameModeCount = static_cast<int>(sizeof(gameModeValues) / sizeof(gameModeValues[0]));

    // The create/edit dialogs: vanilla's `CreateWorldScreen` and
    // `EditWorldScreen` wording, with the engine's own world kinds standing in
    // for vanilla's world types on the second button of the pair.
    const char *const createHeading = "Create New World";
    const char *const editHeading = "Edit World";
    const char *const nameLabel = "World Name";
    const char *const resultFolderLabel = "Will be saved in:";
    const char *const gameModeLabel = "Game Mode";
    const char *const worldTypeLabel = "World Type";
    const char *const worldTypeValues[] = { "Normal", "Flat", "Graphing" };
    const int worldTypeCount = static_cast<int>(sizeof(worldTypeValues) / sizeof(worldTypeValues[0]));
    const char *const createConfirmLabel = "Create New World";
    const char *const saveLabel = "Save";
    const char *const cancelLabel = "Cancel";

    // The delete question, which is vanilla's `ConfirmScreen`: the question as
    // the heading and the loss spelled out under it, with the world's name where
    // the format string's %s is.
    const char *const deleteQuestion = "Are you sure you want to delete this world?";
    const char *const deleteWarningFormat = "'%s' will be lost forever! (A long time!)";
    const char *const deleteConfirmLabel = "Delete";

    // Vanilla's pause screen, in its own order and its own grid: a full-width
    // button, three rows of two, then a full-width button. "Advancements" and
    // "Statistics" have no screen in this engine, so Help (the in-game guide) and
    // Block List (the catalogue) stand in for them; Save World, Sound Test and
    // Player Inventory are this game's own, standing in where vanilla keeps its
    // Send Feedback, Report Bugs and Share to LAN.
    const char *const pauseLabels[] = {
        "Back to Game",
        "Help",
        "Block List",
        "Save World",
        "Sound Test...",
        "Options...",
        "Player Inventory",
        "Save and Quit to Title"
    };
    const int pauseLabelCount = static_cast<int>(sizeof(pauseLabels) / sizeof(pauseLabels[0]));

    // The splashes are vanilla's. They are taken from 1.17.1's
    // `assets/minecraft/texts/splashes.txt`, kept verbatim and in the file's own
    // order; this is a sample of it rather than all 427 lines, because the rest are
    // either the same jokes again or are written in characters the font has no
    // glyphs for. The "Minceraft" one-in-ten-thousand easter egg is not kept: it
    // swaps the wordmark image as well as the line, and the wordmark here is the
    // single-line crop of `gui/title/minecraft.png` the title screen always shows.
    const char *const splashLines[] = {
        "As seen on TV!",
        "More polygons!",
        "It's here!",
        "Excitement!",
        "Indev!",
        "It's a game!",
        "Minecraft!",
        "Ingots!",
        "Create!",
        "The bee's knees!",
        "Not on steam!",
        "Teetsuuuuoooo!",
        "90% bug free!",
        "Absolutely no memes!",
        "Cloud computing!",
        "Bringing home the bacon!",
        "Euclidian!",
        "Complex cellular automata!",
        "Thousands of colors!",
        "Sensational!",
        "Macroscopic!",
        "Monster infighting!",
        "You've got a brand new key!",
        "All inclusive!",
        "Livestreamed!",
        "All is full of love!",
        "Collaborate and listen!",
        "Han shot first!",
        "Larger than Earth!",
        "Falling off cliffs!",
        "Let's danec!",
        "20 GOTO 10!",
        "Cogito ergo sum!",
        "BTAF used to be good!",
        "Bring me Ray Cokes!",
        "Sublime!",
        "Rita is the new top dog!",
        "Supercalifragilisticexpialidocious!",
        "Fan fiction!",
        "Internet enabled!",
        "DRR! DRR! DRR!",
        "Woo, somethingawful!",
        "Woo, 2pp!",
        "Very fun!",
        "MAP11 has two names!",
        "Bees, bees, bees, bees!",
        "Menger sponge!",
        "Slow acting portals!",
        "Finally with ladders!",
        "Jump up, jump up, and get down!",
        "Welcome to your Doom!",
        "\"Autological\" is!",
        "The creeper is a spy!",
        "The sky is the limit!",
        "Undefeated!",
        "This message will never appear on the splash screen, isn't that weird?",
        "Tyrion would love it!",
        "Also try Mount And Blade!",
        "Also try Pixeljunk Shooter!",
        "Read more books!",
        "Bigger than a bread box!",
        "Don't bother with the clones!",
        "Finally complete!",
        "Testificates!",
        "Place ALL the blocks!",
        "Ghoughpteighbteau tchoghs!",
        "Doesn't use the U-word!",
        "Technologic!",
        "My life for Aiur!",
        "You can't explain that!",
        "Mmmph, mmph!",
        ".party()!",
        "I have a suggestion.",
        "HURNERJSGER?",
        "pls rt",
        "One day, somewhere in the future, my work will be quoted!",
        "So sweet, like a nice bon bon!",
        "Warning! A huge battleship \"STEVE\" is approaching fast!",
        "Strange, but not a stranger!",
        "Take an eggbeater and beat it against a skillet!",
        "/give @a hugs 64",
        "Where there is not light, there can spider!",
        "Falling with style!",
        "Replaced molten cheese with blood?",
        "Should not be played while driving",
        "An illusion! What are you hiding?",
        "Truly gone fishing!",
        "Ahhhhhh!",
        "Plant a tree!",
        "It came from space.",
        "Ping the human!",
        "Wash your hands!",
        "Stay safe!",
        "Shop for your elders!",
        "Everybody do the Leif!",
        "From free range developers!",
        "And my pickaxe!",
        "Vanilla!",
        "Be anti-racist!",
        "Educate your friends on anti-racism!",
        "Contains simulated goats!",
        "Now you are thinking with pistons!",
        "Plant-based light sources!",
    };
    const int splashLineCount = static_cast<int>(sizeof(splashLines) / sizeof(splashLines[0]));

    namespace
    {
        /**
         * The player's GUI-scale choice, as the options screen stores it: 0 is
         * vanilla's Auto, and 1..4 are the manual scales. Auto is the default, so
         * the front-end looks the same on a machine that has never touched the
         * option.
         */
        int preferred_scale = 0;
    }

    int maxUiScale()
    {
        // The front-end was laid out for the calculator's 320x240, and a desktop
        // window is twice that in both directions, so one factor covers both.
        const int scale = SCREEN_WIDTH / 320;
        return scale < 1 ? 1 : scale;
    }

    int uiScale()
    {
        // A bigger GUI than the screen can hold is not an option, so a manual
        // scale is clamped to Auto's; on the calculator every choice collapses to
        // 1, which is also what vanilla shows in a 320x240 window.
        const int largest = maxUiScale();
        if(preferred_scale < 1 || preferred_scale > largest)
            return largest;
        return preferred_scale;
    }

    int guiScale()
    {
        return preferred_scale;
    }

    void setGuiScale(int scale)
    {
        preferred_scale = scale < 0 ? 0 : scale;
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

        // The factor is one map for the whole rectangle: built once, three
        // lookups a pixel.
        MixTables mix;
        mix.buildScaled(keep_percent);

        for(int row = 0; row < h; ++row)
        {
            COLOR *line = tex.bitmap + (y + row) * tex.width + x;
            for(int col = 0; col < w; ++col)
                line[col] = mix.mix(line[col]);
        }
    }

    void drawButton(TEXTURE &tex, int x, int y, int w, int h, bool focused, bool enabled)
    {
        if(w <= 0 || h <= 0)
            return;

        // The official widget sheet holds all three states of a Minecraft button
        // stacked in one 200x60 block, and in 1.17 they are ordered greyed-out
        // first (y=46, a dark face), then plain (y=66), then highlighted (y=86,
        // which is the one with the white outline). Vanilla picks the row for the
        // state and stretches the 200x20 button to the box the screen gave it,
        // which is all this has to do.
        const int state_row = !enabled ? 0 : (focused ? 40 : 20);
        blit(menu_button, tex, 0, state_row, 200, 20, x, y, w, h);
    }

    void drawButtonLabel(const char *text, TEXTURE &tex, int x, int y, int w, int h,
                         bool focused, bool enabled)
    {
        (void)focused; // vanilla labels the focused button the same as the rest

        // The icon buttons carry no label at all; an empty string draws nothing.
        if(text == nullptr || text[0] == '\0')
            return;

        const int text_w = static_cast<int>(measureString(text));
        const int text_x = x + (w - text_w) / 2;
        const int text_y = y + (h - static_cast<int>(fontHeight())) / 2 + 1;

        // Vanilla labels always carry a shadow, which is what keeps them legible
        // on both halves of the gradient.
        drawString(text, TextShadow, tex, text_x + 1, text_y + 1);
        drawString(text, enabled ? Text : TextDisabled, tex, text_x, text_y);
    }

    void drawIconButton(const TEXTURE &icon, TEXTURE &tex, int x, int y, int w, int h,
                        bool focused, bool enabled)
    {
        drawButton(tex, x, y, w, h, focused, enabled);

        // The icon is vanilla's 15x15, centred in the 20x20 button and scaled
        // with the GUI scale like everything else on the screen.
        const int s = uiScale();
        const int icon_w = static_cast<int>(icon.width) * s;
        const int icon_h = static_cast<int>(icon.height) * s;
        blit(icon, tex, 0, 0, icon.width, icon.height,
             x + (w - icon_w) / 2, y + (h - icon_h) / 2, icon_w, icon_h);
    }

    void drawEditBox(TEXTURE &tex, int x, int y, int w, int h,
                     const char *text, const char *hint, bool focused)
    {
        if(w <= 0 || h <= 0)
            return;
        const int s = uiScale();

        // Vanilla's edit box: a one-pixel grey frame around a black field.
        fillRect(tex, x, y, w, h, EditBoxBorder);
        fillRect(tex, x + s, y + s, w - 2 * s, h - 2 * s, Black);

        // The text sits four pixels in, greyed to the placeholder colour while the
        // box is empty and the placeholder is what is showing.
        const bool empty = text == nullptr || text[0] == '\0';
        const char *shown = empty ? hint : text;
        if(shown == nullptr)
            shown = "";
        const int text_x = x + 4 * s;
        const int text_y = y + (h - static_cast<int>(fontHeight())) / 2;
        drawString(shown, empty ? EditBoxHint : Text, tex, text_x, text_y);

        // The caret sits at the end of the text while the box holds the focus.
        if(focused)
        {
            const int caret_x = text_x + (empty ? 0 : static_cast<int>(measureString(text)));
            fillRect(tex, caret_x, text_y, s, static_cast<int>(fontHeight()), Text);
        }
    }

    void drawWorldEntry(const char *name, const char *sub, const char *info,
                        TEXTURE &tex, int x, int y)
    {
        // The row's icon sits at (x, y); the three lines are drawn beside it at
        // vanilla's own 1/12/21 offsets, plain (no shadow) as vanilla draws list
        // rows: the name in white over the two grey detail lines.
        const int s = uiScale();
        const int text_x = x + WorldRowTextOffset * s;
        drawString(name, Text, tex, text_x, y + WorldRowNameY * s);
        drawString(sub, EntrySub, tex, text_x, y + WorldRowSubY * s);
        drawString(info, EntrySub, tex, text_x, y + WorldRowInfoY * s);
    }

    void drawSplash(const char *text, TEXTURE &tex, int x, int y)
    {
        // Vanilla draws the splash with the font's ordinary shadow, not an
        // outline: the same glyphs one pixel down and right in the shadow colour
        // vanilla derives for a shadowed line (a dark yellow for the yellow
        // text), with the yellow itself on top. The caller leaves a pixel of
        // margin, which is where the shadow lands.
        drawString(text, SplashShadow, tex, x + 1, y + 1);
        drawString(text, Splash, tex, x, y);
    }

    void drawSmallPrint(const char *text, TEXTURE &tex, int x, int y)
    {
        // The version and the credits are plain white text: vanilla draws them
        // with `drawString`, which has no shadow, unlike the centred headings and
        // the button labels.
        drawString(text, Text, tex, x, y);
    }

    void drawHeading(const char *text, TEXTURE &tex, int y)
    {
        const int x = (SCREEN_WIDTH - static_cast<int>(measureString(text))) / 2;
        drawString(text, TextShadow, tex, x + 1, y + 1);
        drawString(text, Text, tex, x, y);
    }

    int headingY()
    {
        // Vanilla draws a screen's title 15 GUI pixels down; the scale multiplies
        // it like every other number here.
        return HeadingTop * uiScale();
    }

    void drawHeadingScaled(const char *text, TEXTURE &tex, int y, int scale)
    {
        if(scale < 1)
            scale = 1;

        const int x = (SCREEN_WIDTH - static_cast<int>(measureStringScaled(text, scale))) / 2;
        drawStringScaled(text, TextShadow, tex, x + scale, y + scale, scale);
        drawStringScaled(text, Text, tex, x, y, scale);
    }

    void drawSlider(TEXTURE &tex, int x, int y, int w, int h,
                    unsigned int value, unsigned int min, unsigned int max,
                    bool focused, bool enabled)
    {
        if(w <= 0 || h <= 0)
            return;

        // The track is the button art, exactly as vanilla's slider draws it, so a
        // slider and a button share a face and only the handle marks one out.
        drawButton(tex, x, y, w, h, focused, enabled);

        const int handle_w = static_cast<int>(slider_handle.width) * uiScale();
        int travel = w - handle_w;
        if(travel < 0)
            travel = 0;

        unsigned int position = 0;
        if(max > min)
        {
            if(value < min)
                value = min;
            if(value > max)
                value = max;
            // Rounded to the nearest pixel of travel rather than truncated, so a
            // value at the top of its range puts the handle against the right end.
            position = static_cast<unsigned int>((value - min) * static_cast<unsigned int>(travel)
                                                 / (max - min));
        }

        // The widget sheet holds the handle's plain and highlighted states stacked;
        // the row picks the one for the state the row is in.
        const int row = (focused && enabled) ? static_cast<int>(slider_handle.height) / 2 : 0;
        blit(slider_handle, tex, 0, row, slider_handle.width, slider_handle.height / 2,
             x + static_cast<int>(position), y, handle_w, h);
    }

    void drawCheckbox(TEXTURE &tex, int x, int y, int size, bool checked, bool focused,
                      bool enabled)
    {
        if(size <= 0)
            return;

        // checkbox.png is a 2x2 grid of 20x20 cells: the *column* is unchecked or
        // checked (the tick is in the right-hand cells, which have the fewest dark
        // pixels of the four) and the *row* is plain or highlighted.
        (void)enabled;
        const int src_x = checked ? static_cast<int>(checkbox.width) / 2 : 0;
        const int src_y = focused ? static_cast<int>(checkbox.height) / 2 : 0;
        blit(checkbox, tex, src_x, src_y, 20, 20, x, y, size, size);
    }

    void formatOptionLabel(char *out, unsigned int size, const char *name, const char *value)
    {
        if(out == nullptr || size == 0)
            return;
        // "Name: Value", which is how vanilla writes an option's label.
        snprintf(out, size, "%s: %s", name, value);
    }


    void logoScreenSize(int &w, int &h)
    {
        w = title_logo.width * uiScale();
        h = title_logo.height * uiScale();
    }

    void editionScreenSize(int &w, int &h)
    {
        w = edition.width * uiScale();
        h = edition.height * uiScale();
    }

    int buttonBlockHeight(int count)
    {
        if(count <= 0)
            return 0;
        return count * ButtonHeight * uiScale() + (count - 1) * (ButtonPitch - ButtonHeight) * uiScale();
    }

    ButtonColumn buttonColumnAt(int count, int top)
    {
        ButtonColumn column;
        column.count = count < 0 ? 0 : count;
        // Vanilla's button is 200 wide at every scale; on a screen too narrow for
        // it (neither of this engine's two is) it shrinks to fit with a margin.
        column.w = std::min(ButtonWidth * uiScale(), SCREEN_WIDTH - 8);
        column.h = ButtonHeight * uiScale();
        column.gap = (ButtonPitch - ButtonHeight) * uiScale();
        column.x = (SCREEN_WIDTH - column.w) / 2;
        column.top = top;
        return column;
    }

    ButtonColumn centeredButtonColumn(int count, int centre_y)
    {
        return buttonColumnAt(count, centre_y - buttonBlockHeight(count) / 2);
    }

    void TitleLayout::buttonRect(int index, int &x, int &y, int &w, int &h) const
    {
        if(index < TitleFullButtons)
        {
            x = buttons.x;
            y = buttons.buttonY(index);
            w = buttons.w;
            h = buttons.h;
            return;
        }

        y = bottom_y;
        if(index == TitleLeftHalfButton || index == TitleRightHalfButton)
        {
            x = (index == TitleLeftHalfButton) ? bottom_left_x : bottom_right_x;
            w = bottom_w;
            h = bottom_h;
            return;
        }

        // The two 20x20 icon buttons share the row's ends: language to the left
        // of "Options...", accessibility to the right of "Quit Game".
        x = (index == TitleLeftIconButton) ? icon_left_x : icon_right_x;
        w = icon_w;
        h = icon_w;
    }

    TitleLayout titleLayout(const char *splash)
    {
        TitleLayout layout;
        const int s = uiScale();

        layout.logo_w = title_logo.width * s;
        layout.logo_h = title_logo.height * s;
        layout.logo_x = (SCREEN_WIDTH - layout.logo_w) / 2;
        layout.logo_y = TitleLogoTop * s;

        // The "Java Edition" strip is centred like the wordmark and sits at
        // vanilla's own y, over the wordmark's lower band; the splash is centred
        // at (width/2 + 90, 66), which is where vanilla's translated-and-rotated
        // line (its top at local y -8 under a y-70 origin) actually sits.
        layout.edition_w = edition.width * s;
        layout.edition_h = edition.height * s;
        layout.edition_x = (SCREEN_WIDTH - layout.edition_w) / 2;
        layout.edition_y = TitleEditionTop * s;

        // Vanilla's splash scale: 180% measured against the line in GUI units, so
        // the line comes out the same size on the calculator and on a desktop. The
        // outline the sprite carries is part of the width that gets scaled.
        const int drawn_text = static_cast<int>(measureString(splash)) + 2;
        const int unit_w = drawn_text / s;
        layout.splash_scale = 180 * 100 / (unit_w + 32);

        // The blit anchors on the sprite's left edge and its centre line, so the
        // anchor is half the drawn width left of vanilla's centre.
        const int drawn_w = drawn_text * layout.splash_scale / 100;
        layout.splash_x = SCREEN_WIDTH / 2 + TitleSplashOffsetX * s - drawn_w / 2;
        layout.splash_y = TitleSplashCentreY * s;
        // A narrow screen cannot hold the line where vanilla puts it, so it slides
        // left until it fits.
        if(layout.splash_x + drawn_w > SCREEN_WIDTH)
            layout.splash_x = SCREEN_WIDTH - drawn_w;
        if(layout.splash_x < 0)
            layout.splash_x = 0;

        // Vanilla's button block: three full-width buttons, then two half-width
        // ones sharing a row `72 + 12` below the third, at vanilla's own x.
        layout.buttons = buttonColumnAt(TitleFullButtons,
                                        SCREEN_HEIGHT / 4 + TitleButtonOffset * s);
        layout.bottom_w = TitleHalfWidth * s;
        layout.bottom_h = ButtonHeight * s;
        layout.bottom_y = SCREEN_HEIGHT / 4 + (TitleButtonOffset + TitleBottomOffset) * s;
        layout.bottom_left_x = SCREEN_WIDTH / 2 - 100 * s;
        layout.bottom_right_x = SCREEN_WIDTH / 2 + 2 * s;
        layout.icon_w = TitleIconSize * s;
        layout.icon_left_x = SCREEN_WIDTH / 2 + TitleIconLeftOffset * s;
        layout.icon_right_x = SCREEN_WIDTH / 2 + TitleIconRightOffset * s;

        layout.version_x = 2 * s;
        layout.credit_x = SCREEN_WIDTH - static_cast<int>(measureString(creditText)) - 2 * s;
        layout.version_y = SCREEN_HEIGHT - TitleSmallPrintBottom * s;
        return layout;
    }

    void PauseLayout::buttonRect(int index, int &x, int &y, int &w, int &h) const
    {
        h = button_h;

        // The first and last buttons run the full 204 pixels; everything between
        // them is a two-column row, exactly as vanilla lays the pause screen out.
        if(index <= 0 || index >= count - 1)
        {
            const int row = (index <= 0) ? 0 : PauseGridRows + 1;
            x = wide_x;
            y = rowY(row);
            w = wide_w;
            return;
        }

        const int pair = index - 1;
        x = (pair % 2 == 0) ? left_x : right_x;
        y = rowY(1 + pair / 2);
        w = half_w;
    }

    PauseLayout pauseMenuLayout()
    {
        PauseLayout layout;
        layout.scale = uiScale();
        const int s = layout.scale;

        layout.count = pauseLabelCount;
        layout.heading_y = PauseHeadingY * s;
        layout.top = SCREEN_HEIGHT / 4 + PauseButtonTop * s;
        layout.button_h = ButtonHeight * s;

        // Vanilla's own x values: a 204-wide button centred on the screen and the
        // two 98-wide columns at `width / 2 - 102` and `width / 2 + 4`.
        layout.wide_w = std::min(PauseWideWidth * s, SCREEN_WIDTH - 8);
        layout.wide_x = (SCREEN_WIDTH - layout.wide_w) / 2;
        layout.half_w = std::min(PauseHalfWidth * s, SCREEN_WIDTH - 8);
        layout.left_x = SCREEN_WIDTH / 2 + PauseLeftOffset * s;
        layout.right_x = SCREEN_WIDTH / 2 + PauseRightOffset * s;
        return layout;
    }

    int optionRows(int entry_count)
    {
        // Two entries share a row, so an odd setting leaves the row's right half
        // empty rather than adding a row of its own.
        return (entry_count + 1) / 2;
    }

    OptionsLayout optionsLayout(int entry_count, int first_visible)
    {
        OptionsLayout layout;
        const int scale = uiScale();

        layout.title_y = headingY();

        // Vanilla's own grid: two 150-pixel buttons either side of the centre, on
        // the same 24-pixel pitch as the rest of the front-end, the first row at
        // height/6 - 12.
        layout.button_w = OptionsButtonWidth * scale;
        layout.button_h = ButtonHeight * scale;
        layout.pitch = ButtonPitch * scale;
        layout.left_x = SCREEN_WIDTH / 2 - OptionsLeftOffset * scale;
        layout.right_x = SCREEN_WIDTH / 2 + OptionsRightOffset * scale;
        layout.first_row_y = SCREEN_HEIGHT / 6 + OptionsTopOffset * scale;

        layout.rows = optionRows(entry_count);

        // The "Done" button sits on the bottom edge, as vanilla's does, and the
        // grid has to stop above it.
        layout.done_w = std::min(OptionsDoneWidth * scale, SCREEN_WIDTH - 8);
        layout.done_h = ButtonHeight * scale;
        layout.done_x = (SCREEN_WIDTH - layout.done_w) / 2;
        layout.done_y = SCREEN_HEIGHT - layout.done_h - 4 * scale;

        int room = layout.done_y - 4 * scale - layout.first_row_y;
        if(room < 0)
            room = 0;
        layout.visible_rows = room / layout.pitch;
        if(layout.visible_rows < 1)
            layout.visible_rows = 1;

        int scroll = first_visible;
        const int last_scroll = layout.rows - layout.visible_rows;
        if(scroll > last_scroll)
            scroll = last_scroll;
        if(scroll < 0)
            scroll = 0;
        layout.first_visible = scroll;
        return layout;
    }

    int optionsScrollFor(int row, int visible_rows, int total_rows, int current_scroll)
    {
        if(visible_rows < 1 || total_rows <= visible_rows)
            return 0;

        const int last_scroll = total_rows - visible_rows;
        int scroll = current_scroll;

        // Move as little as possible: the grid does not jump while the selection
        // stays inside the window, and steps by a row when it leaves it.
        if(row < scroll)
            scroll = row;
        else if(row >= scroll + visible_rows)
            scroll = row - visible_rows + 1;

        if(scroll > last_scroll)
            scroll = last_scroll;
        if(scroll < 0)
            scroll = 0;
        return scroll;
    }

    void WorldSelectLayout::buttonRect(int action, int &x, int &y, int &w, int &h) const
    {
        h = button_h;
        if(action == WorldPlay || action == WorldCreate)
        {
            x = (action == WorldPlay) ? row1_left_x : row1_right_x;
            y = row1_y;
            w = row1_w;
            return;
        }

        const int index = action - WorldEdit; // the four second-row buttons
        x = row2_x[index >= 0 && index < 4 ? index : 0];
        y = row2_y;
        w = row2_w;
    }

    WorldSelectLayout worldSelectLayout()
    {
        WorldSelectLayout layout;
        const int s = uiScale();
        layout.scale = s;

        layout.heading_y = WorldHeadingY * s;
        layout.search_x = SCREEN_WIDTH / 2 + WorldSearchOffsetX * s;
        layout.search_y = WorldSearchY * s;
        layout.search_w = WorldSearchWidth * s;
        layout.search_h = WorldSearchHeight * s;
        layout.list_top = WorldListTop * s;
        layout.list_bottom = SCREEN_HEIGHT - WorldListBottomOffset * s;

        // Vanilla's `getRowLeft`/`getRowWidth`: a 270-wide row whose left edge is
        // two pixels right of the list's centre line's start.
        layout.row_w = WorldRowWidth * s;
        layout.row_left = SCREEN_WIDTH / 2 - layout.row_w / 2 + WorldRowLeftOffset * s;
        layout.row_pitch = WorldRowPitch * s;
        layout.row_h = WorldRowHeight * s;

        // The selection frame runs two pixels around the row's content box.
        layout.box_x = layout.row_left - 2 * s;
        layout.box_w = layout.row_w + 4 * s;

        layout.icon_size = WorldIconSize * s;
        layout.text_x = layout.row_left + WorldRowTextOffset * s;

        layout.button_h = ButtonHeight * s;
        layout.row1_y = SCREEN_HEIGHT - WorldRow1YOffset * s;
        layout.row2_y = SCREEN_HEIGHT - WorldRow2YOffset * s;
        layout.row1_w = WorldRow1Width * s;
        layout.row1_left_x = SCREEN_WIDTH / 2 + WorldRow1LeftOffset * s;
        layout.row1_right_x = SCREEN_WIDTH / 2 + WorldRow1RightOffset * s;
        layout.row2_w = WorldRow2Width * s;
        for(int i = 0; i < 4; ++i)
            layout.row2_x[i] = SCREEN_WIDTH / 2 + WorldRow2Offsets[i] * s;
        return layout;
    }

    void WorldFormLayout::optionRect(int index, int &x, int &y, int &w, int &h) const
    {
        x = (index == 0) ? left_x : right_x;
        y = option_y;
        w = option_w;
        h = button_h;
    }

    void WorldFormLayout::bottomRect(int index, int &x, int &y, int &w, int &h) const
    {
        x = (index == 0) ? left_x : right_x;
        y = bottom_y;
        w = option_w;
        h = button_h;
    }

    WorldFormLayout worldFormLayout()
    {
        WorldFormLayout layout;
        const int s = uiScale();
        layout.scale = s;

        layout.heading_y = FormHeadingY * s;
        layout.label_y = FormLabelY * s;
        layout.field_w = FormFieldWidth * s;
        layout.field_h = FormFieldHeight * s;
        layout.field_x = SCREEN_WIDTH / 2 - layout.field_w / 2;
        layout.field_y = FormFieldY * s;
        layout.result_y = FormResultY * s;
        layout.option_y = FormOptionY * s;
        layout.help_y = FormHelpY * s;
        layout.help_pitch = FormHelpPitch * s;
        layout.option_w = FormButtonWidth * s;
        layout.button_h = ButtonHeight * s;
        layout.left_x = SCREEN_WIDTH / 2 + FormLeftOffset * s;
        layout.right_x = SCREEN_WIDTH / 2 + FormRightOffset * s;
        layout.bottom_y = SCREEN_HEIGHT - FormBottomYOffset * s;
        return layout;
    }

    void ConfirmLayout::buttonRect(int index, int &x, int &y, int &w, int &h) const
    {
        x = (index == 0) ? left_x : right_x;
        y = button_y;
        w = button_w;
        h = button_h;
    }

    ConfirmLayout confirmLayout(int message_lines)
    {
        ConfirmLayout layout;
        const int s = uiScale();
        layout.scale = s;

        layout.title_y = ConfirmTitleY * s;
        layout.message_y = ConfirmMessageY * s;
        layout.line_pitch = ConfirmLinePitch * s;
        if(message_lines < 1)
            message_lines = 1;

        // Vanilla's own clamp: the buttons sit a line-and-a-half under the
        // message, but never above a sixth of the screen nor below its bottom.
        const int wanted = (ConfirmMessageY + message_lines * ConfirmLinePitch + 12) * s;
        const int above = SCREEN_HEIGHT / 6 + ConfirmButtonYBase * s;
        const int below = SCREEN_HEIGHT - ConfirmButtonYMax * s;
        layout.button_y = wanted < above ? above : (wanted > below ? below : wanted);

        layout.button_w = ConfirmButtonWidth * s;
        layout.button_h = ButtonHeight * s;
        layout.left_x = SCREEN_WIDTH / 2 + ConfirmLeftOffset * s;
        layout.right_x = SCREEN_WIDTH / 2 + ConfirmRightOffset * s;
        return layout;
    }

    void drawMenuBackground(TEXTURE &tex)
    {
        // Vanilla tiles `options_background.png` at 32 GUI pixels a tile -- the
        // 16-pixel source doubled -- and tints it a quarter grey. nGL cannot blend,
        // so the tint is baked in by dimming the dirt after it is laid down.
        const int tile = static_cast<int>(menu_background.width) * 2 * uiScale();
        if(tile <= 0)
            return;

        for(int y = 0; y < static_cast<int>(tex.height); y += tile)
            for(int x = 0; x < static_cast<int>(tex.width); x += tile)
                blit(menu_background, tex, 0, 0, menu_background.width, menu_background.height,
                     x, y, tile, tile);

        // A quarter brightness is what the menus sit on, and it is also what keeps
        // the white labels readable on the dirt.
        shadeRect(tex, 0, 0, tex.width, tex.height, 25);
    }

    LoadingLayout loadingLayout()
    {
        LoadingLayout layout;
        const int scale = uiScale();

        // The label sits just above the middle line and the bar just below it, so
        // the pair is centred on the screen as a whole.
        layout.label_y = SCREEN_HEIGHT / 2 - static_cast<int>(fontHeight()) - 4 * scale;

        layout.bar_h = LoadingBarHeight * scale;
        layout.bar_w = LoadingBarWidth * scale;
        const int max_w = SCREEN_WIDTH - 40 * scale;
        if(layout.bar_w > max_w)
            layout.bar_w = max_w;
        if(layout.bar_w < 4 * scale)
            layout.bar_w = 4 * scale;
        layout.bar_x = (SCREEN_WIDTH - layout.bar_w) / 2;
        layout.bar_y = SCREEN_HEIGHT / 2 + 4 * scale;
        return layout;
    }

    DeathLayout deathLayout()
    {
        DeathLayout layout;
        const int scale = uiScale();

        // Vanilla draws the title inside a 2x matrix at the GUI y 30, so it lands
        // 60 GUI pixels down, and the death cause and the score follow it at 85
        // and 100. The buttons start at `height / 4 + 72`, the same two rows the
        // screen has always used.
        layout.title_scale = 2;
        layout.title_y = 30 * scale * layout.title_scale;
        layout.message_y = 85 * scale;
        layout.score_y = 100 * scale;

        layout.buttons = buttonColumnAt(2, SCREEN_HEIGHT / 4 + 72 * scale);
        return layout;
    }

    void drawDeathOverlay(TEXTURE &tex)
    {
        // Vanilla's two gradient ends: (alpha 0x60, red 0x50) at the top and
        // (alpha 0xA0, red 0x80) at the bottom, with the green and blue channels
        // left at zero.
        const int top_alpha = 0x60, bottom_alpha = 0xA0;
        const int top_red = 0x50, bottom_red = 0x80;

        const int height = static_cast<int>(tex.height);
        const int span = height > 1 ? height - 1 : 1;

        // The gradient steps through a handful of alpha/red pairs down the
        // screen; each pair is one map, shared by the rows it covers.
        MixTables mix;
        int mix_keep = -1, mix_red = -1;

        for(int y = 0; y < height; ++y)
        {
            const int alpha = top_alpha + (bottom_alpha - top_alpha) * y / span;
            const int red8 = top_red + (bottom_red - top_red) * y / span;
            const int keep = 255 - alpha;
            if(keep != mix_keep || red8 != mix_red)
            {
                mix.build(keep, red8 * alpha / 255, 0, 0);
                mix_keep = keep;
                mix_red = red8;
            }

            COLOR *line = tex.bitmap + y * tex.width;
            for(int x = 0; x < static_cast<int>(tex.width); ++x)
                line[x] = mix.mix(line[x]);
        }
    }

    void drawPauseOverlay(TEXTURE &tex)
    {
        // `Screen.renderBackground` fills the pause screen with a vertical gradient
        // from ARGB 0xC0101010 to 0xD0101010: a dark grey whose opacity grows from
        // about 75% at the top to 81% at the bottom. nGL cannot blend, so every
        // pixel is mixed toward that grey by the gradient's own alpha.
        const int top_alpha = 0xC0, bottom_alpha = 0xD0;
        const int wash = 0x10;

        const int height = static_cast<int>(tex.height);
        const int span = height > 1 ? height - 1 : 1;

        // The gradient's alpha steps only a handful of times down the screen;
        // each step is one map, shared by the rows it covers.
        MixTables mix;
        int mix_keep = -1;

        for(int y = 0; y < height; ++y)
        {
            const int alpha = top_alpha + (bottom_alpha - top_alpha) * y / span;
            const int keep = 255 - alpha;
            if(keep != mix_keep)
            {
                const int add = wash * alpha / 255;
                mix.build(keep, add, add, add);
                mix_keep = keep;
            }

            COLOR *line = tex.bitmap + y * tex.width;
            for(int x = 0; x < static_cast<int>(tex.width); ++x)
                line[x] = mix.mix(line[x]);
        }
    }

    void drawLoadingScreen(TEXTURE &tex, const char *message, int percent)
    {
        drawMenuBackground(tex);

        const LoadingLayout layout = loadingLayout();

        drawHeading(message, tex, layout.label_y);

        if(percent < 0)
            percent = 0;
        if(percent > 100)
            percent = 100;

        const int border = LoadingBarBorder * uiScale();

        // A frame, a dark track and a white fill: the shape vanilla's own progress
        // bar has, drawn as colour because the sheet it uses is not in this tree.
        fillRect(tex, layout.bar_x, layout.bar_y, layout.bar_w, layout.bar_h, LoadingBarFrame);

        const int track_x = layout.bar_x + border;
        const int track_y = layout.bar_y + border;
        const int track_w = layout.bar_w - 2 * border;
        const int track_h = layout.bar_h - 2 * border;
        if(track_w <= 0 || track_h <= 0)
            return;

        fillRect(tex, track_x, track_y, track_w, track_h, LoadingBarTrack);

        const int filled = track_w * percent / 100;
        if(filled > 0)
            fillRect(tex, track_x, track_y, filled, track_h, LoadingBarFill);
    }
}
