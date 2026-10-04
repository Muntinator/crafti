#include "starttask.h"

#include "controls.h"

#include "audio_manager.h"

#include <algorithm>
#include <stdint.h>

#ifndef _TINSPIRE
#include <SDL/SDL.h>
#endif

#include "font.h"
#include "menuui.h"
#include "settingstask.h"
#include "worldtask.h"
#include "worldselecttask.h"
#include "graphtask.h"
#include "world.h"
#include "blockrenderer.h"
#include "terrain.h"
#include "texturetools.h"

#include "textures/title_backdrop.h"
#include "textures/title_logo.h"
#include "textures/edition.h"
#include "textures/language_icon.h"
#include "textures/accessibility_icon.h"

StartTask start_task;

// The button order lives in MenuUI's layout and here in the enum; the two have
// to agree on how many buttons the title screen has.
static_assert(StartTask::START_ITEM_MAX == MenuUI::TitleButtonCount, "title button count");

namespace
{
    /**
     * The title screen is static: the panorama, the wordmark and the edition strip
     * are the same every frame, and all three are ordinary textures embedded in the
     * build, so they are simply blitted. The splash is the one piece that has to be
     * drawn: it is text, it changes every visit, and it is rotated, so it is drawn
     * once into a texture of its own and then blitted at an angle.
     */
    TEXTURE *splash_texture = nullptr;
    const char *splash_drawn = nullptr;

    /** Picks one of the jokes under the wordmark, once per visit to the screen. */
    const char *current_splash = MenuUI::splashLines[0];

    /**
     * The splash's pulse. `splash_pulse` runs 0..10..0 and is the percent the line
     * is scaled down by, and it steps every `SplashPulseStepMs` so that the whole
     * up-and-down takes half a second -- the two pulses a second vanilla has.
     */
    const int SplashPulseStepMs = 25;
    int splash_pulse = 0;
    int splash_pulse_ms = 0;

    /**
     * Draws the splash line into a texture of its own, outlined, ready to be
     * rotated onto the screen. It is rebuilt only when the line changes, which is
     * once per visit: this is the only per-visit allocation the screen makes.
     */
    void buildSplash(const char *text)
    {
        if(splash_texture != nullptr)
        {
            deleteTexture(splash_texture);
            splash_texture = nullptr;
        }

        // One pixel of outline either side, which is what drawSplash() adds.
        const int w = static_cast<int>(measureString(text)) + 2;
        const int h = static_cast<int>(fontHeight()) + 2;
        splash_texture = newTexture(w, h, 0, true, 0);
        MenuUI::drawSplash(text, *splash_texture, 1, 1);
        splash_drawn = text;
    }

    /** Gives the splash texture back. The rest of the screen is embedded artwork. */
    void releaseTitleGraphics()
    {
        if(splash_texture != nullptr)
        {
            deleteTexture(splash_texture);
            splash_texture = nullptr;
            splash_drawn = nullptr;
        }
    }

