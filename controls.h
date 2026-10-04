#ifndef CONTROLS_H
#define CONTROLS_H

#include "task.h"

/**
 * The calculator's control scheme.
 *
 * This build is played on a calculator with a damaged keyboard, so every binding
 * lives here and nowhere else. The four arrow keys that ring the touchpad's
 * click button -- the calculator's mouse keys -- carry movement, the pad itself
 * carries the camera, and the two modifiers do the two things that used to sit
 * on Esc and the Menu key.
 *
 * Keys deliberately left unbound, because they are unreadable or unusable:
 *
 *     8  5  6  4    the digits either side of the movement cross
 *     .             block list / screenshot
 *     delete        backspace / delete world
 *     Car  Menu  Doc  Tab  Esc
 *
 * They are not merely unused: `Task::keyPressed` has no desktop mapping for any
 * of them, so nothing in the game can still ask for one.
 *
 * What replaces them:
 *
 *     the four mouse keys   walk, and move the cursor in every menu
 *     the touchpad           the camera in the world, and every on-screen button
 *     the pad's click key    jump, and "select" in every menu
 *     Enter                  "select" too, for a second way in
 *     Shift                  the menu: open it in the world, back out of anything
 *     Ctrl                   memory: saves the world
 *     B                      the block list (Ctrl+B takes a screenshot)
 *     the Bar key            erase, where a backspace used to be -- it sits
 *                            directly above the dead Delete key
 *     0 1 2 3 7 9            the hotbar and place/break, untouched
 *     /                      the console, untouched
 *
 * The names below are deliberately about the *action*, not the key: the point of
 * this file is that no screen has to remember which physical key it is.
 *
 * One honest consequence: the console's text table still lists `4`, `5`, `6`,
 * `8` and `.`, because those are the calculator's keys for typing those
 * characters and the desktop's for the same. On the damaged keyboard they cannot
 * be read, so those five characters cannot be typed into a command. No command
 * needs them, and it is left visible rather than quietly dropped.
 */
namespace Controls
{

// --- movement: the four mouse keys around the touchpad click ---------------

/** Walk forward. */
inline bool forward() { return Task::keyPressed(KEY_NSPIRE_UP); }
/** Walk back. */
inline bool back() { return Task::keyPressed(KEY_NSPIRE_DOWN); }
/** Strafe left. */
inline bool left() { return Task::keyPressed(KEY_NSPIRE_LEFT); }
/** Strafe right. */
inline bool right() { return Task::keyPressed(KEY_NSPIRE_RIGHT); }

// --- menu cursor: the same four keys, one screen at a time ------------------

/** Move the selection up one row. */
inline bool cursorUp() { return Task::keyPressed(KEY_NSPIRE_UP); }
/** Move the selection down one row. */
inline bool cursorDown() { return Task::keyPressed(KEY_NSPIRE_DOWN); }
/** Move the selection left one column, or change a value down. */
inline bool cursorLeft() { return Task::keyPressed(KEY_NSPIRE_LEFT); }
/** Move the selection right one column, or change a value up. */
inline bool cursorRight() { return Task::keyPressed(KEY_NSPIRE_RIGHT); }

// --- actions ----------------------------------------------------------------

/** Jump in the world, and "select" on a menu that has something selected. */
inline bool jump() { return Task::keyPressed(KEY_NSPIRE_CLICK); }

/** Select whatever is in focus: the pad's click key, or Enter. */
inline bool activate()
{
    return Task::keyPressed(KEY_NSPIRE_CLICK) || Task::keyPressed(KEY_NSPIRE_ENTER);
}

/** Open the pause menu in the world, and go back out of any other screen. */
inline bool menu() { return Task::keyPressed(KEY_NSPIRE_SHIFT); }

/** Memory: saves the world. */
inline bool memory() { return Task::keyPressed(KEY_NSPIRE_CTRL); }

/** The console, on the divide key, which is the one that spells '/'. */
inline bool console() { return Task::keyPressed(KEY_NSPIRE_DIVIDE); }

/** The block catalogue. Ctrl with it takes a screenshot instead. */
inline bool blockList() { return Task::keyPressed(KEY_NSPIRE_B); }

/** Erase one character, where the dead Delete key used to backspace. */
inline bool erase() { return Task::keyPressed(KEY_NSPIRE_BAR); }

/** Any of the menu-ish keys, for a screen's held-down latch. */
inline bool menuOrJump()
{
    return Task::keyPressed(KEY_NSPIRE_SHIFT) || Task::keyPressed(KEY_NSPIRE_CLICK);
}

}

#endif // CONTROLS_H