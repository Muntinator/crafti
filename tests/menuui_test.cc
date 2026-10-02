// Host test for the front-end layout (menuui.cpp).
//
// The menus are drawn, so most of MenuUI cannot be checked without a screen, but
// the one thing that goes wrong silently is geometry: a label wider than its
// button, a wordmark that overflows a 320-pixel screen, a column of buttons that
// runs into the small print at the bottom. The calculator screen is half the
// desktop's in each direction, so anything that fits on one has to be checked on
// the other -- which is why this file is built twice by the tests Makefile, once
// with _TINSPIRE (320x240) and once without (640x480).
//
// The layout itself is MenuUI's: this test measures the module's own strings and
// its own artwork against the module's own boxes, so a label that is lengthened
// without moving the buttons fails here rather than on the calculator.
//
// It is also what pins the port of the port: the button width, the pitch, the
// first button's height and the font's own cell size are the numbers that make
// this front-end vanilla's rather than merely similar to it.
//
// Build and run with `make -C tests`.

#include "menuui.h"

#include <stdio.h>
#include <string.h>
#include <vector>

#include "font.h"

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

using namespace MenuUI;

/** A label has to leave a pixel of gradient either side of the text. */
static void check_labels_fit(const ButtonColumn &column, const char *const labels[], int count,
                             const char *what)
{
    CHECK(count == column.count);
    for(int i = 0; i < count; ++i)
    {
        CHECK(labels[i] != nullptr && labels[i][0] != '\0');

        const int width = static_cast<int>(measureString(labels[i]));
        if(width + 8 > column.w)
            printf("    %s label %d (\"%s\") is %d wide in a %d button\n",
                   what, i, labels[i], width, column.w);
        CHECK(width + 8 <= column.w);
    }
}

static void test_font()
{
    // Vanilla's font, at the GUI scale: 8 pixels on the calculator, 16 on the
    // desktop, which is the same 8-pixel cell doubled.
    CHECK(fontHeight() == 8u * uiScale());

    // Every printable ASCII character has to advance the pen by something, and no
    // glyph may be wider than its cell.
    for(int c = 0x20; c < 0x7F; ++c)
    {
        char text[2] = { static_cast<char>(c), '\0' };
        const int width = static_cast<int>(measureString(text));
        CHECK(width > 0 && width <= static_cast<int>(fontHeight()));
    }
    CHECK(measureString(" ") > 0);
    CHECK(measureString("") == 0);
}

static void test_button_boxes()
{
    CHECK(uiScale() >= 1);
    CHECK(SCREEN_WIDTH == 320 * uiScale());
    CHECK(SCREEN_HEIGHT == 240 * uiScale());

    // Vanilla's button, at the scale the screen runs at: 200x20 on a 24-pixel
    // pitch, centred.
    const ButtonColumn column = centeredButtonColumn(4, SCREEN_HEIGHT / 2);
    CHECK(column.w == ButtonWidth * uiScale());
    CHECK(column.h == ButtonHeight * uiScale());
    CHECK(column.gap == (ButtonPitch - ButtonHeight) * uiScale());
    CHECK(column.buttonY(1) - column.buttonY(0) == ButtonPitch * uiScale());
    CHECK(column.h >= static_cast<int>(fontHeight()) + 2);
    CHECK(column.w > 0 && column.w <= SCREEN_WIDTH);
    CHECK(column.x >= 0 && column.x + column.w <= SCREEN_WIDTH);
    CHECK(column.gap > 0);
    CHECK(column.count == 4);
    CHECK(column.buttonY(0) == column.top);
    CHECK(column.bottom() == column.buttonY(3) + column.h);

    // The column is centred on the line it was asked for.
    CHECK(column.top + buttonBlockHeight(4) / 2 == SCREEN_HEIGHT / 2);

    // A column placed by its first button starts exactly there.
    const ButtonColumn placed = buttonColumnAt(3, 10);
    CHECK(placed.top == 10);
    CHECK(placed.count == 3);
    CHECK(placed.bottom() == 10 + buttonBlockHeight(3));

    // An empty column has no height and no buttons to draw.
    const ButtonColumn empty = centeredButtonColumn(0, SCREEN_HEIGHT / 2);
    CHECK(empty.count == 0);
    CHECK(empty.bottom() == empty.top);
    CHECK(buttonBlockHeight(0) == 0);
}