    /**
     * Blits `src` rotated by -20 degrees, the angle vanilla tilts the splash at,
     * scaled by `scale_percent` and anchored on its left edge's centre line.
     *
     * nGL has no rotated blit, so this walks the destination rectangle the rotated
     * sprite covers and maps every pixel back into the source with the inverse
     * rotation: one pass, and the sprite is sampled exactly once per pixel, which
     * is what keeps the tilted text from breaking up. The angle's sine and cosine
     * are the fixed-point constants below, because a 20-degree tilt does not need
     * a call into the trigonometry library.
     */
    void blitRotated(const TEXTURE &src, TEXTURE &dest, int anchor_x, int anchor_y,
                     int scale_percent)
    {
        if(scale_percent < 1)
            scale_percent = 1;

        const int64_t cos_q = 962;  // cos(-20) * 1024
        const int64_t sin_q = -350; // sin(-20) * 1024

        const int w = static_cast<int>(src.width);
        const int h = static_cast<int>(src.height);
        const int half = h / 2;

        // The rotated rectangle's bounding box: the four corners carried through
        // the rotation are enough, and they keep the loop off the rest of the screen.
        const int corner_x[4] = {0, w, 0, w};
        const int corner_y[4] = {-half, -half, half, half};
        int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
        for(int i = 0; i < 4; ++i)
        {
            const int dx = static_cast<int>((corner_x[i] * cos_q - corner_y[i] * sin_q)
                                            * scale_percent / (1024 * 100));
            const int dy = static_cast<int>((corner_x[i] * sin_q + corner_y[i] * cos_q)
                                            * scale_percent / (1024 * 100));
            if(i == 0)
            {
                min_x = max_x = dx;
                min_y = max_y = dy;
                continue;
            }
            min_x = std::min(min_x, dx);
            max_x = std::max(max_x, dx);
            min_y = std::min(min_y, dy);
            max_y = std::max(max_y, dy);
        }

        const int64_t divisor = 1024 * static_cast<int64_t>(scale_percent);
        for(int dy = min_y; dy <= max_y; ++dy)
        {
            const int py = anchor_y + dy;
            if(py < 0 || py >= static_cast<int>(dest.height))
                continue;

            for(int dx = min_x; dx <= max_x; ++dx)
            {
                const int px = anchor_x + dx;
                if(px < 0 || px >= static_cast<int>(dest.width))
                    continue;

                // The inverse of the rotation above, with the scale undone.
                const int64_t dxq = dx, dyq = dy;
                const int sx = static_cast<int>((dxq * cos_q + dyq * sin_q) * 100 / divisor);
                const int sy = static_cast<int>((-dxq * sin_q + dyq * cos_q) * 100 / divisor) + half;
                if(sx < 0 || sx >= w || sy < 0 || sy >= h)
                    continue;

                const COLOR c = src.bitmap[sx + sy * w];
                if(src.has_transparency && c == src.transparent_color)
                    continue;
                dest.bitmap[px + py * dest.width] = c;
            }
        }
    }
}

StartTask::StartTask()
{
}

StartTask::~StartTask()
{
}

void StartTask::makeCurrent()
{
    // Vanilla's title screen opens with nothing focused: no button wears the
    // highlight until the pointer hovers one or the keyboard steps to one.
    selected_item = -1;
    current_splash = MenuUI::splashLines[static_cast<unsigned int>(rand()) % MenuUI::splashLineCount];
    // The title screen plays vanilla's `music.menu` -- the pack's four menu
    // tracks, not the world's soundtrack. Opening a world hands the music over
    // to the game pool the same way vanilla's MusicManager swaps pools.
    GameAudio::setMusicDesired(true, GameAudio::MusicSceneMenu);
    // The point the pointer already sits at does not count as a move: the screen
    // opens with nothing focused, and the pointer only takes over when it is
    // actually moved. On the calculator this is also what stops the press that
    // left the previous screen from landing on a button here.
    Pointer::seed();
    Task::makeCurrent();
}

void StartTask::activate()
{
    if(!itemEnabled(selected_item))
        return;

    GameAudio::uiClick();

    // Every choice but quitting leaves the title screen, and the splash texture
    // goes with it (the next visit builds it again).
    if(selected_item != QUIT)
        releaseTitleGraphics();

    switch(selected_item)
    {
    case SINGLEPLAYER:
        // Vanilla opens its world list here; this engine's world kinds -- the
        // saved world and the three it can generate -- stand in for the list.
        worldselect_task.makeCurrent();
        break;
    case OPTIONS:
        // Vanilla's "Options..." is on the title screen as well as in the pause
        // menu, and opens the same options screen from both.
        settings_task.openFrom(&start_task);
        break;
    case QUIT:
        running = false;
        break;
    default:
        break;
    }
}

