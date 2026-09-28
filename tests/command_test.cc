// Host tests for command.h: the debug command language.
//
// Nothing here touches the world, because nothing in command.cpp does: the
// tokeniser, the number and name parsers, the coordinate and time shorthands,
// the /fill cap and the command table are all pure, and this file pins them down
// so a typo in a command line is answered by the parser rather than by the world.
//
// The tests are deliberately unkind about the edges: an unterminated quote, an
// eleventh argument, an overflowing number, a name that is only a separator, a
// /fill box that would freeze the calculator. All of those have to be rejected,
// and each one has a case below.
//
// Build and run with `make -C tests`.

#include "command.h"
#include "weather.h"
#include "worldclock.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

using namespace Command;

static void test_empty_lines()
{
    Parsed parsed;

    CHECK(parse("", parsed) == Result::Empty);
    CHECK(parse("   ", parsed) == Result::Empty);
    CHECK(parse("/", parsed) == Result::Empty);
    CHECK(parse("\t\n ", parsed) == Result::Empty);

    // An empty line must not leave a half-written command behind.
    CHECK(parsed.name[0] == '\0');
    CHECK(parsed.arg_count == 0);
}

static void test_parsing_basics()
{
    Parsed parsed;

    CHECK(parse("/give stone 4", parsed) == Result::Ok);
    CHECK(strcmp(parsed.name, "give") == 0);
    CHECK(parsed.arg_count == 2);
    CHECK(strcmp(parsed.args[0], "stone") == 0);
    CHECK(strcmp(parsed.args[1], "4") == 0);

    // The '/' is optional, case does not matter in the name, and runs of
    // whitespace collapse.
    CHECK(parse("GIVE   Stone   4", parsed) == Result::Ok);
    CHECK(strcmp(parsed.name, "give") == 0);
    CHECK(parsed.arg_count == 2);
    // Only the command name is folded; an argument keeps the spelling the player
    // typed, because the name tables are compared case-insensitively later.
    CHECK(strcmp(parsed.args[0], "Stone") == 0);

    CHECK(parse("\t/time\tset\tnoon\t", parsed) == Result::Ok);
    CHECK(strcmp(parsed.name, "time") == 0);
    CHECK(parsed.arg_count == 2);
    CHECK(strcmp(parsed.args[0], "set") == 0);
    CHECK(strcmp(parsed.args[1], "noon") == 0);

    // A name with no arguments.
    CHECK(parse("seed", parsed) == Result::Ok);
    CHECK(strcmp(parsed.name, "seed") == 0);
    CHECK(parsed.arg_count == 0);
}

static void test_quoting()
{
    Parsed parsed;

    CHECK(parse("/give \"redstone torch\" 4", parsed) == Result::Ok);
    CHECK(strcmp(parsed.name, "give") == 0);
    CHECK(parsed.arg_count == 2);
    CHECK(strcmp(parsed.args[0], "redstone torch") == 0);
    CHECK(strcmp(parsed.args[1], "4") == 0);

    // A quote in the middle of a word is a literal character, not a second word.
    CHECK(parse("give stone", parsed) == Result::Ok);
    CHECK(strcmp(parsed.args[0], "stone") == 0);

    // An unterminated quote is refused rather than swallowing the rest of the
    // line, which would turn a typo into a command with one huge argument.
    CHECK(parse("/give \"stone", parsed) == Result::TooLong);

    // A quoted empty argument is still an argument.
    CHECK(parse("setblock 0 0 0 \"\"", parsed) == Result::Ok);
    CHECK(parsed.arg_count == 4);
    CHECK(parsed.args[3][0] == '\0');
}

