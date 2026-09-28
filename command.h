#ifndef COMMAND_H
#define COMMAND_H

#include <stdint.h>

/**
 * The debug command language: everything about a command line that does not need
 * the world.
 *
 * This header includes nothing from the engine on purpose, so the whole grammar
 * -- tokenising, quoting, number parsing, the names of times and weather, the
 * coordinate `~` shorthand, the size cap on /fill and the command table itself --
 * is unit tested on a development host (tests/command_test.cc) without pulling in
 * nGL, the world or the textures.
 *
 * The split is the same one worldclock.h and weather.h follow:
 *
 *  - this file answers *what did the player type, and is it well formed?*
 *  - worldcommands.cpp answers *what does the world do about it?*
 *
 * Command names are matched case-insensitively and are stored lowercase, and
 * block and item names are matched through normaliseName(), which folds case and
 * drops the spaces, underscores and hyphens that separate the same name in
 * different spellings ("Cobblestone", "cobble-stone" and "cobble_stone" all
 * match). That is what lets /give accept the names the game already displays
 * instead of a second private table of aliases.
 *
 * Nothing here allocates: a parsed line is a fixed-size struct, which matters on
 * the CX where the heap is small and a typo should not be able to fail the
 * command parser.
 */
namespace Command
{
    /** Arguments a command may carry, beyond its name. */
    constexpr int MaxArgs = 10;
    /** Longest command name or argument, including the terminator. */
    constexpr int MaxTokenLength = 28;

    enum class Result
    {
        Ok,
        /** Only whitespace (or a lone '/'), so nothing was run. */
        Empty,
        /** A token or the argument count did not fit. */
        TooLong,
    };

    struct Parsed
    {
        /** Lowercase command name, without the leading '/'. */
        char name[MaxTokenLength] = {};
        char args[MaxArgs][MaxTokenLength] = {};
        int arg_count = 0;
    };

    /**
     * Splits a line into a command and its arguments. A leading '/' is optional
     * (the console's prompt implies it, but a line may also be passed in with one
     * already, as the help text spells commands). Double quotes group a
     * multi-word argument, which is how a block whose name has a space in it gets
     * through the tokeniser ("redstone torch").
     */
    Result parse(const char *line, Parsed &out);

    // --- command table ------------------------------------------------------

    int commandCount();
    const char *commandName(int index);
    /** How the command is written out, i.e. "time <set|add> <value>". */
    const char *commandUsage(int index);
    const char *commandSummary(int index);

    /** Index of a command or one of its aliases, or -1. `name` is lowercase. */
    int commandIndex(const char *name);
    bool isKnownCommand(const char *name);

    /** True when a command's name starts with `prefix` (an empty prefix matches). */
    bool commandMatchesPrefix(int index, const char *prefix);

    /** One line of the help screen for one command, or for all of them. */
    void formatHelp(int index, char *out, unsigned int size);
    void formatHelpAll(char *out, unsigned int size);

    // --- numbers ------------------------------------------------------------

    /** Strict: the whole string has to be a number, or nothing is written. */
    bool parseInt(const char *text, int &out);
    bool parseUnsigned(const char *text, unsigned int &out);

    /**
     * 0..1 for the named modes. Anything else is rejected rather than silently
     * taken as off, so a typo in a command says so instead of doing nothing.
     */
    bool parseBoolean(const char *text, bool &out);

    /**
     * A world coordinate. "~" is relative to `origin` and "~5" is five along from
     * it, exactly as the vanilla commands read; anything else is absolute. The
     * world's own bounds are checked by the caller, which is the only place that
     * knows how tall the world is.
     */
    bool parseCoordinate(const char *text, int origin, int &out);

    // --- named times, weather and gamemodes --------------------------------

    /** Ticks since dawn, in [0, TicksPerDay). */
    bool parseTime(const char *text, unsigned int &ticks);

    /** Index into the weather states of weather.h (0 clear, 1 rain, 2 thunder, 3 snow). */
    bool parseWeather(const char *text, int &state);
    const char *weatherName(int state);

    /** 0 survival, 1 creative. */
    bool parseGamemode(const char *text, int &mode);
    const char *gamemodeName(int mode);

    // --- spellings ----------------------------------------------------------

    /**
     * Folds a name for comparison: lowercased, with spaces, underscores and
     * hyphens removed. Output is always terminated and never overflows.
     */
    void normaliseName(const char *in, char *out, unsigned int size);

    /** True when two names are the same spelling once normalised. */
    bool namesMatch(const char *a, const char *b);

    // --- /fill --------------------------------------------------------------

    /**
     * Absolute cap on the blocks one /fill may place. The CX has no room for
     * Minecraft's 32768 and every block written costs a chunk rebuild, so the
     * limit is what keeps a mistyped corner from freezing the calculator: at
     * 2048 the worst case is still a handful of frames.
     */
    constexpr long MaxFillBlocks = 2048;

    /**
     * Inclusive volume of a box, in blocks, and whether it is within the cap.
     * Returns false for an empty box or one over the cap; `volume` still holds
     * the size, so the caller can say how far over it was.
     */
    bool fillVolume(int x1, int y1, int z1, int x2, int y2, int z2, long &volume);
}

#endif // COMMAND_H