void StartTask::render()
{
    const MenuUI::TitleLayout layout = MenuUI::titleLayout(current_splash);

    if(splash_drawn != current_splash)
        buildSplash(current_splash);

    // The panorama, scaled to the screen. Vanilla rotates it every frame; here it
    // is one pre-rendered frame, because six cubemap faces do not fit on a CX.
    drawTexture(title_backdrop, *screen,
                0, 0, title_backdrop.width, title_backdrop.height,
                0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    // The official wordmark and the edition strip under it.
    drawTexture(title_logo, *screen,
                0, 0, title_logo.width, title_logo.height,
                layout.logo_x, layout.logo_y, layout.logo_w, layout.logo_h);
    drawTexture(edition, *screen,
                0, 0, edition.width, edition.height,
                layout.edition_x, layout.edition_y, layout.edition_w, layout.edition_h);

    // The splash, tilted up to the right and pulsing, which is what makes the title
    // screen look alive without anything actually moving. The pulse runs 0..10..0
    // (vanilla's `|sin| * 0.1`), so the line shrinks by up to ten percent and comes
    // back, twice a second.
    if(splash_texture != nullptr)
    {
        const int pulse = splash_pulse <= MenuUI::SplashPulse
                              ? splash_pulse
                              : 2 * MenuUI::SplashPulse - splash_pulse;
        blitRotated(*splash_texture, *screen, layout.splash_x, layout.splash_y,
                    layout.splash_scale - pulse);
    }

    for(int i = 0; i < START_ITEM_MAX; ++i)
    {
        int x = 0, y = 0, w = 0, h = 0;
        layout.buttonRect(i, x, y, w, h);
        const bool enabled = itemEnabled(i);
        const bool focused = (i == selected_item);

        // The two icon buttons carry vanilla's icons instead of a label.
        if(i == LANGUAGE)
        {
            MenuUI::drawIconButton(language_icon, *screen, x, y, w, h, focused, enabled);
            continue;
        }
        if(i == ACCESSIBILITY)
        {
            MenuUI::drawIconButton(accessibility_icon, *screen, x, y, w, h, focused, enabled);
            continue;
        }

        MenuUI::drawButton(*screen, x, y, w, h, focused, enabled);
        MenuUI::drawButtonLabel(MenuUI::titleLabels[i], *screen, x, y, w, h,
                                focused, enabled);
    }

    // The small print along the bottom, where vanilla keeps its version and its
    // credits at vanilla's own x. What the calculator's keys do is documented on
    // the help screen rather than here, and the audio pack reports itself on the
    // sound test screen.
    MenuUI::drawSmallPrint(MenuUI::versionText, *screen, layout.version_x, layout.version_y);
    MenuUI::drawSmallPrint(MenuUI::creditText, *screen, layout.credit_x, layout.version_y);
}

void StartTask::logic(GLFix dt)
{
    // The splash's pulse runs on real time, like the day/night clock does.
    splash_pulse_ms += static_cast<int>(dt * GLFix(static_cast<int>(simulation_tick_ms)));
    while(splash_pulse_ms >= SplashPulseStepMs)
    {
        splash_pulse_ms -= SplashPulseStepMs;
        splash_pulse = (splash_pulse + 1) % 21;
    }

    // Vanilla's focus-by-hover, on both machines: the pointer picks the button
    // and a click takes it. On the calculator the pointer is the touchpad's
    // virtual cursor, so the title screen can be opened without the keypad --
    // which matters because "Singleplayer" is the only way into a world.
    Pointer::poll();
    const int mouse_x = Pointer::x(), mouse_y = Pointer::y();

    const MenuUI::TitleLayout layout = MenuUI::titleLayout(current_splash);
    int hovered = -1;
    for(int i = 0; i < START_ITEM_MAX; ++i)
    {
        int x = 0, y = 0, w = 0, h = 0;
        layout.buttonRect(i, x, y, w, h);
        if(mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h)
            hovered = i;
    }

    // Only a pointer that has moved takes the focus; a resting pointer leaves the
    // keyboard in charge. A click still takes whatever is under it. Moving onto a
    // greyed button or off the buttons clears the highlight, which is what
    // vanilla's hover looks like: the lit button is the one under the pointer,
    // and a button that cannot be used never lights up.
    if(Pointer::moved())
        selected_item = (hovered >= 0 && itemEnabled(hovered)) ? hovered : -1;

    if(Pointer::clicked() && hovered >= 0 && itemEnabled(hovered))
    {
        selected_item = hovered;
        activate();
        return;
    }

    if(key_held_down)
        key_held_down = Controls::cursorUp() || Controls::cursorDown() || Controls::activate() || Controls::menu();
    else if(Controls::cursorUp())
    {
        do
        {
            --selected_item;
            if(selected_item < 0)
                selected_item = START_ITEM_MAX - 1;
        } while(!itemEnabled(selected_item));
        key_held_down = true;
    }
    else if(Controls::cursorDown())
    {
        do
        {
            ++selected_item;
            if(selected_item == START_ITEM_MAX)
                selected_item = 0;
        } while(!itemEnabled(selected_item));
        key_held_down = true;
    }
    else if(Controls::activate())
    {
        if(!itemEnabled(selected_item))
        {
            key_held_down = true;
            return;
        }

        activate();
        key_held_down = true;
    }
    else if(Controls::menu())
    {
        if(has_saved_world)
        {
            releaseTitleGraphics();
            world_task.makeCurrent();
        }
        else
            running = false;

        key_held_down = true;
    }
}