static void test_title_layout()
{
    const TitleLayout layout = titleLayout(splashLines[0]);

    // Vanilla's title button block: three full-width buttons, then two half-width
    // ones on a shared row. Every label has to fit its own box, which is not the
    // same width for all of them.
    CHECK(layout.buttons.count == TitleFullButtons);
    CHECK(layout.buttons.x >= 0 && layout.buttons.x + layout.buttons.w <= SCREEN_WIDTH);
    CHECK(layout.buttons.w == ButtonWidth * uiScale());

    int bottom_right_x = 0, bottom_right_y = 0, bottom_right_w = 0, bottom_right_h = 0;
    layout.buttonRect(titleLabelCount - 1, bottom_right_x, bottom_right_y,
                      bottom_right_w, bottom_right_h);
    for(int i = 0; i < titleLabelCount; ++i)
    {
        int x = 0, y = 0, w = 0, h = 0;
        layout.buttonRect(i, x, y, w, h);
        CHECK(x >= 0 && x + w <= SCREEN_WIDTH);
        CHECK(y >= 0 && y + h <= SCREEN_HEIGHT);
        const int width = static_cast<int>(measureString(titleLabels[i]));
        if(width + 8 > w)
            printf("    title label %d (\"%s\") is %d wide in a %d button\n",
                   i, titleLabels[i], width, w);
        CHECK(width + 8 <= w);
    }

    // Vanilla's first title button, and vanilla's own bottom row under it.
    CHECK(layout.buttons.top == SCREEN_HEIGHT / 4 + TitleButtonOffset * uiScale());
    CHECK(bottom_right_y == SCREEN_HEIGHT / 4 + (TitleButtonOffset + TitleBottomOffset) * uiScale());
    CHECK(bottom_right_w == TitleHalfWidth * uiScale());
    CHECK(bottom_right_h == ButtonHeight * uiScale());

    int left_x = 0, left_y = 0, left_w = 0, left_h = 0;
    layout.buttonRect(TitleFullButtons, left_x, left_y, left_w, left_h);
    CHECK(left_x == SCREEN_WIDTH / 2 - 100 * uiScale());
    CHECK(bottom_right_x == SCREEN_WIDTH / 2 + 2 * uiScale());
    CHECK(left_y == bottom_right_y);
    CHECK(left_w == bottom_right_w && left_h == bottom_right_h);

    // The wordmark is the official image, scaled with the screen, centred, and it
    // has to end above the buttons.
    int logo_w = 0, logo_h = 0;
    logoScreenSize(logo_w, logo_h);
    CHECK(layout.logo_w == logo_w && layout.logo_h == logo_h);
    CHECK(logo_w > 0 && logo_h > 0);
    CHECK(layout.logo_x == (SCREEN_WIDTH - layout.logo_w) / 2);
    CHECK(layout.logo_x >= 0 && layout.logo_x + layout.logo_w <= SCREEN_WIDTH);
    CHECK(layout.logo_y == TitleLogoTop * uiScale());
    CHECK(layout.logo_y + layout.logo_h <= layout.buttons.top);

    // The edition strip sits at vanilla's own y, over the wordmark's lower band,
    // and shares the top of the screen with it.
    int edition_w = 0, edition_h = 0;
    editionScreenSize(edition_w, edition_h);
    CHECK(layout.edition_w == edition_w && layout.edition_h == edition_h);
    CHECK(layout.edition_x == (SCREEN_WIDTH - layout.edition_w) / 2);
    CHECK(layout.edition_x >= 0 && layout.edition_x + layout.edition_w <= SCREEN_WIDTH);
    CHECK(layout.edition_y == TitleEditionTop * uiScale());
    CHECK(layout.edition_y + layout.edition_h <= layout.buttons.top);

    // The splash is centred at vanilla's point (width/2 + 90, 66), tilted, and it
    // has to fit on the screen: that is what the anchor is slid left for.
    CHECK(layout.splash_y == TitleSplashCentreY * uiScale());
    CHECK(layout.splash_y < layout.buttons.top);
    CHECK(layout.splash_scale > 0);
    CHECK(layout.splash_x >= 0);

    // The version sits on the bottom line, inside the screen.
    CHECK(layout.version_x == 2 * uiScale());
    CHECK(layout.version_y == SCREEN_HEIGHT - TitleSmallPrintBottom * uiScale());
    CHECK(layout.version_y + static_cast<int>(fontHeight()) <= SCREEN_HEIGHT);
    CHECK(layout.version_y > layout.buttons.bottom());

    // The version and the credit share the bottom line without touching, and the
    // credit still ends inside the screen.
    const int version_w = static_cast<int>(measureString(versionText));
    const int credit_w = static_cast<int>(measureString(creditText));
    CHECK(layout.credit_x == SCREEN_WIDTH - credit_w - 2 * uiScale());
    CHECK(layout.credit_x >= layout.version_x + version_w);
    CHECK(version_w + credit_w + 8 <= SCREEN_WIDTH);
    CHECK(credit_w + 1 <= SCREEN_WIDTH);

    printf("    title: buttons x%d..%d y%d..%d + 2 half x%d/%d y%d, logo %d,%d %dx%d, edition %d,%d %dx%d\n",
           layout.buttons.x, layout.buttons.x + layout.buttons.w,
           layout.buttons.top, layout.buttons.bottom(),
           left_x, bottom_right_x, bottom_right_y,
           layout.logo_x, layout.logo_y, layout.logo_w, layout.logo_h,
           layout.edition_x, layout.edition_y, layout.edition_w, layout.edition_h);
    printf("    splash: anchor %d,%d scale %d%% of %d wide\n",
           layout.splash_x, layout.splash_y, layout.splash_scale,
           static_cast<int>(measureString(splashLines[0])));

    CHECK(titleLabels[0] != nullptr);
    CHECK(pauseHeading[0] != '\0');

    // Every splash line has to fit the screen when the layout is built for it,
    // together with the outline it is drawn with.
    for(int i = 0; i < splashLineCount; ++i)
    {
        const TitleLayout splash_layout = titleLayout(splashLines[i]);
        const int drawn_w = (static_cast<int>(measureString(splashLines[i])) + 2)
                            * splash_layout.splash_scale / 100;
        if(splash_layout.splash_x + drawn_w > SCREEN_WIDTH)
            printf("    splash %d (\"%s\") is %d wide from x=%d\n",
                   i, splashLines[i], drawn_w, splash_layout.splash_x);
        CHECK(splash_layout.splash_x >= 0);
        CHECK(splash_layout.splash_x + drawn_w <= SCREEN_WIDTH);
    }
}

