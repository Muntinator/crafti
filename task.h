#ifndef TASK_H
#define TASK_H

#include "gl.h"

#include <libndls.h>

/**
 * Nominal wall-clock length of one `dt` unit, in milliseconds. The main loop
 * divides the frame time by this to get `dt`, and anything that must run on real
 * time rather than on frames (the day/night clock) multiplies by it again to
 * recover elapsed milliseconds. Keeping it in one place stops the two from
 * drifting apart.
 */
#ifdef _TINSPIRE
constexpr unsigned int simulation_tick_ms = 300; // Calculator fixed simulation tick
#else
constexpr unsigned int simulation_tick_ms = 33; // Fixed simulation tick (~30 Hz)
#endif

/**
 * The pointing device, as one API for both machines.
 *
 * A desktop has a mouse that reports an absolute position. A touchpad CX has
 * none: its pad reports contact and a *delta*, and a press only when the finger
 * lands on one of the arrow or click areas. The front end is written entirely
 * against absolute screen coordinates, so the touchpad is given a virtual cursor
 * here -- the same idea the inventory screen already used on its own -- and every
 * screen can then be driven by pointing on either machine instead of being
 * keyboard-only on the calculator.
 *
 * One poll per logic tick, then read the accessors. `moved()` is what separates
 * a resting pointer from one that has actually gone somewhere, which is how
 * vanilla's screens keep the keyboard in charge until the pointer takes over.
 */
namespace Pointer
{
    /** Reads the device and updates the position and button state for this tick. */
    void poll();

    /** The pointer's position in screen pixels. */
    int x();
    int y();

    /** True when the position changed since the previous poll. */
    bool moved();

    /** True while the primary button is held. */
    bool down();

    /** The rising edge of `down()`: a tap or a click, once. */
    bool clicked();

    /**
     * Takes the current position as the baseline, so the next `moved()` is false
     * until the pointer really moves. Called when a screen opens, so the press
     * that opened it cannot also land on a button of the new screen.
     */
    void seed();
}

class Task
{
public:
    virtual ~Task() {}

    virtual void render() = 0;
    /** dt = elapsed real time / nominal tick (1.0 = one old logic step). Keeps motion consistent when FPS varies. */
    virtual void logic(GLFix dt) = 0;

    virtual void makeCurrent();

    static bool keyPressed(const t_key &key);

    /**
     * The character a text key is typing, or 0 when none was newly pressed.
     *
     * The command console needs what the game's own controls deliberately leave
     * alone -- the calculator's letter and digit keys -- and it needs them as an
     * edge rather than as the level keyPressed() reports, because a held key must
     * not type over and over. Both are handled here so the console does not have
     * to know which machine it is running on.
     */
    static char textKeyPressed();
    /**
     * Forgets which text keys were held. Called when a text field opens: a key
     * that was already down (the '/' that opened the console, say) must not count
     * as the first character typed.
     */
    static void resetTextKeys();

    static void initializeGlobals(const char *savefile);
    static void deinitializeGlobals();

    //All tasks share these values

    //Pointer to the current running task
    static Task *current_task;
    //Whether a key is being held down
    static bool key_held_down;
    //Whether the machine has a touchpad
    static bool has_touchpad, keys_inverted;
    //The application will exit if this is false
    static bool running;
    //The buffer nGL renders to
    static TEXTURE *screen;
    //Some tasks draw the last frame of world_task as a background
    static TEXTURE *background;
    static bool background_saved;
    static void saveBackground();
    static void drawBackground();
    //Saving and loading
    static bool load();
    static bool save();
    static const char *savefile;
};

#endif // TASK_H
