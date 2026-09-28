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

class Task
{
public:
    virtual ~Task() {}

    virtual void render() = 0;
    /** dt = elapsed real time / nominal tick (1.0 = one old logic step). Keeps motion consistent when FPS varies. */
    virtual void logic(GLFix dt) = 0;

    virtual void makeCurrent();

    static bool keyPressed(const t_key &key);

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