static void test_pause_layout()
{
    const PauseLayout layout = pauseMenuLayout();
    const int scale = uiScale();

    CHECK(layout.count == pauseLabelCount);
    CHECK(layout.top == SCREEN_HEIGHT / 4 + PauseButtonTop * scale);
    CHECK(layout.bottom() <= SCREEN_HEIGHT);

    // Vanilla's grid: a 204-pixel button, three rows of two 98-pixel buttons at
    // `width / 2 - 102` and `width / 2 + 4`, then a 204-pixel button, every one 20
    // high on a 24-pixel pitch.
    CHECK(layout.wide_w == PauseWideWidth * scale);
    CHECK(layout.wide_x == (SCREEN_WIDTH - layout.wide_w) / 2);
    CHECK(layout.wide_x == SCREEN_WIDTH / 2 + PauseLeftOffset * scale);
    CHECK(layout.half_w == PauseHalfWidth * scale);
    CHECK(layout.left_x == SCREEN_WIDTH / 2 + PauseLeftOffset * scale);
    CHECK(layout.right_x == SCREEN_WIDTH / 2 + PauseRightOffset * scale);
    CHECK(layout.button_h == ButtonHeight * scale);
    CHECK(layout.left_x + layout.half_w < layout.right_x);

    // Every button fits the screen and every label fits its own box, which is not
    // the same width for the full-width buttons and the pairs.
    for(int i = 0; i < pauseLabelCount; ++i)
    {
        int x = 0, y = 0, w = 0, h = 0;
        layout.buttonRect(i, x, y, w, h);
        CHECK(x >= 0 && x + w <= SCREEN_WIDTH);
        CHECK(y >= 0 && y + h <= SCREEN_HEIGHT);
        CHECK(h == layout.button_h);
        const int width = static_cast<int>(measureString(pauseLabels[i]));
        if(width + 8 > w)
            printf("    pause label %d (\"%s\") is %d wide in a %d button\n",
                   i, pauseLabels[i], width, w);
        CHECK(width + 8 <= w);
    }

    // The first and last entries are the wide ones, at the first row and the last.
    int x0 = 0, y0 = 0, w0 = 0, h0 = 0, xl = 0, yl = 0, wl = 0, hl = 0;
    layout.buttonRect(0, x0, y0, w0, h0);
    layout.buttonRect(pauseLabelCount - 1, xl, yl, wl, hl);
    CHECK(w0 == layout.wide_w && wl == layout.wide_w);
    CHECK(y0 == layout.top);
    CHECK(yl == layout.rowY(PauseGridRows + 1));
    CHECK(layout.bottom() == yl + layout.button_h);

    // The pairs step down a row every two buttons and share each row's y.
    for(int i = 1; i + 1 < pauseLabelCount - 1; i += 2)
    {
        int ax = 0, ay = 0, aw = 0, ah = 0, bx = 0, by = 0, bw = 0, bh = 0;
        layout.buttonRect(i, ax, ay, aw, ah);
        layout.buttonRect(i + 1, bx, by, bw, bh);
        CHECK(ay == by);
        CHECK(aw == layout.half_w && bw == layout.half_w);
        CHECK(ax == layout.left_x && bx == layout.right_x);
        if(i > 1)
        {
            int px = 0, py = 0, pw = 0, ph = 0;
            layout.buttonRect(i - 1, px, py, pw, ph);
            CHECK(ay - py == PauseRowPitch * scale);
        }
    }

    // The heading the pause screen draws above the grid has to clear it.
    CHECK(layout.heading_y == PauseHeadingY * scale);
    CHECK(layout.heading_y + static_cast<int>(fontHeight()) <= layout.top);

    printf("    pause: wide x%d..%d y%d, pairs x%d/%d w%d y%d..%d, heading y%d\n",
           layout.wide_x, layout.wide_x + layout.wide_w, layout.top,
           layout.left_x, layout.right_x, layout.half_w,
           layout.rowY(1), layout.rowY(PauseGridRows), layout.heading_y);

    // The order mirrors vanilla 1.17.1's pause screen, with this game's screens
    // standing in where vanilla has none: Help for "Advancements", Block List for
    // "Statistics", and this game's own Save World, Sound Test and Player
    // Inventory where vanilla keeps Send Feedback, Report Bugs and Share to LAN.
    static const char *const expected_pause_labels[] = {
        "Back to Game", "Help", "Block List", "Save World", "Sound Test...",
        "Options...", "Player Inventory", "Save and Quit to Title"
    };
    CHECK(pauseLabelCount == 8);
    CHECK(static_cast<int>(sizeof(expected_pause_labels) / sizeof(expected_pause_labels[0]))
          == pauseLabelCount);
    for(int i = 0; i < pauseLabelCount; ++i)
        CHECK(strcmp(pauseLabels[i], expected_pause_labels[i]) == 0);
}

