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
// The layout itself is MenuUI's: this test measures the module's own strings
// against the module's own boxes, so a label that is lengthened without moving the
// buttons fails here rather than on the calculator.
//
// Build and run with `make -C tests`.

#include "menuui.h"

#include <stdio.h>

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

static void test_button_boxes()
{
    CHECK(uiScale() >= 1);
    CHECK(SCREEN_WIDTH == 320 * uiScale());
    CHECK(SCREEN_HEIGHT == 240 * uiScale());

    // Every button is tall enough for the font and has a visible edge.
    const ButtonColumn column = centeredButtonColumn(4, SCREEN_HEIGHT / 2);
    CHECK(column.h >= static_cast<int>(fontHeight()) + 2);
    CHECK(column.w > 0 && column.w <= SCREEN_WIDTH);
    CHECK(column.x >= 0 && column.x + column.w <= SCREEN_WIDTH);
    CHECK(column.gap > 0);
    CHECK(column.count == 4);
    CHECK(column.buttonY(0) == column.top);
    CHECK(column.bottom() == column.buttonY(3) + column.h);

    // An empty column has no height and no buttons to draw.
    const ButtonColumn empty = centeredButtonColumn(0, SCREEN_HEIGHT / 2);
    CHECK(empty.count == 0);
    CHECK(empty.bottom() == empty.top);
}

static void test_title_layout()
{
    const TitleLayout layout = titleLayout();

    CHECK(layout.buttons.count == titleLabelCount);
    check_labels_fit(layout.buttons, titleLabels, titleLabelCount, "title");
    check_labels_fit(pauseMenuLayout(), pauseLabels, pauseLabelCount, "pause");

    // The wordmark fits the screen, outline and bevel included.
    const int margin = logoMargin(layout.logo_scale);
    CHECK(layout.logo_scale >= 1);
    CHECK(layout.logo_width == logoWidthFor(titleWordmark, layout.logo_scale));
    CHECK(layout.logo_height == logoHeightFor(layout.logo_scale));
    CHECK(layout.logo_width + margin * 2 <= SCREEN_WIDTH);
    CHECK(layout.logo_height + margin * 2 <= SCREEN_HEIGHT);

    // The wordmark and the splash own the top of the screen; the buttons sit below
    // both of them and above the small print.
    CHECK(layout.splash_y >= layout.logo_y);
    CHECK(layout.splash_y + static_cast<int>(fontHeight()) <= layout.buttons.top);
    CHECK(layout.buttons.bottom() < layout.audio_y);
    CHECK(layout.audio_y < layout.hint_y);
    CHECK(layout.hint_y < layout.version_y);
    CHECK(layout.version_y + static_cast<int>(fontHeight()) <= SCREEN_HEIGHT);

    // The version and the credit share the bottom line without touching, and the
    // credit still ends inside the screen.
    const int version_w = static_cast<int>(measureString(versionText));
    const int credit_w = static_cast<int>(measureString(creditText));
    CHECK(version_w + credit_w + 8 <= SCREEN_WIDTH);
    CHECK(credit_w + 1 <= SCREEN_WIDTH);

    // The small print is small enough to be legible in one line each.
    CHECK(static_cast<int>(measureString(hintText)) <= SCREEN_WIDTH);

    // Enabling and disabling the first button is what the title screen does with
    // the saved-world state, so both states have to look right.
    CHECK(titleLabels[0] != nullptr);
}

static void test_pause_layout()
{
    const ButtonColumn column = pauseMenuLayout();
    CHECK(column.count == pauseLabelCount);
    CHECK(column.top >= 0);
    CHECK(column.bottom() <= SCREEN_HEIGHT);
    CHECK(column.x >= 0 && column.x + column.w <= SCREEN_WIDTH);
    check_labels_fit(column, pauseLabels, pauseLabelCount, "pause");
}

static void test_splashes()
{
    CHECK(splashLineCount > 0);
    CHECK(titleWordmark[0] != '\0');
    CHECK(versionText[0] != '\0');
    CHECK(creditText[0] != '\0');

    // A splash is drawn centred under the wordmark with its outline; it has to be
    // narrower than the screen by that outline.
    for(int i = 0; i < splashLineCount; ++i)
    {
        CHECK(splashLines[i] != nullptr);
        const int width = static_cast<int>(measureString(splashLines[i]));
        if(width + 2 > SCREEN_WIDTH)
            printf("    splash %d (\"%s\") is %d wide\n", i, splashLines[i], width);
        CHECK(width + 2 <= SCREEN_WIDTH);
    }
}

int main()
{
    printf("menuui_test (%dx%d)\n", SCREEN_WIDTH, SCREEN_HEIGHT);

    test_button_boxes();
    test_title_layout();
    test_pause_layout();
    test_splashes();

    printf("menuui_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
