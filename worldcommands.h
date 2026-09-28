#ifndef WORLDCOMMANDS_H
#define WORLDCOMMANDS_H

/**
 * The world side of the debug command language.
 *
 * command.h decides what the player typed; this decides what happens. The work
 * is deliberately collected here rather than in the console task, because the
 * console is only one way in: a command can also be run from a script, from a
 * future chat line, or from a test, and all of them should mean the same thing.
 *
 * Every command writes back one short line, which the console shows and which a
 * silent caller can ignore. The line is always written when a command was
 * recognised, including when it refused to do something, so the player is told
 * why instead of being left guessing.
 *
 * The names a command takes are the names the game already displays: /give walks
 * block_names (terrain.cpp) and the item name table (itemicons.cpp) rather than
 * keeping a second private list, so a block that is added to the game is
 * addressable by the name it shows without touching this file. Matching folds
 * case and the separators, so "Cobblestone", "cobble-stone" and "cobble stone"
 * all reach the same block.
 */

/** Runs one line. Writes a one-line reply into `reply`, which is never empty. */
void runCommand(const char *line, char *reply, unsigned int reply_size);

#endif // WORLDCOMMANDS_H
