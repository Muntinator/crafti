#ifndef PLAYERMODEL_H
#define PLAYERMODEL_H

#include "gl.h"
#include "mobmodel.h"

/**
 * The player's own model, drawn inside a GUI screen.
 *
 * Vanilla's player inventory shows the player: `InventoryScreen.renderBg` blits
 * the window and then calls `renderEntityInInventory(leftPos + 51, topPos + 75,
 * 30, ...)` to draw the player standing in the window's right half, turning to
 * follow the mouse. This is that model -- a transcription of `PlayerModel` into
 * the Mob tables (see mobmodel.h) -- and the official "Steve" skin it is
 * unwrapped against.
 *
 * The model is drawn with the same `Mob::draw()` the world's mobs use, so the
 * geometry, the skin's unwrap and the limb poses are the ones the rest of the
 * engine renders with; what is added here is the projection. nGL projects
 * through a fixed camera at the screen's centre whose divide is
 * `near_plane / z`, so a model hung off the near plane comes out at its own size
 * with the screen's centre as its origin -- which is exactly what a GUI window
 * needs, since the model is then just translated to its corner of the window.
 *
 * Only the player inventory draws it: the crafting table, the furnace and the
 * chest are windows vanilla does not put a player in.
 */
namespace PlayerModel
{
    /** The vanilla `PlayerModel` boxes, read against `PlayerModel.createMesh`. */
    const Mob::MobModel &model();

    /** The official 64x64 "Steve" skin (`textures/entity/steve.png`). */
    const TEXTURE &skin();

    /**
     * Draws the player the way `renderEntityInInventory` does: the feet at
     * (feet_x, feet_y), one world block `pixels_per_block` pixels tall, and the
     * whole model turned by `yaw` degrees about its own vertical axis and leaned
     * by `pitch` degrees about its side-to-side axis. Vanilla's own call passes
     * `i + 51, j + 75, 30`, so the player window passes its origin plus
     * (51, 75)*scale and 30*scale.
     *
     * The depth buffer is cleared for the picture: the GUI draws by writing the
     * framebuffer directly, so the world's own depth values would otherwise still
     * be sitting behind the window and would clip the model.
     */
    void drawInGui(int feet_x, int feet_y, int pixels_per_block, GLFix yaw, GLFix pitch);

    /** The model's turn for a mouse `dx` pixels from its centre (vanilla's rule). */
    GLFix yawForMouse(int dx);
    /** And its lean for a `dy` pixels above or below that centre. */
    GLFix pitchForMouse(int dy);

    /** How far up from the feet vanilla draws a player model, in blocks. */
    constexpr int ModelHeightBlocks = 2;
}

#endif // PLAYERMODEL_H