// The options screen's grid. The entries' own names and values live in
// settingstask.cpp; what is checked here is the shape of the grid they are drawn
// into and that a vanilla "Name: Value" label fits a 150-pixel row.
static void test_options_layout()
{
    const int scale = uiScale();
    const int entries = 19; // settingstask's own count, including the GUI scale

    const OptionsLayout layout = optionsLayout(entries, 0);

    // Two 150-pixel columns of 20-pixel widgets on vanilla's 24-pixel pitch.
    CHECK(layout.button_w == OptionsButtonWidth * scale);
    CHECK(layout.button_h == ButtonHeight * scale);
    CHECK(layout.pitch == ButtonPitch * scale);
    CHECK(layout.rows == optionRows(entries));
    CHECK(layout.rows == (entries + 1) / 2);

    CHECK(layout.right_x > layout.left_x);
    CHECK(layout.left_x >= 0);
    CHECK(layout.left_x + layout.button_w <= SCREEN_WIDTH);
    CHECK(layout.right_x + layout.button_w <= SCREEN_WIDTH);
    CHECK(layout.columnX(0) == layout.left_x);
    CHECK(layout.columnX(1) == layout.right_x);

    // Vanilla's first row, and the "Done" button under the grid.
    CHECK(layout.first_row_y == SCREEN_HEIGHT / 6 + OptionsTopOffset * scale);
    CHECK(layout.done_w == OptionsDoneWidth * scale);
    CHECK(layout.done_h == ButtonHeight * scale);
    CHECK(layout.done_x == (SCREEN_WIDTH - layout.done_w) / 2);
    CHECK(layout.done_x >= 0 && layout.done_x + layout.done_w <= SCREEN_WIDTH);
    CHECK(layout.done_y + layout.done_h <= SCREEN_HEIGHT);
    CHECK(layout.done_y > layout.first_row_y);

    // The window shows as many rows as fit between the first row and "Done", and
    // no more than the grid has.
    CHECK(layout.visible_rows >= 1);
    CHECK(layout.visible_rows <= layout.rows);
    CHECK(layout.rowY(layout.visible_rows - 1) + layout.button_h <= layout.done_y);
    CHECK(layout.first_visible == 0);
    CHECK(layout.rowVisible(0));
    CHECK(!layout.rowVisible(layout.rows));
    CHECK(!layout.rowVisible(layout.first_visible - 1));

    // A request to scroll past the end is clamped, so the last row is still on
    // screen.
    const OptionsLayout scrolled = optionsLayout(entries, 1000);
    CHECK(scrolled.first_visible == scrolled.rows - scrolled.visible_rows);
    CHECK(scrolled.rowVisible(scrolled.rows - 1));

    // Every "Name: Value" label the screen can build fits its row. The names are
    // settingstask's; the longest value of each kind is paired with it.
    const char *const row_labels[] = {
        "Leaves: Transparent", "Speed: Normal", "Distance: 9", "Fast mode: Off",
        "Near plane: 512", "World: Static (no ticks)", "Show FPS: Off",
        "Block indicator: Off", "Coord indicator: Off", "Audio master: 100%",
        "Music volume: 100%", "Effects volume: 100%", "Ambience volume: 100%",
        "GPIO4 audio: Off", "Villages: Common", "Day/night: On",
        "Day length: 20 min", "Weather: On", "GUI scale: Auto"
    };
    CHECK(static_cast<int>(sizeof(row_labels) / sizeof(row_labels[0])) == entries);

    const int toggle_box = 20 * scale;
    const int toggle_gap = 4 * scale;
    const int toggle_label_w = layout.button_w - toggle_box - toggle_gap;
    CHECK(toggle_label_w > 0);

    for(int i = 0; i < entries; ++i)
    {
        const int width = static_cast<int>(measureString(row_labels[i]));
        if(width + 8 > layout.button_w)
            printf("    option \"%s\" is %d wide in a %d row\n",
                   row_labels[i], width, layout.button_w);
        CHECK(width + 8 <= layout.button_w);

        // An on/off row keeps a checkbox at its right, so its label has less room.
        if(i == 3 || i == 6 || i == 7 || i == 8 || i == 13 || i == 15 || i == 17)
            CHECK(width + 4 <= toggle_label_w);
    }

    // formatOptionLabel is vanilla's "Name: Value".
    char formatted[64];
    formatOptionLabel(formatted, sizeof(formatted), "Fast mode", "On");
    CHECK(strcmp(formatted, "Fast mode: On") == 0);

    printf("    options: %d entries, %d rows, window %d, left x%d, right x%d, done y%d\n",
           entries, layout.rows, layout.visible_rows, layout.left_x, layout.right_x,
           layout.done_y);
}

