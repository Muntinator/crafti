#include "starttask.h"

#include "audio_manager.h"

#include "font.h"
#include "menuui.h"
#include "worldtask.h"
#include "graphtask.h"
#include "world.h"
#include "blockrenderer.h"
#include "terrain.h"
#include "texturetools.h"

extern unsigned char font_dat[];

StartTask start_task;

namespace
{
    /**
     * The title screen is static: the dirt backdrop and the wordmark are the same
     * every frame. Both are therefore drawn once into textures of their own and
     * blitted from then on, which is what makes the screen cheap on the calculator
     * (two copies instead of a few hundred scaled tiles and a full-screen darkening
     * pass every frame). They are deliberately never freed: this task lives as long
     * as the game does, and freeing a texture after nglUninit() is not safe.
     */
    TEXTURE *title_backdrop = nullptr;
    TEXTURE *title_logo = nullptr;

    constexpr int TitleDirtTile = 16; ///< one dirt block, in screen pixels at scale 1

    /** One of the jokes under the wordmark, picked once per visit to the screen. */
    const char *current_splash = MenuUI::splashLines[0];

    /**
     * Gives the two cached textures back. They are worth their 150 kB while the
     * title screen is up and worth nothing once the world is, so they are built
     * when the screen is shown and released when it is left -- a world that never
     * returns to the title keeps the memory, which is the point.
     */
    void releaseTitleGraphics()
    {
        if(title_backdrop != nullptr)
        {
            deleteTexture(title_backdrop);
            title_backdrop = nullptr;
        }
        if(title_logo != nullptr)
        {
            deleteTexture(title_logo);
            title_logo = nullptr;
        }
    }

    /** Tiles the dirt once into a screen-sized texture and dims it like vanilla. */
    void buildTitleBackdrop()
    {
        title_backdrop = newTexture(SCREEN_WIDTH, SCREEN_HEIGHT, 0, false);

        // terrain_atlas[2][0] is the dirt block: the same tile the world draws.
        const TextureAtlasEntry &dirt = terrain_atlas[2][0].resized;
        const int tile = TitleDirtTile * MenuUI::uiScale();

        for(int y = 0; y + tile <= static_cast<int>(title_backdrop->height); y += tile)
            for(int x = 0; x + tile <= static_cast<int>(title_backdrop->width); x += tile)
                drawTexture(*terrain_resized, *title_backdrop,
                            dirt.left, dirt.top,
                            dirt.right - dirt.left, dirt.bottom - dirt.top,
                            x, y, tile, tile);

        // Vanilla's menu backdrop is the dirt at about two thirds brightness, so
        // that the logo and the labels on top of it stay readable.
        MenuUI::shadeRect(*title_backdrop, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 62);
    }

    /** Draws the wordmark once, into a texture sized to hold its outline and bevel. */
    void buildTitleLogo(const MenuUI::TitleLayout &layout)
    {
        const int margin = MenuUI::logoMargin(layout.logo_scale);

        title_logo = newTexture(layout.logo_width + margin * 2, layout.logo_height + margin * 2, 0, true, 0);
        MenuUI::drawLogo(MenuUI::titleWordmark, *title_logo, title_logo->width / 2, margin, layout.logo_scale);
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
    // Vanilla opens on the first button it can actually use.
    selected_item = has_saved_world ? CONTINUE : NEW_TERRAIN;
    current_splash = MenuUI::splashLines[static_cast<unsigned int>(rand()) % MenuUI::splashLineCount];
    GameAudio::stopMusic();
    Task::makeCurrent();
}

void StartTask::render()
{
    const MenuUI::TitleLayout layout = MenuUI::titleLayout();

    if(title_backdrop == nullptr)
        buildTitleBackdrop();
    if(title_logo == nullptr)
        buildTitleLogo(layout);

    // The whole backdrop in one copy: the dirt is already tiled and dimmed inside it.
    drawTexture(*title_backdrop, *screen,
                0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
                0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    drawTexture(*title_logo, *screen,
                0, 0, title_logo->width, title_logo->height,
                (SCREEN_WIDTH - static_cast<int>(title_logo->width)) / 2, layout.logo_y,
                title_logo->width, title_logo->height);

    MenuUI::drawSplash(current_splash, *screen, SCREEN_WIDTH / 2, layout.splash_y);

    for(int i = 0; i < START_ITEM_MAX; ++i)
    {
        const int y = layout.buttons.buttonY(i);
        const bool enabled = itemEnabled(i);
        const bool focused = (i == selected_item);

        MenuUI::drawButton(*screen, layout.buttons.x, y, layout.buttons.w, layout.buttons.h,
                           focused, enabled);
        MenuUI::drawButtonLabel(MenuUI::titleLabels[i], *screen, layout.buttons.x, y,
                                layout.buttons.w, layout.buttons.h, focused, enabled);
    }

    // The small print along the bottom, where vanilla keeps its version and its
    // credits, and the two lines this build needs: how to drive the menu, and
    // whether the audio pack was found.
    MenuUI::drawSmallPrint(MenuUI::versionText, *screen, 1, layout.version_y);
    MenuUI::drawSmallPrint(MenuUI::creditText, *screen,
                           SCREEN_WIDTH - static_cast<int>(measureString(MenuUI::creditText)) - 1,
                           layout.version_y);
    MenuUI::drawSmallPrint(MenuUI::hintText, *screen, 1, layout.hint_y);
    MenuUI::drawSmallPrint(GameAudio::packStatus(), *screen, 1, layout.audio_y);
}void StartTask::logic(GLFix /*dt */)
{
    if(key_held_down)
        key_held_down = keyPressed(KEY_NSPIRE_ESC) || keyPressed(KEY_NSPIRE_UP) || keyPressed(KEY_NSPIRE_DOWN) || keyPressed(KEY_NSPIRE_2) || keyPressed(KEY_NSPIRE_8) || keyPressed(KEY_NSPIRE_5) || keyPressed(KEY_NSPIRE_ENTER);
    else if(keyPressed(KEY_NSPIRE_UP) || keyPressed(KEY_NSPIRE_8))
    {
        do
        {
            --selected_item;
            if(selected_item < 0)
                selected_item = START_ITEM_MAX - 1;
        } while(!itemEnabled(selected_item));
        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_DOWN) || keyPressed(KEY_NSPIRE_2))
    {
        do
        {
            ++selected_item;
            if(selected_item == START_ITEM_MAX)
                selected_item = 0;
        } while(!itemEnabled(selected_item));
        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_5))
    {
        if(!itemEnabled(selected_item))
        {
            key_held_down = true;
            return;
        }

        GameAudio::play(GameAudio::EventMenuSelect);

        // Every choice but quitting leaves the title screen, and the two cached
        // textures go with it (the next visit builds them again).
        if(selected_item != EXIT)
            releaseTitleGraphics();

        switch(selected_item)
        {
        case CONTINUE:
            world_task.makeCurrent();
            break;
        case NEW_FLAT:
            world.setWorldType(World::WorldType::Flat);
            world_task.resetWorld();
            world_task.makeCurrent();
            break;
        case NEW_TERRAIN:
            world.setWorldType(World::WorldType::Terrain);
            world_task.resetWorld();
            world_task.makeCurrent();
            break;
        case NEW_GRAPH:
            graph_task.makeCurrent();
            break;
        case EXIT:
            running = false;
            break;
        }

        key_held_down = true;
    }
    else if(keyPressed(KEY_NSPIRE_ESC))
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