static void test_overlong_input()
{
    Parsed parsed;

    // A command name one under the limit is fine, and one over is not.
    // The longest name the parser holds is MaxTokenLength - 1 characters plus
    // the terminator.
    char name[MaxTokenLength + 8];
    memset(name, 'x', sizeof(name) - 1);
    name[MaxTokenLength - 2] = '\0';
    char line[MaxTokenLength + 16];
    snprintf(line, sizeof(line), "/%s", name);
    CHECK(parse(line, parsed) == Result::Ok);
    CHECK(strlen(parsed.name) == static_cast<unsigned int>(MaxTokenLength) - 2);

    memset(name, 'x', sizeof(name) - 1);
    name[MaxTokenLength + 4] = '\0';
    snprintf(line, sizeof(line), "/%s", name);
    CHECK(parse(line, parsed) == Result::TooLong);

    // Exactly the cap is accepted: one name plus MaxArgs arguments.
    char many[512];
    int used = 0;
    for(int i = 0; i <= MaxArgs; ++i)
        used += snprintf(many + used, sizeof(many) - used, " a%d", i);
    CHECK(parse(many, parsed) == Result::Ok);
    CHECK(parsed.arg_count == MaxArgs);
    CHECK(strcmp(parsed.name, "a0") == 0);
    CHECK(strcmp(parsed.args[MaxArgs - 1], "a10") == 0);

    // One argument past the cap is rejected rather than dropped, so a mistyped
    // command cannot look like it ran.
    snprintf(many + used, sizeof(many) - used, "%s", " extra");
    CHECK(parse(many, parsed) == Result::TooLong);
}

static void test_numbers()
{
    int value = 0;

    CHECK(parseInt("0", value) && value == 0);
    CHECK(parseInt("12", value) && value == 12);
    CHECK(parseInt("-7", value) && value == -7);
    CHECK(parseInt("+7", value) && value == 7);
    CHECK(parseInt("2147483647", value) && value == 2147483647);
    CHECK(parseInt("-2147483647", value) && value == -2147483647);

    // Rejections: a number is the whole string, and it has to fit.
    value = 99;
    CHECK(!parseInt("", value));
    CHECK(!parseInt("12a", value));
    CHECK(!parseInt("a12", value));
    CHECK(!parseInt("1 2", value));
    CHECK(!parseInt("-", value));
    CHECK(!parseInt("2147483648", value));
    CHECK(!parseInt("1.5", value));
    // ...and a rejected parse must not have written anything.
    CHECK(value == 99);

    unsigned int unsigned_value = 0;
    CHECK(parseUnsigned("0", unsigned_value) && unsigned_value == 0);
    CHECK(parseUnsigned("4096", unsigned_value) && unsigned_value == 4096);
    CHECK(parseUnsigned("4294967295", unsigned_value) && unsigned_value == 4294967295u);
    CHECK(!parseUnsigned("4294967296", unsigned_value));
    CHECK(!parseUnsigned("-1", unsigned_value));
    CHECK(!parseUnsigned("+1", unsigned_value));
    CHECK(!parseUnsigned("", unsigned_value));
}

static void test_booleans()
{
    bool value = false;

    CHECK(parseBoolean("on", value) && value);
    CHECK(parseBoolean("TRUE", value) && value);
    CHECK(parseBoolean("yes", value) && value);
    CHECK(parseBoolean("1", value) && value);
    CHECK(parseBoolean("off", value) && !value);
    CHECK(parseBoolean("False", value) && !value);
    CHECK(parseBoolean("no", value) && !value);
    CHECK(parseBoolean("0", value) && !value);
    CHECK(!parseBoolean("maybe", value));
    CHECK(!parseBoolean("", value));
}

static void test_coordinates()
{
    int value = 0;

    CHECK(parseCoordinate("5", 100, value) && value == 5);
    CHECK(parseCoordinate("-5", 100, value) && value == -5);
    // "~" alone is the origin, "~n" is n along from it.
    CHECK(parseCoordinate("~", 100, value) && value == 100);
    CHECK(parseCoordinate("~5", 100, value) && value == 105);
    CHECK(parseCoordinate("~-5", 100, value) && value == 95);
    CHECK(parseCoordinate("~0", -3, value) && value == -3);

    CHECK(!parseCoordinate("", 0, value));
    CHECK(!parseCoordinate("~x", 0, value));
    CHECK(!parseCoordinate("x", 0, value));
}