static void test_options_scrolling()
{
    CHECK(optionsScrollFor(0, 3, 10, 5) == 0);   // above the window: jump to the top
    CHECK(optionsScrollFor(5, 3, 10, 0) == 3);   // below it: step down one
    CHECK(optionsScrollFor(4, 3, 10, 3) == 3);   // inside it: do not move
    CHECK(optionsScrollFor(2, 3, 10, 5) == 2);   // above it: follow the selection
    CHECK(optionsScrollFor(9, 3, 10, 0) == 7);   // the last row: clamp to the end
    CHECK(optionsScrollFor(4, 10, 10, 0) == 0);  // everything fits: never scroll
    CHECK(optionsScrollFor(0, 1, 10, 9) == 0);
}

static void test_loading_screen()
{
    const LoadingLayout layout = loadingLayout();
    const int border = LoadingBarBorder * uiScale();

    CHECK(loadingLabel[0] != '\0');
    CHECK(layout.label_y >= 0);
    CHECK(layout.label_y + static_cast<int>(fontHeight()) <= SCREEN_HEIGHT);
    CHECK(layout.label_y + static_cast<int>(fontHeight()) <= layout.bar_y);
    CHECK(layout.bar_w > 0 && layout.bar_w <= SCREEN_WIDTH);
    CHECK(layout.bar_h > border * 2);
    CHECK(layout.bar_x >= 0 && layout.bar_x + layout.bar_w <= SCREEN_WIDTH);
    CHECK(layout.bar_y + layout.bar_h <= SCREEN_HEIGHT);

    // Draw it for real: menuui.cpp's drawing is all integer work on a texture, so
    // the host can render the screen and read the pixels back.
    std::vector<COLOR> pixels(static_cast<size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT, 0);
    TEXTURE canvas;
    canvas.width = SCREEN_WIDTH;
    canvas.height = SCREEN_HEIGHT;
    canvas.has_transparency = false;
    canvas.transparent_color = 0;
    canvas.bitmap = pixels.data();

    const int track_x = layout.bar_x + border;
    const int track_y = layout.bar_y + border;
    const int track_w = layout.bar_w - 2 * border;
    const int right_inner = layout.bar_x + layout.bar_w - border - 1;

    drawLoadingScreen(canvas, loadingLabel, 50);

    // The dirt backdrop covers the whole screen, not just the middle.
    CHECK(pixels[0] != 0);
    CHECK(pixels[SCREEN_WIDTH - 1] != 0);
    CHECK(pixels[(SCREEN_HEIGHT - 1) * SCREEN_WIDTH] != 0);
    CHECK(pixels[(SCREEN_HEIGHT - 1) * SCREEN_WIDTH + SCREEN_WIDTH - 1] != 0);

    // The bar's frame, its dark track, and a white fill from the left: half full
    // leaves the left end filled and the right end on the track.
    CHECK(pixels[layout.bar_y * SCREEN_WIDTH + layout.bar_x] == LoadingBarFrame);
    CHECK(pixels[track_y * SCREEN_WIDTH + track_x] == LoadingBarFill);
    CHECK(pixels[track_y * SCREEN_WIDTH + track_x + track_w / 2] == LoadingBarTrack);
    CHECK(pixels[track_y * SCREEN_WIDTH + right_inner] == LoadingBarTrack);

    // The label is drawn between the top of the screen and the bar.
    int label_pixels = 0;
    for(int y = layout.label_y; y < layout.label_y + static_cast<int>(fontHeight()); ++y)
        for(int x = 0; x < SCREEN_WIDTH; ++x)
            if(pixels[y * SCREEN_WIDTH + x] == Text)
                ++label_pixels;
    CHECK(label_pixels > 0);

    // Empty and full are the two ends of the bar.
    drawLoadingScreen(canvas, loadingLabel, 0);
    CHECK(pixels[track_y * SCREEN_WIDTH + track_x] == LoadingBarTrack);
    drawLoadingScreen(canvas, loadingLabel, 100);
    CHECK(pixels[track_y * SCREEN_WIDTH + right_inner] == LoadingBarFill);

    printf("    loading: label y%d, bar %d,%d %dx%d\n",
           layout.label_y, layout.bar_x, layout.bar_y, layout.bar_w, layout.bar_h);
}

