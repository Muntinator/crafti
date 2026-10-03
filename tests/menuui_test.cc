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
#include <time.h>
#include <vector>
#include <algorithm>

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

    // One label per button slot: the text buttons carry vanilla's words and the
    // two icon slots carry none.
    CHECK(titleLabelCount == TitleButtonCount);

    int bottom_right_x = 0, bottom_right_y = 0, bottom_right_w = 0, bottom_right_h = 0;
    layout.buttonRect(TitleRightHalfButton, bottom_right_x, bottom_right_y,
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
    layout.buttonRect(TitleLeftHalfButton, left_x, left_y, left_w, left_h);
    CHECK(left_x == SCREEN_WIDTH / 2 - 100 * uiScale());
    CHECK(bottom_right_x == SCREEN_WIDTH / 2 + 2 * uiScale());
    CHECK(left_y == bottom_right_y);
    CHECK(left_w == bottom_right_w && left_h == bottom_right_h);

    // Vanilla's two 20x20 icon buttons flank that row: the language button at
    // width / 2 - 124 and the accessibility button at width / 2 + 104, each on
    // the row's own y and clearing the half buttons by the 4-pixel gap.
    int icon_x = 0, icon_y = 0, icon_w = 0, icon_h = 0;
    layout.buttonRect(TitleLeftIconButton, icon_x, icon_y, icon_w, icon_h);
    CHECK(icon_x == SCREEN_WIDTH / 2 + TitleIconLeftOffset * uiScale());
    CHECK(icon_y == bottom_right_y);
    CHECK(icon_w == TitleIconSize * uiScale() && icon_h == TitleIconSize * uiScale());
    CHECK(icon_x + icon_w + 4 * uiScale() == left_x);

    layout.buttonRect(TitleRightIconButton, icon_x, icon_y, icon_w, icon_h);
    CHECK(icon_x == SCREEN_WIDTH / 2 + TitleIconRightOffset * uiScale());
    CHECK(icon_y == bottom_right_y);
    CHECK(icon_x == bottom_right_x + bottom_right_w + 4 * uiScale());

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

static void test_world_select_layout()
{
    // Vanilla's `WorldSelectionScreen`, at the scale the screen runs at: the
    // heading at y 8, the search box at y 22, the list from 48 down to
    // height - 64, rows on a 36-pixel pitch with 32 of content, and the two
    // button rows at height - 52 and height - 28. These are the numbers that
    // make this screen vanilla's rather than merely similar to it.
    const int s = uiScale();
    const WorldSelectLayout layout = worldSelectLayout();

    CHECK(layout.scale == s);
    CHECK(layout.heading_y == WorldHeadingY * s);
    CHECK(layout.search_x == SCREEN_WIDTH / 2 + WorldSearchOffsetX * s);
    CHECK(layout.search_y == WorldSearchY * s);
    CHECK(layout.search_w == WorldSearchWidth * s);
    CHECK(layout.search_h == WorldSearchHeight * s);
    CHECK(layout.list_top == WorldListTop * s);
    CHECK(layout.list_bottom == SCREEN_HEIGHT - WorldListBottomOffset * s);
    CHECK(layout.row_pitch == WorldRowPitch * s);
    CHECK(layout.row_h == WorldRowHeight * s);
    CHECK(layout.row_w == WorldRowWidth * s);
    CHECK(layout.row_left == SCREEN_WIDTH / 2 - WorldRowWidth * s / 2 + WorldRowLeftOffset * s);
    CHECK(layout.box_x == layout.row_left - 2 * s);
    CHECK(layout.box_w == layout.row_w + 4 * s);
    CHECK(layout.icon_size == WorldIconSize * s);
    CHECK(layout.text_x == layout.row_left + WorldRowTextOffset * s);
    CHECK(layout.button_h == ButtonHeight * s);
    CHECK(layout.row1_y == SCREEN_HEIGHT - WorldRow1YOffset * s);
    CHECK(layout.row2_y == SCREEN_HEIGHT - WorldRow2YOffset * s);
    CHECK(layout.row1_w == WorldRow1Width * s);
    CHECK(layout.row1_left_x == SCREEN_WIDTH / 2 + WorldRow1LeftOffset * s);
    CHECK(layout.row1_right_x == SCREEN_WIDTH / 2 + WorldRow1RightOffset * s);
    CHECK(layout.row2_w == WorldRow2Width * s);
    for(int i = 0; i < 4; ++i)
        CHECK(layout.row2_x[i] == SCREEN_WIDTH / 2 + WorldRow2Offsets[i] * s);

    // Everything sits inside the screen.
    CHECK(layout.search_x >= 0 && layout.search_x + layout.search_w <= SCREEN_WIDTH);
    CHECK(layout.row_left >= 0 && layout.row_left + layout.row_w <= SCREEN_WIDTH);
    CHECK(layout.list_top < layout.list_bottom);
    CHECK(layout.visibleRows() >= 1);

    // Rows start four pixels under the list's top, step by the pitch, and a row
    // scrolled out of the window is drawn off the top of it.
    CHECK(layout.rowY(0, 0) == layout.list_top + 4 * s);
    CHECK(layout.rowY(1, 0) - layout.rowY(0, 0) == WorldRowPitch * s);
    CHECK(layout.rowY(2, 2) == layout.rowY(0, 0));

    // The six buttons: the two 150-wide ones on the first row and the four
    // 72-wide ones on the second, at vanilla's own x offsets. Every label has
    // to fit its own box.
    CHECK(worldSelectActionCount == WorldActionCount);
    for(int action = 0; action < WorldActionCount; ++action)
    {
        int x, y, w, h;
        layout.buttonRect(action, x, y, w, h);
        CHECK(h == ButtonHeight * s);
        if(action == WorldPlay || action == WorldCreate)
        {
            CHECK(w == WorldRow1Width * s);
            CHECK(y == layout.row1_y);
            CHECK(x == (action == WorldPlay ? layout.row1_left_x : layout.row1_right_x));
        }
        else
        {
            CHECK(w == WorldRow2Width * s);
            CHECK(y == layout.row2_y);
            CHECK(x == layout.row2_x[action - WorldEdit]);
        }
        CHECK(x >= 0 && x + w <= SCREEN_WIDTH);
        CHECK(y + h <= SCREEN_HEIGHT);

        const char *label = worldSelectActionLabels[action];
        CHECK(label != nullptr && label[0] != '\0');
        const int width = static_cast<int>(measureString(label));
        if(width + 8 > w)
            printf("    world button %d (\"%s\") is %d wide in a %d button\n",
                   action, label, width, w);
        CHECK(width + 8 <= w);
    }

    // The strings the screen draws fit their own boxes.
    CHECK(worldSelectSearchHint[0] != '\0');
    CHECK(static_cast<int>(measureString(worldSelectSearchHint)) <= layout.search_w - 8 * s);
    CHECK(worldSelectDefaultName[0] != '\0');
    CHECK(worldSelectNeverPlayed[0] != '\0');
    CHECK(worldSelectNewTag[0] != '\0');
    CHECK(worldSelectWorldWord[0] != '\0');

    printf("    world select: list y%d..%d, %d rows pitch %d at x%d..%d\n",
           layout.list_top, layout.list_bottom, layout.visibleRows(), layout.row_pitch,
           layout.row_left, layout.row_left + layout.row_w);
}

static void test_world_form_layout()
{
    // Vanilla's `CreateWorldScreen`: the name field at y 60 under its grey
    // label, the "Will be saved in:" line at 85, the option pair at 100 and
    // the help lines at 122 and 134 -- all over vanilla's own bottom row.
    const int s = uiScale();
    const WorldFormLayout form = worldFormLayout();

    CHECK(form.heading_y == FormHeadingY * s);
    CHECK(form.label_y == FormLabelY * s);
    CHECK(form.field_w == FormFieldWidth * s && form.field_h == FormFieldHeight * s);
    CHECK(form.field_x == SCREEN_WIDTH / 2 - FormFieldWidth * s / 2);
    CHECK(form.field_y == FormFieldY * s);
    CHECK(form.result_y == FormResultY * s);
    CHECK(form.option_y == FormOptionY * s);
    CHECK(form.help_y == FormHelpY * s && form.help_pitch == FormHelpPitch * s);
    CHECK(form.option_w == FormButtonWidth * s);
    CHECK(form.button_h == ButtonHeight * s);
    CHECK(form.left_x == SCREEN_WIDTH / 2 + FormLeftOffset * s);
    CHECK(form.right_x == SCREEN_WIDTH / 2 + FormRightOffset * s);
    CHECK(form.bottom_y == SCREEN_HEIGHT - FormBottomYOffset * s);

    int x, y, w, h;
    form.optionRect(0, x, y, w, h);
    CHECK(x == form.left_x && y == form.option_y && w == form.option_w && h == form.button_h);
    form.optionRect(1, x, y, w, h);
    CHECK(x == form.right_x && y == form.option_y);
    form.bottomRect(0, x, y, w, h);
    CHECK(x == form.left_x && y == form.bottom_y && w == form.option_w);
    form.bottomRect(1, x, y, w, h);
    CHECK(x == form.right_x && y == form.bottom_y);
    CHECK(form.left_x + form.option_w < form.right_x);
    CHECK(form.bottom_y + form.button_h <= SCREEN_HEIGHT);

    // Every label the dialog draws fits its own box or its own line.
    for(int mode = 0; mode < gameModeCount; ++mode)
    {
        char label[40];
        formatOptionLabel(label, sizeof(label), gameModeLabel, gameModeValues[mode]);
        if(static_cast<int>(measureString(label)) + 8 > form.option_w)
            printf("    game mode label \"%s\" is %d wide in a %d button\n",
                   label, static_cast<int>(measureString(label)), form.option_w);
        CHECK(static_cast<int>(measureString(label)) + 8 <= form.option_w);

        for(int line = 0; line < 2; ++line)
        {
            const int width = static_cast<int>(measureString(gameModeHelp[mode][line]));
            if(width > SCREEN_WIDTH - 16)
                printf("    mode %d help %d (\"%s\") is %d wide\n",
                       mode, line, gameModeHelp[mode][line], width);
            CHECK(width <= SCREEN_WIDTH - 16);
        }
    }
    for(int type = 0; type < worldTypeCount; ++type)
    {
        char label[40];
        formatOptionLabel(label, sizeof(label), worldTypeLabel, worldTypeValues[type]);
        CHECK(static_cast<int>(measureString(label)) + 8 <= form.option_w);
    }

    // The label, the field's placeholder and the result line fit the field's
    // 200-pixel column.
    CHECK(static_cast<int>(measureString(nameLabel)) <= FormFieldWidth * s);
    CHECK(static_cast<int>(measureString(worldSelectDefaultName)) <= FormFieldWidth * s - 8 * s);
    char result[64];
    snprintf(result, sizeof(result), "%s %s", resultFolderLabel, worldSelectDefaultName);
    CHECK(static_cast<int>(measureString(result)) <= FormFieldWidth * s);

    // And the bottom row's own labels fit their buttons.
    CHECK(static_cast<int>(measureString(createConfirmLabel)) + 8 <= FormButtonWidth * s);
    CHECK(static_cast<int>(measureString(saveLabel)) + 8 <= FormButtonWidth * s);
    CHECK(static_cast<int>(measureString(cancelLabel)) + 8 <= FormButtonWidth * s);

    printf("    world form: field y%d, options y%d, bottom y%d\n",
           form.field_y, form.option_y, form.bottom_y);
}

static void test_confirm_layout()
{
    // Vanilla's `ConfirmScreen`: the question at y 70, the message at y 90 on a
    // 9-pixel pitch, and the buttons clamped between a sixth of the screen plus
    // 96 and 24 off the bottom.
    const int s = uiScale();

    for(int lines = 1; lines <= 2; ++lines)
    {
        const ConfirmLayout c = confirmLayout(lines);
        CHECK(c.title_y == ConfirmTitleY * s);
        CHECK(c.message_y == ConfirmMessageY * s);
        CHECK(c.line_pitch == ConfirmLinePitch * s);
        CHECK(c.button_w == ConfirmButtonWidth * s);
        CHECK(c.button_h == ButtonHeight * s);
        CHECK(c.left_x == SCREEN_WIDTH / 2 + ConfirmLeftOffset * s);
        CHECK(c.right_x == SCREEN_WIDTH / 2 + ConfirmRightOffset * s);

        const int wanted = (ConfirmMessageY + lines * ConfirmLinePitch + 12) * s;
        const int above = SCREEN_HEIGHT / 6 + ConfirmButtonYBase * s;
        const int below = SCREEN_HEIGHT - ConfirmButtonYMax * s;
        int expected = wanted < above ? above : wanted;
        if(expected > below)
            expected = below;
        CHECK(c.button_y == expected);
        CHECK(c.button_y + c.button_h <= SCREEN_HEIGHT);
        CHECK(c.button_y >= c.message_y + lines * c.line_pitch);

        int x, y, w, h;
        c.buttonRect(0, x, y, w, h);
        CHECK(x == c.left_x && y == c.button_y && w == c.button_w && h == c.button_h);
        c.buttonRect(1, x, y, w, h);
        CHECK(x == c.right_x && y == c.button_y);
        CHECK(x + w <= SCREEN_WIDTH);
    }

    // The delete question is one line and the warning wraps into the lines the
    // layout was made for.
    const int question_w = static_cast<int>(measureString(deleteQuestion));
    if(question_w + 8 > SCREEN_WIDTH)
        printf("    delete question is %d wide on a %d screen\n", question_w, SCREEN_WIDTH);
    CHECK(question_w + 8 <= SCREEN_WIDTH);
    CHECK(deleteConfirmLabel[0] != '\0');
    CHECK(strstr(deleteWarningFormat, "%s") != nullptr);

    printf("    confirm: question y%d, buttons y%d..%d\n",
           ConfirmTitleY * s, confirmLayout(1).button_y, confirmLayout(2).button_y);
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

// --------------------------------------------------- the full-screen washes

namespace
{
    /**
     * The pause wash, computed the direct way: every pixel's three channels
     * unpacked to 8-bit, mixed toward the gradient's grey and packed back.
     * This is the arithmetic MenuUI's mix tables have to reproduce exactly.
     */
    void referencePauseWash(TEXTURE &tex)
    {
        const int top_alpha = 0xC0, bottom_alpha = 0xD0;
        const int wash = 0x10;

        const int height = static_cast<int>(tex.height);
        const int span = height > 1 ? height - 1 : 1;

        for(int y = 0; y < height; ++y)
        {
            const int alpha = top_alpha + (bottom_alpha - top_alpha) * y / span;
            const int keep = 255 - alpha;

            COLOR *line = tex.bitmap + y * tex.width;
            for(int x = 0; x < static_cast<int>(tex.width); ++x)
            {
                const COLOR c = line[x];
                const int r = (((c >> 11) & 0x1F) * 255 / 31) * keep / 255 + wash * alpha / 255;
                const int g = (((c >> 5) & 0x3F) * 255 / 63) * keep / 255 + wash * alpha / 255;
                const int b = (c & 0x1F) * 255 / 31 * keep / 255 + wash * alpha / 255;
                line[x] = static_cast<COLOR>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
    }

    /** The death wash, the same direct arithmetic with its red gradient. */
    void referenceDeathWash(TEXTURE &tex)
    {
        const int top_alpha = 0x60, bottom_alpha = 0xA0;
        const int top_red = 0x50, bottom_red = 0x80;

        const int height = static_cast<int>(tex.height);
        const int span = height > 1 ? height - 1 : 1;

        for(int y = 0; y < height; ++y)
        {
            const int alpha = top_alpha + (bottom_alpha - top_alpha) * y / span;
            const int red8 = top_red + (bottom_red - top_red) * y / span;
            const int keep = 255 - alpha;

            COLOR *line = tex.bitmap + y * tex.width;
            for(int x = 0; x < static_cast<int>(tex.width); ++x)
            {
                const COLOR c = line[x];
                const int r = (((c >> 11) & 0x1F) * 255 / 31) * keep / 255 + red8 * alpha / 255;
                const int g = (((c >> 5) & 0x3F) * 255 / 63) * keep / 255;
                const int b = (c & 0x1F) * 255 / 31 * keep / 255;
                line[x] = static_cast<COLOR>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
    }

    /** The dirt dimming, the direct way: each channel scaled by `keep / 100`. */
    void referenceShade(TEXTURE &tex, int keep_percent)
    {
        for(int row = 0; row < static_cast<int>(tex.height); ++row)
        {
            COLOR *line = tex.bitmap + row * tex.width;
            for(int col = 0; col < static_cast<int>(tex.width); ++col)
            {
                const COLOR c = line[col];
                const int r = ((c >> 11) & 0x1F) * keep_percent / 100;
                const int g = ((c >> 5) & 0x3F) * keep_percent / 100;
                const int b = (c & 0x1F) * keep_percent / 100;
                line[col] = static_cast<COLOR>((r << 11) | (g << 5) | b);
            }
        }
    }

    double usecsSince(clock_t start, int frames)
    {
        return static_cast<double>(clock() - start) * 1000000.0
            / (static_cast<double>(CLOCKS_PER_SEC) * frames);
    }
}

static void test_overlays()
{
    // Vanilla blends its pause and death washes over the whole screen, and the
    // menu screens dim the dirt the same way; nGL cannot blend, so these mix
    // every pixel of the frame. The fast path is checked pixel for pixel against
    // the direct arithmetic above, so the tables cannot drift from the blend
    // vanilla asks for, and both are timed -- a regression shows up as changed
    // pixels or a cost that jumped.
    std::vector<COLOR> source(static_cast<size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT);
    for(size_t i = 0; i < source.size(); ++i)
        source[i] = static_cast<COLOR>((i * 2654435761u) >> 16);

    std::vector<COLOR> want = source, have = source;
    TEXTURE ref, got;
    ref.width = got.width = SCREEN_WIDTH;
    ref.height = got.height = SCREEN_HEIGHT;
    ref.has_transparency = got.has_transparency = false;
    ref.bitmap = want.data();
    got.bitmap = have.data();

    std::copy(source.begin(), source.end(), want.begin());
    std::copy(source.begin(), source.end(), have.begin());
    referencePauseWash(ref);
    MenuUI::drawPauseOverlay(got);
    CHECK(have == want);

    std::copy(source.begin(), source.end(), want.begin());
    std::copy(source.begin(), source.end(), have.begin());
    referenceDeathWash(ref);
    MenuUI::drawDeathOverlay(got);
    CHECK(have == want);

    std::copy(source.begin(), source.end(), want.begin());
    std::copy(source.begin(), source.end(), have.begin());
    referenceShade(ref, 25);
    MenuUI::shadeRect(got, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 25);
    CHECK(have == want);

    // The washes run for every frame a menu is open, so their cost is per
    // frame: the direct arithmetic beside MenuUI's tables on the same frame.
    const int frames = 32;

    clock_t start = clock();
    for(int i = 0; i < frames; ++i)
        referencePauseWash(ref);
    const double ref_pause = usecsSince(start, frames);
    start = clock();
    for(int i = 0; i < frames; ++i)
        MenuUI::drawPauseOverlay(got);
    const double mix_pause = usecsSince(start, frames);

    start = clock();
    for(int i = 0; i < frames; ++i)
        referenceDeathWash(ref);
    const double ref_death = usecsSince(start, frames);
    start = clock();
    for(int i = 0; i < frames; ++i)
        MenuUI::drawDeathOverlay(got);
    const double mix_death = usecsSince(start, frames);

    start = clock();
    for(int i = 0; i < frames; ++i)
        referenceShade(ref, 25);
    const double ref_shade = usecsSince(start, frames);
    start = clock();
    for(int i = 0; i < frames; ++i)
        MenuUI::shadeRect(got, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 25);
    const double mix_shade = usecsSince(start, frames);

    printf("    cost: pause wash %.1f us direct / %.1f us MenuUI per %dx%d frame\n",
           ref_pause, mix_pause, SCREEN_WIDTH, SCREEN_HEIGHT);
    printf("    cost: death wash %.1f us direct / %.1f us MenuUI per %dx%d frame\n",
           ref_death, mix_death, SCREEN_WIDTH, SCREEN_HEIGHT);
    printf("    cost: dirt dim 25%% %.1f us direct / %.1f us MenuUI per %dx%d frame\n",
           ref_shade, mix_shade, SCREEN_WIDTH, SCREEN_HEIGHT);
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
    test_world_select_layout();
    test_world_form_layout();
    test_confirm_layout();
    test_pause_layout();
    test_options_layout();
    test_options_scrolling();
    test_loading_screen();
    test_overlays();
    test_scaled_font();
    test_death_screen();
    test_strings();

    printf("menuui_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