static void test_time()
{
    unsigned int ticks = 0;

    // The names come from the clock, so /time and the sky cannot disagree.
    CHECK(parseTime("day", ticks) && ticks == WorldClock::TimeSunrise);
    CHECK(parseTime("SUNRISE", ticks) && ticks == WorldClock::TimeSunrise);
    CHECK(parseTime("dawn", ticks) && ticks == WorldClock::TimeSunrise);
    CHECK(parseTime("noon", ticks) && ticks == WorldClock::TimeNoon);
    CHECK(parseTime("midday", ticks) && ticks == WorldClock::TimeNoon);
    CHECK(parseTime("sunset", ticks) && ticks == WorldClock::TimeSunset);
    CHECK(parseTime("night", ticks) && ticks == 14000);
    CHECK(parseTime("midnight", ticks) && ticks == WorldClock::TimeMidnight);

    // A bare number is ticks, and it wraps into the day.
    CHECK(parseTime("0", ticks) && ticks == 0);
    CHECK(parseTime("6000", ticks) && ticks == 6000);
    CHECK(parseTime("24000", ticks) && ticks == 0);
    CHECK(parseTime("25000", ticks) && ticks == 1000);

    // The compact form: a day is 24000 ticks, an hour 1000, a minute 16.
    CHECK(parseTime("1d", ticks) && ticks == 0); // a whole day wraps to dawn
    CHECK(parseTime("6h", ticks) && ticks == 6000);
    CHECK(parseTime("12h", ticks) && ticks == 12000);
    CHECK(parseTime("1d6h", ticks) && ticks == 6000);
    CHECK(parseTime("1d12h30m", ticks) && ticks == 12000 + (30 * (WorldClock::TicksPerDay / 1440)) % WorldClock::TicksPerDay);
    CHECK(parseTime("100t", ticks) && ticks == 100);

    // Rejections.
    CHECK(!parseTime("", ticks));
    CHECK(!parseTime("12x", ticks));
    CHECK(!parseTime("x12", ticks));
    CHECK(parseTime("12", ticks) && ticks == 12); // a bare number is a tick count
    CHECK(!parseTime("6h6", ticks));         // a trailing amount with no unit
    CHECK(!parseTime("-1", ticks));
}

static void test_weather_and_gamemode()
{
    int state = -1;

    CHECK(parseWeather("clear", state) && state == 0);
    CHECK(parseWeather("SUNNY", state) && state == 0);
    CHECK(parseWeather("rain", state) && state == 1);
    CHECK(parseWeather("raining", state) && state == 1);
    CHECK(parseWeather("thunder", state) && state == 2);
    CHECK(parseWeather("storm", state) && state == 2);
    CHECK(parseWeather("2", state) && state == 2);
    // Snow is the fourth state, and it is spelled the way a player would: it is
    // the only way to see the flakes without a cold biome (weather.h).
    CHECK(parseWeather("snow", state) && state == 3);
    CHECK(parseWeather("SNOWING", state) && state == 3);
    CHECK(parseWeather("3", state) && state == 3);
    CHECK(!parseWeather("hail", state));
    CHECK(!parseWeather("4", state));
    CHECK(!parseWeather("", state));

    // The names round trip through the formatter the console prints with, and they
    // are numbered like the states they name, so the override can be stored as the
    // number parseWeather returned.
    CHECK(strcmp(weatherName(0), "Clear") == 0);
    CHECK(strcmp(weatherName(2), "Thunderstorm") == 0);
    CHECK(strcmp(weatherName(3), "Snow") == 0);
    CHECK(strcmp(weatherName(Weather::StateCount), "?") == 0);
    // Both tables cover every state, and they mean the same state at the same
    // index -- the console merely spells a storm the long way, which is the one
    // difference between them and is pinned here so it stays the only one.
    for(int state_index = 0; state_index < Weather::StateCount; ++state_index)
    {
        CHECK(weatherName(state_index)[0] != '\0');
        CHECK(Weather::stateName(state_index)[0] != '\0');
        CHECK(weatherName(state_index)[0] == Weather::stateName(state_index)[0]);
    }
    CHECK(strcmp(weatherName(2), "Thunderstorm") == 0);
    CHECK(strcmp(Weather::stateName(2), "Thunder") == 0);

    int mode = -1;
    CHECK(parseGamemode("survival", mode) && mode == 0);
    CHECK(parseGamemode("s", mode) && mode == 0);
    CHECK(parseGamemode("CREATIVE", mode) && mode == 1);
    CHECK(parseGamemode("c", mode) && mode == 1);
    CHECK(parseGamemode("1", mode) && mode == 1);
    CHECK(!parseGamemode("adventure", mode));
    CHECK(!parseGamemode("2", mode));
    CHECK(!parseGamemode("", mode));

    CHECK(strcmp(gamemodeName(0), "Survival") == 0);
    CHECK(strcmp(gamemodeName(1), "Creative") == 0);
    CHECK(strcmp(gamemodeName(2), "?") == 0);
}

