#include "command.h"

#include <stdio.h>
#include <string.h>

#include "worldclock.h" // TicksPerDay, so the named times and the time units agree with the clock

namespace
{
    struct Entry
    {
        const char *name;
        const char *usage;
        const char *summary;
    };

    /**
     * The commands, in the order /help lists them. `usage` leaves the leading '/'
     * off, so the help line is the same however it is spelled, and the console can
     * print "/time <set|add> <value>" without repeating itself.
     */
    const Entry commands[] = {
        {"help",     "help [command]",                       "List the commands, or explain one"},
        {"give",     "give <block|item> [count]",            "Put a stack in the inventory"},
        {"time",     "time <set|add> <value>",               "Move the day/night clock"},
        {"weather",  "weather <clear|rain|thunder|snow> [secs]",  "Force the weather for a while"},
        {"teleport", "teleport <x> <y> <z>",                 "Move the player, ~ for relative"},
        {"setblock", "setblock <x> <y> <z> <block> [data]",  "Replace one block"},
        {"fill",     "fill <x1> <y1> <z1> <x2> <y2> <z2> <b>","Fill a box with a block"},
        {"summon",   "summon <mob> [x] [y] [z]",             "Spawn a mob near the player"},
        {"seed",     "seed",                                 "Show the world seed"},
        {"gamemode", "gamemode <survival|creative>",         "Switch the play mode"},
        {"enchant",  "enchant [1|2|3]",                      "Enchant the held item at a table"},
    };

    constexpr int command_count = static_cast<int>(sizeof(commands) / sizeof(commands[0]));

    struct Alias
    {
        const char *alias;
        const char *name;
    };

    /** Spellings the game has always used, kept alongside the full names. */
    const Alias aliases[] = {
        {"tp", "teleport"},
        {"gm", "gamemode"},
        {"giveitem", "give"},
        {"enchanting", "enchant"},
        {"?", "help"},
    };

    struct NamedTime
    {
        const char *name;
        unsigned int ticks;
    };

    /**
     * The named times. They are the clock's own constants rather than copies, so
     * /time can never drift from the positions the sky renderer uses.
     */
    const NamedTime times[] = {
        {"sunrise", WorldClock::TimeSunrise},
        {"dawn",    WorldClock::TimeSunrise},
        {"morning", 1000},
        {"day",     WorldClock::TimeSunrise},
        {"noon",    WorldClock::TimeNoon},
        {"midday",  WorldClock::TimeNoon},
        {"afternoon", 9000},
        {"sunset",  WorldClock::TimeSunset},
        {"dusk",    WorldClock::TimeDusk},
        {"evening", WorldClock::TimeDusk},
        {"night",   14000},
        {"midnight", WorldClock::TimeMidnight},
    };

    // Numbered exactly like Weather::State, which is what the weather override
    // stores, so the names and the states cannot drift apart.
    const char *weather_names[] = {"Clear", "Rain", "Thunderstorm", "Snow"};
    const char *gamemode_names[] = {"Survival", "Creative"};

    constexpr int weather_count = 4;
    constexpr int gamemode_count = 2;