/** Counts the pixels of a given colour in a canvas. */
static int count_colour(const std::vector<COLOR> &pixels, COLOR colour)
{
    int n = 0;
    for(size_t i = 0; i < pixels.size(); ++i)
        if(pixels[i] == colour)
            ++n;
    return n;
}

static void test_scaled_font()
{
    CHECK(measureStringScaled("", 1) == 0);
    CHECK(measureStringScaled("abc", 0) == measureString("abc"));
    CHECK(measureStringScaled("You Died!", 2) == measureString("You Died!") * 2);

    // The doubled ink is exactly four times the plain ink: every source pixel is
    // written as a 2x2 block.
    std::vector<COLOR> plain(static_cast<size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT, 0);
    std::vector<COLOR> doubled(plain.size(), 0);
    TEXTURE a, b;
    a.width = b.width = SCREEN_WIDTH;
    a.height = b.height = SCREEN_HEIGHT;
    a.has_transparency = b.has_transparency = false;
    a.transparent_color = b.transparent_color = 0;
    a.bitmap = plain.data();
    b.bitmap = doubled.data();

    drawStringScaled("You Died!", Text, a, 4, 4, 1);
    drawStringScaled("You Died!", Text, b, 4, 4, 2);
    CHECK(count_colour(doubled, Text) == count_colour(plain, Text) * 4);

    // A scale of 1 is the ordinary draw, glyph for glyph.
    std::vector<COLOR> also_plain(plain.size(), 0);
    TEXTURE c = a;
    c.bitmap = also_plain.data();
    drawStringScaled("You Died!", Text, c, 4, 4, 1);
    CHECK(also_plain == plain);
}