static void test_name_spellings()
{
    char folded[64];

    normaliseName("Cobblestone", folded, sizeof(folded));
    CHECK(strcmp(folded, "cobblestone") == 0);

    // The three separators a player might type all fold away.
    normaliseName("Redstone Torch", folded, sizeof(folded));
    CHECK(strcmp(folded, "redstonetorch") == 0);
    normaliseName("redstone_torch", folded, sizeof(folded));
    CHECK(strcmp(folded, "redstonetorch") == 0);
    normaliseName("redstone-torch", folded, sizeof(folded));
    CHECK(strcmp(folded, "redstonetorch") == 0);

    // A name that is only separators folds to nothing, and must never match.
    normaliseName("  _- ", folded, sizeof(folded));
    CHECK(folded[0] == '\0');
    CHECK(!namesMatch("  ", "stone"));
    CHECK(!namesMatch("", ""));

    CHECK(namesMatch("Cobblestone", "cobblestone"));
    CHECK(namesMatch("Redstone Torch", "redstone_torch"));
    CHECK(!namesMatch("Cobblestone", "stone"));
    // "Stone" must not match "Sandstone": the compare is of whole names, not of
    // substrings, which is what keeps /give honest.
    CHECK(!namesMatch("Stone", "Sandstone"));
    CHECK(!namesMatch("Stone", "Redstone Dust"));

    // Truncation is safe with a tiny buffer.
    normaliseName("Cobblestone", folded, 4);
    CHECK(strlen(folded) == 3);
    CHECK(folded[3] == '\0');
    normaliseName("stone", folded, 1);
    CHECK(folded[0] == '\0');
}

static void test_fill_volume()
{
    long volume = 0;

    CHECK(fillVolume(0, 0, 0, 0, 0, 0, volume) && volume == 1);
    CHECK(fillVolume(0, 0, 0, 3, 3, 3, volume) && volume == 64);
    // Order does not matter: the corners are just two points of a box.
    CHECK(fillVolume(3, 3, 3, 0, 0, 0, volume) && volume == 64);
    CHECK(fillVolume(-3, -3, -3, 0, 0, 0, volume) && volume == 64);

    // The cap is what stops a mistyped corner from freezing the calculator.
    CHECK(fillVolume(0, 0, 0, 11, 11, 11, volume));       // 1728, just under
    CHECK(volume == 1728);
    CHECK(!fillVolume(0, 0, 0, 12, 12, 12, volume));      // 2197, over
    CHECK(volume == 2197);
    CHECK(!fillVolume(-1000000, 0, 0, 1000000, 1, 1, volume)); // far over, no overflow
    CHECK(volume > MaxFillBlocks);

    // An empty box cannot exist, but a degenerate one is still a box.
    CHECK(fillVolume(0, 0, 0, -1, 0, 0, volume));
    CHECK(volume == 2);
}