    bool isSpace(char c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    char lower(char c)
    {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    /** Case-insensitive equality. The parser only ever compares command names. */
    bool equalsIgnoreCase(const char *a, const char *b)
    {
        while(*a != '\0' && *b != '\0')
        {
            if(lower(*a) != lower(*b))
                return false;
            ++a;
            ++b;
        }
        return *a == '\0' && *b == '\0';
    }

    /** Ticks in one unit of the compact "1d6h30m" time form. */
    bool unitTicks(char unit, unsigned int &ticks)
    {
        switch(unit)
        {
        case 'd': ticks = WorldClock::TicksPerDay; return true;
        case 'h': ticks = WorldClock::TicksPerDay / 24; return true;
        case 'm': ticks = WorldClock::TicksPerDay / 1440; return true;
        case 's': ticks = WorldClock::TicksPerDay / 86400; return true;
        case 't': ticks = 1; return true;
        default: return false;
        }
    }
}

namespace Command
{
    Result parse(const char *line, Parsed &out)
    {
        const char *cursor = line;

        // A line may arrive with its '/' already, either from a caller that stored
        // a whole command or from a player who typed one out of habit.
        while(isSpace(*cursor))
            ++cursor;
        if(*cursor == '/')
            ++cursor;

        char *token = out.name;
        int token_size = MaxTokenLength;
        int length = 0;
        bool quoted = false;
        bool started = false;
        out.arg_count = 0;

        for(;; ++cursor)
        {
            const char c = *cursor;
            const bool end = c == '\0';

            if(!end && c == '"' && !quoted && length == 0)
            {
                // Opening a quoted argument. An empty quoted argument is allowed,
                // which is how a command can be given a deliberately blank word.
                quoted = true;
                started = true;
                continue;
            }

            const bool separator = !end && (quoted ? c == '"' : isSpace(c));
            if(!end && !separator)
            {
                started = true;
                if(length >= token_size - 1)
                    return Result::TooLong;
                token[length++] = (token == out.name) ? lower(c) : c;
                continue;
            }

            if(quoted)
            {
                if(end)
                    return Result::TooLong; // an unterminated quote is a typo, not a word
                quoted = false;
                token[length] = '\0';
                continue; // the closing quote is not a separator on its own
            }

            if(started)
            {
                token[length] = '\0';

                if(token == out.name)
                {
                    if(out.arg_count == 0 && length == 0)
                        return Result::Empty;
                }
                else if(out.arg_count < MaxArgs)
                {
                    // Only the command name is case-folded; an argument may be a
                    // name the game displays with capitals, and the world side
                    // folds it when it compares.
                    ++out.arg_count;
                }
                else
                    return Result::TooLong;
            }

            if(end)
                break;

            // Move to the next argument. Once the args are full the loop keeps
            // counting them, which is what turns an eleventh into TooLong.
            if(out.arg_count < MaxArgs && out.name[0] != '\0')
            {
                token = out.args[out.arg_count];
                token_size = MaxTokenLength;
                length = 0;
                started = false;
                // A separator run skips the whole run; the outer loop advances one
                // character per pass, so an empty slot is filled on the next pass.
                while(isSpace(cursor[1]))
                    ++cursor;
                continue;
            }

            // Past the args: keep consuming the rest so the token count is known.
            token = out.args[MaxArgs - 1];
            token_size = MaxTokenLength;
            length = 0;
            started = false;
            while(isSpace(cursor[1]))
                ++cursor;
        }

        if(out.name[0] == '\0')
            return Result::Empty;

        return Result::Ok;
    }

    int commandCount()
    {
        return command_count;
    }

    const char *commandName(int index)
    {
        if(index < 0 || index >= command_count)
            return "";
        return commands[index].name;
    }

    const char *commandUsage(int index)
    {
        if(index < 0 || index >= command_count)
            return "";
        return commands[index].usage;
    }

    const char *commandSummary(int index)
    {
        if(index < 0 || index >= command_count)
            return "";
        return commands[index].summary;
    }

    int commandIndex(const char *name)
    {
        if(name == nullptr || name[0] == '\0')
            return -1;

        for(int i = 0; i < command_count; ++i)
        {
            if(equalsIgnoreCase(name, commands[i].name))
                return i;
        }

        for(unsigned int i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i)
        {
            if(equalsIgnoreCase(name, aliases[i].alias))
                return commandIndex(aliases[i].name);
        }

        return -1;
    }

    bool isKnownCommand(const char *name)
    {
        return commandIndex(name) >= 0;
    }

    bool commandMatchesPrefix(int index, const char *prefix)
    {
        if(index < 0 || index >= command_count)
            return false;

        for(const char *p = prefix; *p != '\0'; ++p)
        {
            if(commands[index].name[p - prefix] == '\0' || lower(*p) != commands[index].name[p - prefix])
                return false;
        }

        return true;
    }

    void formatHelp(int index, char *out, unsigned int size)
    {
        if(out == nullptr || size == 0)
            return;

        if(index < 0 || index >= command_count)
        {
            out[0] = '\0';
            return;
        }

        // snprintf rather than strcpy: the console's line is short but the sizes
        // are set by whoever draws it, and a help line must never overflow one.
        const int written = snprintf(out, size, "/%s", commands[index].usage);

        if(written < 0)
            out[0] = '\0';
        else if(static_cast<unsigned int>(written) >= size)
            out[size - 1] = '\0';
    }

    void formatHelpAll(char *out, unsigned int size)
    {
        if(out == nullptr || size == 0)
            return;

        unsigned int used = 0;
        out[0] = '\0';

        for(int i = 0; i < command_count; ++i)
        {
            const int written = snprintf(out + used, size - used, "%s/%s",
                                         used == 0 ? "" : " ", commands[i].name);
            if(written < 0 || used + static_cast<unsigned int>(written) >= size)
                break; // the list is clipped rather than overflowing the line
            used += static_cast<unsigned int>(written);
        }
    }

    bool parseInt(const char *text, int &out)
    {
        if(text == nullptr || text[0] == '\0')
            return false;

        const char *cursor = text;
        bool negative = false;

        if(*cursor == '-' || *cursor == '+')
        {
            negative = *cursor == '-';
            ++cursor;
        }

        if(*cursor < '0' || *cursor > '9')
            return false;

        long value = 0;
        while(*cursor >= '0' && *cursor <= '9')
        {
            value = value * 10 + (*cursor - '0');
            // 2147483647 fits a long everywhere this builds, and the check keeps a
            // silly number from wrapping into a valid-looking coordinate.
            if(value > 2147483647L)
                return false;
            ++cursor;
        }

        if(*cursor != '\0')
            return false;

        out = static_cast<int>(negative ? -value : value);
        return true;
    }

    bool parseUnsigned(const char *text, unsigned int &out)
    {
        if(text == nullptr || text[0] == '\0')
            return false;

        const char *cursor = text;
        if(*cursor < '0' || *cursor > '9')
            return false;

        unsigned long value = 0;
        while(*cursor >= '0' && *cursor <= '9')
        {
            value = value * 10 + static_cast<unsigned long>(*cursor - '0');
            if(value > 0xFFFFFFFFUL)
                return false;
            ++cursor;
        }

        if(*cursor != '\0')
            return false;

        out = static_cast<unsigned int>(value);
        return true;
    }

    bool parseBoolean(const char *text, bool &out)
    {
        if(text == nullptr)
            return false;

        static const char *const yes[] = {"on", "true", "yes", "1"};
        static const char *const no[] = {"off", "false", "no", "0"};

        for(unsigned int i = 0; i < sizeof(yes) / sizeof(yes[0]); ++i)
        {
            if(equalsIgnoreCase(text, yes[i]))
            {
                out = true;
                return true;
            }
        }

        for(unsigned int i = 0; i < sizeof(no) / sizeof(no[0]); ++i)
        {
            if(equalsIgnoreCase(text, no[i]))
            {
                out = false;
                return true;
            }
        }

        return false;
    }

    bool parseCoordinate(const char *text, int origin, int &out)
    {
        if(text == nullptr || text[0] == '\0')
            return false;

        if(text[0] == '~')
        {
            if(text[1] == '\0')
            {
                out = origin;
                return true;
            }

            int offset;
            if(!parseInt(text + 1, offset))
                return false;

            out = origin + offset;
            return true;
        }

        return parseInt(text, out);
    }

    bool parseTime(const char *text, unsigned int &ticks)
    {
        if(text == nullptr || text[0] == '\0')
            return false;

        for(unsigned int i = 0; i < sizeof(times) / sizeof(times[0]); ++i)
        {
            if(equalsIgnoreCase(text, times[i].name))
            {
                ticks = times[i].ticks;
                return true;
            }
        }

        // A bare number is a tick count, which is what the debug screen shows and
        // what /time add wants to be given back.
        unsigned int plain;
        if(parseUnsigned(text, plain))
        {
            ticks = plain % WorldClock::TicksPerDay;
            return true;
        }

        // "1d6h30m": one or more amount/unit pairs, summed. Anything left over
        // fails, so a typo like "12x" is rejected rather than read as noon.
        unsigned long total = 0;
        const char *cursor = text;
        int pairs = 0;

        while(*cursor != '\0')
        {
            if(*cursor < '0' || *cursor > '9')
                return false;

            unsigned long amount = 0;
            while(*cursor >= '0' && *cursor <= '9')
            {
                amount = amount * 10 + static_cast<unsigned long>(*cursor - '0');
                if(amount > 1000000UL)
                    return false;
                ++cursor;
            }

            // "12" with no unit is not part of the compact form; plain numbers
            // were handled above, so this is a typo.
            if(*cursor == '\0')
                return false;

            unsigned int per_unit;
            if(!unitTicks(lower(*cursor), per_unit))
                return false;

            total += amount * per_unit;
            if(total >= 1UL << 31)
                return false;

            ++cursor;
            ++pairs;
        }

        if(pairs == 0)
            return false;

        ticks = static_cast<unsigned int>(total % WorldClock::TicksPerDay);
        return true;
    }

    bool parseWeather(const char *text, int &state)
    {
        if(text == nullptr)
            return false;

        struct NamedWeather
        {
            const char *name;
            int state;
        };

        static const NamedWeather names[] = {
            {"clear", 0}, {"sunny", 0}, {"sun", 0},
            {"rain", 1}, {"raining", 1}, {"rainy", 1}, {"storm", 2},
            {"thunder", 2}, {"thunderstorm", 2}, {"lightning", 2},
            // Snow is a spell of its own rather than a state the clock picks, so
            // it is the way to see the flakes without first finding a cold biome.
            {"snow", 3}, {"snowing", 3}, {"snowy", 3},
        };

        for(unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        {
            if(equalsIgnoreCase(text, names[i].name))
            {
                state = names[i].state;
                return true;
            }
        }

        unsigned int numeric;
        if(parseUnsigned(text, numeric) && numeric < weather_count)
        {
            state = static_cast<int>(numeric);
            return true;
        }

        return false;
    }

    const char *weatherName(int state)
    {
        if(state < 0 || state >= weather_count)
            return "?";
        return weather_names[state];
    }

    bool parseGamemode(const char *text, int &mode)
    {
        if(text == nullptr)
            return false;

        struct NamedMode
        {
            const char *name;
            int mode;
        };

        static const NamedMode names[] = {
            {"survival", 0}, {"survive", 0}, {"s", 0},
            {"creative", 1}, {"c", 1}, {"creative_mode", 1},
        };

        for(unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        {
            if(equalsIgnoreCase(text, names[i].name))
            {
                mode = names[i].mode;
                return true;
            }
        }

        unsigned int numeric;
        if(parseUnsigned(text, numeric) && numeric < gamemode_count)
        {
            mode = static_cast<int>(numeric);
            return true;
        }

        return false;
    }

    const char *gamemodeName(int mode)
    {
        if(mode < 0 || mode >= gamemode_count)
            return "?";
        return gamemode_names[mode];
    }

    void normaliseName(const char *in, char *out, unsigned int size)
    {
        if(out == nullptr || size == 0)
            return;

        unsigned int used = 0;

        for(const char *cursor = in; cursor != nullptr && *cursor != '\0'; ++cursor)
        {
            const char c = *cursor;
            // The separators are the whole point: they differ between the name the
            // game displays ("Spider Web") and the one a player types.
            if(c == ' ' || c == '_' || c == '-')
                continue;
            if(used + 1 >= size)
                break;
            out[used++] = lower(c);
        }

        out[used] = '\0';
    }

    bool namesMatch(const char *a, const char *b)
    {
        if(a == nullptr || b == nullptr)
            return false;

        // Normalised names are never longer than their input, and MaxTokenLength
        // is what the parser hands over.
        char left[MaxTokenLength * 2];
        char right[MaxTokenLength * 2];
        normaliseName(a, left, sizeof(left));
        normaliseName(b, right, sizeof(right));

        if(left[0] == '\0')
            return false;

        return strcmp(left, right) == 0;
    }

    namespace
    {
        /**
         * One axis of the box. The subtraction is done in 64 bits and the result
         * is capped immediately: two coordinates at opposite ends of an int span
         * 2^32 blocks, which a 32-bit long cannot hold, and any axis longer than
         * the cap has already failed, so the exact length past it does not matter.
         */
        long axisSpan(int a, int b)
        {
            long long d = static_cast<long long>(a) - static_cast<long long>(b);
            if(d < 0)
                d = -d;
            ++d;
            return d > MaxFillBlocks ? MaxFillBlocks + 1 : static_cast<long>(d);
        }
    }

    bool fillVolume(int x1, int y1, int z1, int x2, int y2, int z2, long &volume)
    {
        const long dx = axisSpan(x1, x2);
        const long dy = axisSpan(y1, y2);
        const long dz = axisSpan(z1, z2);

        if(dx > MaxFillBlocks || dy > MaxFillBlocks || dz > MaxFillBlocks)
        {
            volume = MaxFillBlocks + 1;
            return false;
        }

        // Multiplying in stages with the cap checked in between is what keeps the
        // product inside a 32-bit long: the second factor is only reached once the
        // first one is known to be under the cap.
        long result = dx * dy;
        if(result > MaxFillBlocks)
        {
            volume = MaxFillBlocks + 1;
            return false;
        }

        result *= dz;
        volume = result;
        return result > 0 && result <= MaxFillBlocks;
    }
}