static void test_death_screen()
{
    const int scale = uiScale();
    const DeathLayout layout = deathLayout();

    // The strings are vanilla 1.17.1's.
    CHECK(strcmp(deathHeading, "You Died!") == 0);
    CHECK(strcmp(deathRespawnLabel, "Respawn") == 0);
    CHECK(strcmp(deathTitleLabel, "Title Screen") == 0);

    // The title is doubled and sits where vanilla puts it (GUI y 30 inside a 2x
    // matrix), with the message and the score under it.
    CHECK(layout.title_scale == 2);
    CHECK(layout.title_y == 60 * scale);
    CHECK(layout.message_y == 85 * scale);
    CHECK(layout.score_y == 100 * scale);
    CHECK(layout.message_y >= layout.title_y + static_cast<int>(fontHeight()) * 2);
    CHECK(layout.score_y > layout.message_y);

    // Two widget buttons from `height / 4 + 72`, on the standard pitch.
    CHECK(layout.buttons.count == 2);
    CHECK(layout.buttons.top == SCREEN_HEIGHT / 4 + 72 * scale);
    CHECK(layout.buttons.h == ButtonHeight * scale);
    CHECK(layout.buttons.buttonY(1) - layout.buttons.buttonY(0) == ButtonPitch * scale);
    CHECK(layout.buttons.top > layout.score_y);
    CHECK(layout.buttons.bottom() <= SCREEN_HEIGHT);
    CHECK(layout.buttons.x >= 0 && layout.buttons.x + layout.buttons.w <= SCREEN_WIDTH);

    const char *const death_labels[] = { deathRespawnLabel, deathTitleLabel };
    check_labels_fit(layout.buttons, death_labels, 2, "death");

    // The doubled title has to fit the screen it is centred on.
    CHECK(static_cast<int>(measureStringScaled(deathHeading, layout.title_scale)) <= SCREEN_WIDTH);

    // The overlay is vanilla's red fade: over a grey world the red channel comes
    // out on top, top and bottom alike.
    std::vector<COLOR> pixels(static_cast<size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT, 0);
    TEXTURE canvas;
    canvas.width = SCREEN_WIDTH;
    canvas.height = SCREEN_HEIGHT;
    canvas.has_transparency = false;
    canvas.transparent_color = 0;
    canvas.bitmap = pixels.data();

    const COLOR grey = rgb(120, 120, 120);
    for(size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = grey;
    drawDeathOverlay(canvas);

    const COLOR top = pixels[0];
    const COLOR bottom = pixels[(SCREEN_HEIGHT - 1) * SCREEN_WIDTH];
    // Compare in 8-bit: 565 gives green six bits and the rest five, so the raw
    // packed channels are not comparable.
    const int top_r = ((top >> 11) & 0x1F) * 255 / 31;
    const int top_g = ((top >> 5) & 0x3F) * 255 / 63;
    const int top_b = (top & 0x1F) * 255 / 31;
    const int bottom_r = ((bottom >> 11) & 0x1F) * 255 / 31;
    const int bottom_g = ((bottom >> 5) & 0x3F) * 255 / 63;
    const int bottom_b = (bottom & 0x1F) * 255 / 31;
    CHECK(top_r > top_g && top_r > top_b);
    CHECK(bottom_r > bottom_g && bottom_r > bottom_b);
    // The fade deepens downward, as vanilla's gradient does.
    CHECK(bottom_r > top_r);
}

static void test_strings()
{
    CHECK(versionText[0] != '\0');
    CHECK(creditText[0] != '\0');

    // The splashes are vanilla's, so there are plenty of them -- far more than the
    // hand-written list this screen used to pick from.
    CHECK(splashLineCount > 50);

    for(int i = 0; i < splashLineCount; ++i)
    {
        CHECK(splashLines[i] != nullptr);
        CHECK(splashLines[i][0] != '\0');

        // The font covers one byte per glyph, so a line has to stay inside
        // printable ASCII: a UTF-8 line would draw two glyphs per accented
        // character. How wide a line is does not matter here -- the splash is
        // scaled to fit, which test_title_layout checks line by line.
        bool ascii = true;
        for(const char *c = splashLines[i]; *c != '\0'; ++c)
            if(static_cast<unsigned char>(*c) < 0x20 || static_cast<unsigned char>(*c) >= 0x7F)
                ascii = false;
        CHECK(ascii);
    }
}

int main()
{
    printf("menuui_test (%dx%d, font %u px)\n", SCREEN_WIDTH, SCREEN_HEIGHT,
           static_cast<unsigned int>(fontHeight()));

    test_font();
    test_button_boxes();
    test_title_layout();
    test_pause_layout();
    test_options_layout();
    test_options_scrolling();
    test_loading_screen();
    test_scaled_font();
    test_death_screen();
    test_strings();

    printf("menuui_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