static void test_command_table()
{
    CHECK(commandCount() >= 10);

    for(int i = 0; i < commandCount(); ++i)
    {
        const char *name = commandName(i);
        CHECK(name[0] != '\0');
        CHECK(commandUsage(i)[0] != '\0');
        CHECK(commandSummary(i)[0] != '\0');
        // Names are stored lowercase and hold no spaces, so the parser's output
        // compares directly.
        for(const char *c = name; *c != '\0'; ++c)
            CHECK(*c >= 'a' && *c <= 'z');

        // Every command resolves back to itself, and unique names mean no command
        // is shadowed by an earlier one.
        CHECK(commandIndex(name) == i);
        for(int j = i + 1; j < commandCount(); ++j)
            CHECK(strcmp(name, commandName(j)) != 0);
    }

    CHECK(isKnownCommand("give"));
    CHECK(isKnownCommand("GIVE"));
    CHECK(!isKnownCommand("explode"));
    CHECK(!isKnownCommand(""));
    CHECK(commandIndex("nonsense") == -1);

    // The short spellings the game has always used.
    CHECK(commandIndex("tp") == commandIndex("teleport"));
    CHECK(commandIndex("gm") == commandIndex("gamemode"));
    CHECK(commandIndex("?") == commandIndex("help"));
    CHECK(commandIndex("giveitem") == commandIndex("give"));

    // Prefix matching is what the console's hint line is built on.
    const int give = commandIndex("give");
    CHECK(commandMatchesPrefix(give, ""));
    CHECK(commandMatchesPrefix(give, "g"));
    CHECK(commandMatchesPrefix(give, "gi"));
    CHECK(!commandMatchesPrefix(give, "t"));
    CHECK(!commandMatchesPrefix(give, "gives"));
    CHECK(!commandMatchesPrefix(-1, "g"));
    CHECK(!commandMatchesPrefix(commandCount(), "g"));
}

static void test_help_formatting()
{
    char line[128];

    const int teleport = commandIndex("teleport");
    formatHelp(teleport, line, sizeof(line));
    CHECK(strcmp(line, "/teleport <x> <y> <z>") == 0);

    // Every command's help line fits the console's own column.
    for(int i = 0; i < commandCount(); ++i)
    {
        formatHelp(i, line, sizeof(line));
        CHECK(line[0] == '/');
        CHECK(strlen(line) < sizeof(line));
    }

    // Out of range and tiny buffers are handled rather than trusted.
    formatHelp(-1, line, sizeof(line));
    CHECK(line[0] == '\0');
    formatHelp(commandCount(), line, sizeof(line));
    CHECK(line[0] == '\0');

    char tight[8];
    formatHelp(teleport, tight, sizeof(tight));
    CHECK(strlen(tight) == sizeof(tight) - 1);
    CHECK(tight[sizeof(tight) - 1] == '\0');

    char list[512];
    formatHelpAll(list, sizeof(list));
    CHECK(list[0] == '/');
    CHECK(strstr(list, "/give") != nullptr);
    CHECK(strstr(list, "/gamemode") != nullptr);
    CHECK(strlen(list) < sizeof(list));

    // The list is clipped instead of overflowing a short line.
    char short_list[16];
    formatHelpAll(short_list, sizeof(short_list));
    CHECK(strlen(short_list) < sizeof(short_list));
    CHECK(short_list[0] == '/');
}

int main()
{
    test_empty_lines();
    test_parsing_basics();
    test_quoting();
    test_overlong_input();
    test_numbers();
    test_booleans();
    test_coordinates();
    test_time();
    test_weather_and_gamemode();
    test_name_spellings();
    test_fill_volume();
    test_command_table();
    test_help_formatting();

    printf("command_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
