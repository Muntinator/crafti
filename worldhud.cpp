// The 2D overlay for WorldTask: the crosshair, the survival HUD, the hotbar,
// messages, the coordinate readout and the graph view's labels.
//
// This is split out of worldtask.cpp, which holds the 3D scene and the player
// simulation. Everything here draws in screen space with drawTexture/drawString
// or straight into the framebuffer, so it needs no part of the 3D pipeline.
//
// The survival bars are driven by survival.h (health in half-heart points,
// hunger in drumstick points, experience as one running total) rather than by
// HUD counters, so what is drawn is a view of the real state.

#include "worldtask.h"

#include "blockrenderer.h"
#include "font.h"
#include "inventory.h"
#include "settingstask.h"
#include "survival.h"

#include "textures/icons.h"
#include "textures/inventory.h"

extern unsigned char font_dat[];

namespace
{
    /** Width of a string in pixels, using the same font metrics the HUD draws with. */
    unsigned int measureTextWidth(const char *str)
    {
        unsigned int w = 0;
        while(*str)
            w += font_dat[17 + static_cast<unsigned char>(*str++)];
        return w;
    }
}

void WorldTask::renderHud()
{
    const bool graph_mode = world.worldType() == World::WorldType::Graph;

    // HUD from textures/gui/icons.png (MCP Gui.field_110324_m). The UVs are the
    // vanilla 1.4.x ones the atlas is generated for: hearts at y=0, armour at
    // y=9, breath at y=18, hunger at y=27, all 9x9 with the container at x=16,
    // the full icon at x=52 and the half icon at x=61.
    {
        constexpr int hotbar_src_width = 22 * 9;
        const int hud_scale = SCREEN_WIDTH >= hotbar_src_width * 2 ? 2 : 1;
        constexpr int sp = 9;
        constexpr int container_x = 16;
        constexpr int full_x = 52;
        constexpr int half_x = 61;
        constexpr int heart_y = 0;
        constexpr int bubble_y_src = 18;
        constexpr int food_y = 27;

        const int row_y = SCREEN_HEIGHT - 39 * hud_scale;
        const int hud_left = SCREEN_WIDTH / 2 - 91 * hud_scale;
        const int hud_right = SCREEN_WIDTH / 2 + 91 * hud_scale;
        const int icon_s = sp * hud_scale;
        const int pitch = 8 * hud_scale;

        // Health in half-heart points: the odd point in the middle of the bar is
        // drawn with the half-heart tile, so 19 points is visibly not full.
        for (unsigned int i = 0; i < max_hearts; ++i)
        {
            const int hx = hud_left + static_cast<int>(i) * pitch;
            drawTexture(icons, *screen, container_x, heart_y, sp, sp, hx, row_y, icon_s, icon_s);

            const int points = health - static_cast<int>(i) * 2;
            if (points >= 2)
                drawTexture(icons, *screen, full_x, heart_y, sp, sp, hx, row_y, icon_s, icon_s);
            else if (points == 1)
                drawTexture(icons, *screen, half_x, heart_y, sp, sp, hx, row_y, icon_s, icon_s);
        }

        // Hunger, filled inwards from the right hand edge like vanilla.
        for (unsigned int i = 0; i < max_food; ++i)
        {
            const int fx = hud_right - static_cast<int>(i) * pitch - icon_s;
            drawTexture(icons, *screen, container_x, food_y, sp, sp, fx, row_y, icon_s, icon_s);

            const int points = hunger.hunger - static_cast<int>(i) * 2;
            if (points >= 2)
                drawTexture(icons, *screen, full_x, food_y, sp, sp, fx, row_y, icon_s, icon_s);
            else if (points == 1)
                drawTexture(icons, *screen, half_x, food_y, sp, sp, fx, row_y, icon_s, icon_s);
        }

        // Breath, only while there is something to warn about: one bubble per
        // tenth of the bar above the hunger row.
        if (air < Survival::MaxAir)
        {
            const int bubble_y = row_y - 10 * hud_scale;
            const unsigned int bubbles = (static_cast<unsigned int>(air) * max_food + Survival::MaxAir - 1) / Survival::MaxAir;
            for (unsigned int i = 0; i < max_food; ++i)
            {
                const int bx = hud_right - static_cast<int>(i) * pitch - icon_s;
                drawTexture(icons, *screen, i < bubbles ? container_x : 25, bubble_y_src, sp, sp,
                            bx, bubble_y, icon_s, icon_s);
            }
        }

        // Experience: level and progress are derived from the running total, so
        // there is only one number to keep in step.
        int xp_level = 0;
        int xp_remaining = 0;
        const float xp_bar = Survival::levelProgress(total_xp, xp_level, xp_remaining);

        if (xp_level > 0 || xp_bar > 0.001f)
        {
            const int bar_x = SCREEN_WIDTH / 2 - 91 * hud_scale;
            const int bar_y = SCREEN_HEIGHT - 29 * hud_scale;
            constexpr int bar_w = 182;
            constexpr int bar_h = 5;
            drawTexture(icons, *screen, 0, 64, bar_w, bar_h,
                        bar_x, bar_y, bar_w * hud_scale, bar_h * hud_scale);
            int fill = static_cast<int>(xp_bar * 183.0f);
            if (fill > bar_w)
                fill = bar_w;
            if (fill > 0)
                drawTexture(icons, *screen, 0, 69, fill, bar_h,
                            bar_x, bar_y, fill * hud_scale, bar_h * hud_scale);
        }

        if (xp_level > 0)
        {
            char lvl[12];
            snprintf(lvl, sizeof(lvl), "%d", xp_level);
            drawStringCenter(lvl, 0x87E0, *screen, SCREEN_WIDTH / 2,
                             static_cast<unsigned int>(SCREEN_HEIGHT - 35 * hud_scale));
        }
    }

    //Don't draw the inventory when drawing the background for BlockListTask
    if(draw_inventory)
    {
        const BLOCK_WDATA current_slot = current_inventory.currentSlot();
        current_inventory.draw(*screen);
        drawStringCenter(current_inventory.currentSlotCount() == 0 ? "Empty" : global_block_renderer.getName(current_slot), 0xFFFF, *screen, SCREEN_WIDTH / 2, SCREEN_HEIGHT - current_inventory.height() - fontHeight());

        // Draw selection indicator using inventory texture at (1,23) to (2,44)
        constexpr int hotbar_src_width = 22 * 9; // 22 * hotbar_slot_count
        constexpr int hotbar_src_height = 22;
        constexpr int hotbar_slot_src_left = 3;
        constexpr int hotbar_slot_src_pitch = 20;

        const int hotbar_scale = SCREEN_WIDTH >= hotbar_src_width * 2 ? 2 : 1;
        const int hotbar_draw_width = hotbar_src_width * hotbar_scale;
        const int hotbar_draw_height = hotbar_src_height * hotbar_scale;
        const int hotbar_slot_pitch = hotbar_slot_src_pitch * hotbar_scale;
        const int hotbar_slots_left = hotbar_slot_src_left * hotbar_scale;
        const int hotbar_slots_top = 3 * hotbar_scale;

        const int inventory_x = (SCREEN_WIDTH - hotbar_draw_width) / 2;
        const int inventory_y = SCREEN_HEIGHT - hotbar_draw_height - 3;

        // Selector: 22x22 from inventory.png at (1,23) to (22,44)
        constexpr int selector_src_x = 1;
        constexpr int selector_src_y = 23;
        constexpr int selector_src_w = 22;
        constexpr int selector_src_h = 22;

        const int slot_offset = current_inventory.currentSlotIndex() * hotbar_slot_pitch;
        const int draw_x = inventory_x + hotbar_slots_left + slot_offset - 2 * hotbar_scale;
        const int draw_y = inventory_y + hotbar_slots_top - 2 * hotbar_scale;

        drawTexture(inventory, *screen,
                    selector_src_x, selector_src_y, selector_src_w, selector_src_h,
                    draw_x, draw_y,
                    selector_src_w * hotbar_scale, selector_src_h * hotbar_scale);
    }

    int message_y = graph_mode ? static_cast<int>(fontHeight()) + 7 : 5;

    if(message_timeout > 0)
    {
        drawString(message, 0xFFFF, *screen, 2, message_y);
        --message_timeout;
    }

    // Active status effects: name and whole seconds left, under the message line.
    // Four fit before the list would reach the middle of the screen.
    for(unsigned int i = 0; i < effect_count && i < 4; ++i)
    {
        const Survival::EffectInstance &e = effects[i];
        char line[40];
        const unsigned int seconds = static_cast<unsigned int>(e.duration) / Survival::TicksPerSecond;
        if(e.duration == 0xFFFF)
            snprintf(line, sizeof(line), "%s", Survival::effectName(e.effect));
        else
            snprintf(line, sizeof(line), "%s %u:%02u", Survival::effectName(e.effect),
                     seconds / 60, seconds % 60);

        drawString(line, Survival::effectIsGood(e.effect) ? 0x07E0 : 0xF800, *screen,
                   2, static_cast<unsigned int>(message_y + fontHeight() + 2 + static_cast<int>(i) * fontHeight()));
    }

    if(graph_mode)
    {
        char bounds_msg[64];
        const int zoom = world.graphZoomPercent();
        const int range = world.graphRange();
        if(world.graphUnbounded())
            snprintf(bounds_msg, sizeof(bounds_msg), "Graph zoom:%d%% n:%d x,y:[-inf,+inf]", zoom, world.graphFillDepth());
        else
        {
            const int bound_times_100 = (range * 10000) / zoom;
            const int bound_int = bound_times_100 / 100;
            const int bound_frac = bound_times_100 % 100;
            snprintf(bounds_msg, sizeof(bounds_msg), "Graph zoom:%d%% n:%d x,y:[-%d.%02d,%d.%02d]",
                     zoom, world.graphFillDepth(), bound_int, bound_frac, bound_int, bound_frac);
        }
        drawString(bounds_msg, 0xFFFF, *screen, 2, 5);

        char expr_msg[72];
        snprintf(expr_msg, sizeof(expr_msg), "z=%s", world.graphExpression());
        const unsigned int expr_w = measureTextWidth(expr_msg);
        const int expr_x = std::max(2, static_cast<int>(SCREEN_WIDTH - expr_w - 2));
        drawString(expr_msg, 0xFFFF, *screen, expr_x, 5);
    }

    if(selection_side != AABB::NONE && settings_task.getValue(SettingsTask::COORD_INDICATOR))
    {
        char pos_msg[64];
        const int bx = selection_pos.x.toInteger<int>();
        const int by = selection_pos.y.toInteger<int>();
        const int bz = selection_pos.z.toInteger<int>();

        if(graph_mode && world.graphMode() == World::GraphMode::Complex)
        {
            const int zoom = world.graphZoomPercent();
            const int rx100 = (bx * 10000) / zoom;
            const int iz100 = (bz * 10000) / zoom;

            const int rx_sign = rx100 < 0 ? -1 : 1;
            const int iz_sign = iz100 < 0 ? -1 : 1;
            const int rx_abs = rx100 * rx_sign;
            const int iz_abs = iz100 * iz_sign;

            snprintf(pos_msg, sizeof(pos_msg), "z=%s%d.%02d%s%d.%02di",
                     rx_sign < 0 ? "-" : "",
                     rx_abs / 100,
                     rx_abs % 100,
                     iz_sign < 0 ? "-" : "+",
                     iz_abs / 100,
                     iz_abs % 100);
        }
        else
        {
            snprintf(pos_msg, sizeof(pos_msg), "Block (%d,%d,%d)", bx, by, bz);
        }

        const int y_pos = graph_mode ? static_cast<int>(fontHeight()) + 7 : 5;
        drawString(pos_msg, 0xFFFF, *screen, 2, y_pos);
    }

    #ifdef FPS_COUNTER
        if(message_timeout == 0 && settings_task.getValue(SettingsTask::SHOW_FPS))
        {
            snprintf(this->message, sizeof(this->message), "FPS: %u", fps);
            message_timeout = 20;
        }
    #endif
}
