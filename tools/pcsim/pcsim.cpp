/*
 * Headless SDL for Muntcraft's desktop build.
 *
 * This file implements the small slice of SDL 1.2 that the game calls (see
 * tools/pcsim/SDL/SDL.h) on top of a plain memory buffer, a script and a
 * virtual clock:
 *
 *   - SDL_SetVideoMode hands back a 16-bit surface in the same RGB565 layout
 *     nGL writes, so the renderer is untouched,
 *   - the clock advances a fixed step per frame instead of following wall time,
 *     which makes a run reproducible and independent of how fast the machine is,
 *   - keys, the mouse and its look deltas come from a scenario file, so a tour
 *     of the game can be scripted,
 *   - frames named by the scenario are written out as PNG files.
 *
 * Run it through tools/pcsim/play.sh; see tools/pcsim/README.md.
 */

#include "SDL/SDL.h"

#include <zlib.h>

#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>

/** Implemented in probe.cpp: prints the real game's state (player, item, clock). */
void pcsim_probe(const char *tag);
/** Implemented in probe.cpp: a height map of the columns around the player. */
void pcsim_probe_scan(int radius);
/** Implemented in probe.cpp: runs a console command through runCommand(). */
void pcsim_run_command(const char *line);
/** Implemented in probe.cpp: points the camera at an exact yaw and pitch. */
void pcsim_aim(int yaw, int pitch);
/** Implemented in probe.cpp: prints the clock, both bodies and the light level. */
void pcsim_sky(const char *tag);
/** Implemented in probe.cpp: applies damage, so a screen reached by dying (the
 *  death screen) can be captured without scripting a fall. */
void pcsim_hurt(int amount);

namespace
{

unsigned long g_virtual_ms = 0;
unsigned long g_frame = 0;
unsigned long g_step_ms = 33; // one simulation tick (task.h: simulation_tick_ms)
bool g_initialized = false;
bool g_quit_requested = false; // the scenario has ended
bool g_quit_sent = false;      // an SDL_QUIT event has been handed to the game
unsigned long g_quit_frame = 0;
std::string g_out_dir = "sim-out";

SDL_Surface g_surface;
std::vector<uint16_t> g_pixels;
SDL_PixelFormat g_format = {16, 2};

Uint8 g_keys[SDLK_LAST];
float g_mouse_x = 0.0f, g_mouse_y = 0.0f;
float g_rel_x = 0.0f, g_rel_y = 0.0f; // this frame's look delta, set by `look`
float g_rel_accum_x = 0.0f, g_rel_accum_y = 0.0f;
// Frames of look left, or -1 for "until the scenario says otherwise". A turn is
// a speed times a frame count, so counting the frames here is what lets a
// scenario ask for a whole number of degrees: 8 px per frame is 8/3 degrees, and
// 135 frames of it is exactly one turn.
int g_look_frames = -1;
Uint8 g_mouse_buttons = 0;

/* --- scenario ------------------------------------------------------------ */

struct Entry
{
    double time = 0.0; // virtual seconds
    std::string command;
    std::vector<std::string> args;
};

std::vector<Entry> g_script;
size_t g_next_entry = 0;
double g_last_entry_time = 0.0;

struct Capture
{
    std::string label;
    std::string file;
    // The caption is bound here, when the scenario asks for the frame, not when
    // the frame is rendered. A shot is written by the *next* SDL_UpdateRect, so
    // reading g_caption at that point would hand the capture whatever text the
    // scenario had set in the meantime (it did: a caption two lines below its
    // shot described the frame after it).
    std::string caption;
};

std::vector<Capture> g_capture_queue;
std::vector<std::string> g_manifest;
std::string g_caption; // shown beside the next captures in the gallery
int g_clip_index = 0;
std::vector<int> g_pending_releases; // keycodes released at the next frame
std::string g_type_queue;            // text to type, one character per frame

/* --- small helpers ------------------------------------------------------- */

/**
 * A divider by zero (or a wild pointer) in the game would otherwise just say
 * "Floating point exception" and leave the interesting part out. Printing a
 * backtrace here turns a crash the harness caused into a line number.
 */
void crashHandler(int sig)
{
    void *frames[32];
    const int count = backtrace(frames, 32);
    fprintf(stderr, "\n[sim] signal %d after %lu frames\n", sig, g_frame);
    backtrace_symbols_fd(frames, count, 2);
    fflush(nullptr);
    _exit(3);
}

void ensureDir(const std::string &path)
{
    std::string partial;
    for(size_t i = 0; i <= path.size(); ++i)
    {
        if(i == path.size() || path[i] == '/')
        {
            if(!partial.empty() && partial != ".")
                mkdir(partial.c_str(), 0755); // EEXIST is fine
        }
        if(i < path.size())
            partial += path[i];
    }
}

int keyCode(const std::string &name)
{
    if(name.size() == 1)
    {
        const char c = name[0];
        if(c >= 'a' && c <= 'z')
            return SDLK_a + (c - 'a');
        if(c >= 'A' && c <= 'Z')
            return SDLK_a + (c - 'A');
        if(c >= '0' && c <= '9')
            return SDLK_0 + (c - '0');
        if(c == ' ')
            return SDLK_SPACE;
    }

    if(name == "space") return SDLK_SPACE;
    if(name == "enter" || name == "ret" || name == "return") return SDLK_RETURN;
    if(name == "kp_enter") return SDLK_KP_ENTER;
    if(name == "esc" || name == "escape") return SDLK_ESCAPE;
    if(name == "tab") return SDLK_TAB;
    if(name == "backspace" || name == "del") return SDLK_BACKSPACE;
    if(name == "shift") return SDLK_LSHIFT;
    if(name == "ctrl") return SDLK_LCTRL;
    if(name == "meta" || name == "alt") return SDLK_LMETA;
    if(name == "up") return SDLK_UP;
    if(name == "down") return SDLK_DOWN;
    if(name == "left") return SDLK_LEFT;
    if(name == "right") return SDLK_RIGHT;
    if(name == "slash" || name == "/") return SDLK_SLASH;
    if(name == "period" || name == "dot" || name == ".") return SDLK_PERIOD;
    if(name == "comma" || name == ",") return SDLK_COMMA;
    if(name == "minus" || name == "-") return SDLK_MINUS;
    if(name == "plus" || name == "+") return SDLK_PLUS;
    if(name == "equals" || name == "=") return SDLK_EQUALS;
    if(name == "colon" || name == ":") return SDLK_COLON;
    if(name == "lbracket" || name == "[") return SDLK_LEFTBRACKET;
    if(name == "rbracket" || name == "]") return SDLK_RIGHTBRACKET;

    fprintf(stderr, "[sim] unknown key '%s'\n", name.c_str());
    return 0;
}

int buttonMask(const std::string &name)
{
    if(name == "left") return SDL_BUTTON(SDL_BUTTON_LEFT);
    if(name == "middle") return SDL_BUTTON(SDL_BUTTON_MIDDLE);
    if(name == "right") return SDL_BUTTON(SDL_BUTTON_RIGHT);
    fprintf(stderr, "[sim] unknown mouse button '%s'\n", name.c_str());
    return 0;
}

/* --- PNG output ---------------------------------------------------------- */

void pngChunk(FILE *file, const char *type, const std::vector<uint8_t> &data)
{
    uint8_t length[4];
    const uint32_t n = static_cast<uint32_t>(data.size());
    length[0] = static_cast<uint8_t>(n >> 24);
    length[1] = static_cast<uint8_t>(n >> 16);
    length[2] = static_cast<uint8_t>(n >> 8);
    length[3] = static_cast<uint8_t>(n);
    fwrite(length, 1, 4, file);
    fwrite(type, 1, 4, file);
    if(!data.empty())
        fwrite(data.data(), 1, data.size(), file);

    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef *>(type), 4);
    if(!data.empty())
        crc = crc32(crc, reinterpret_cast<const Bytef *>(data.data()), static_cast<uInt>(data.size()));

    uint8_t tail[4];
    tail[0] = static_cast<uint8_t>(crc >> 24);
    tail[1] = static_cast<uint8_t>(crc >> 16);
    tail[2] = static_cast<uint8_t>(crc >> 8);
    tail[3] = static_cast<uint8_t>(crc);
    fwrite(tail, 1, 4, file);
}

/**
 * The surface is RGB565, which is what nGL packs its colours into (gl.cpp
 * colorRGB). It is widened to 8 bits per channel here so the PNG is readable
 * anywhere.
 */
bool writePng(const std::string &path)
{
    const int w = g_surface.w, h = g_surface.h;
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h) * (1 + 3 * w));

    for(int y = 0; y < h; ++y)
    {
        raw.push_back(0); // filter: none
        const uint16_t *row = g_pixels.data() + static_cast<size_t>(y) * w;
        for(int x = 0; x < w; ++x)
        {
            const uint16_t c = row[x];
            const uint8_t r = static_cast<uint8_t>((c >> 11) & 0x1F);
            const uint8_t g = static_cast<uint8_t>((c >> 5) & 0x3F);
            const uint8_t b = static_cast<uint8_t>(c & 0x1F);
            raw.push_back(static_cast<uint8_t>((r << 3) | (r >> 2)));
            raw.push_back(static_cast<uint8_t>((g << 2) | (g >> 4)));
            raw.push_back(static_cast<uint8_t>((b << 3) | (b >> 2)));
        }
    }

    uLongf compressed_size = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> compressed(compressed_size);
    if(compress2(compressed.data(), &compressed_size, raw.data(),
                 static_cast<uLong>(raw.size()), 6) != Z_OK)
    {
        fprintf(stderr, "[sim] compression failed for %s\n", path.c_str());
        return false;
    }
    compressed.resize(compressed_size);

    FILE *file = fopen(path.c_str(), "wb");
    if(!file)
    {
        fprintf(stderr, "[sim] cannot write %s\n", path.c_str());
        return false;
    }

    const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(signature, 1, 8, file);

    std::vector<uint8_t> ihdr;
    const uint32_t wh[2] = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    for(int i = 0; i < 2; ++i)
    {
        ihdr.push_back(static_cast<uint8_t>(wh[i] >> 24));
        ihdr.push_back(static_cast<uint8_t>(wh[i] >> 16));
        ihdr.push_back(static_cast<uint8_t>(wh[i] >> 8));
        ihdr.push_back(static_cast<uint8_t>(wh[i]));
    }
    ihdr.push_back(8); // bit depth
    ihdr.push_back(2); // colour type: truecolour
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    pngChunk(file, "IHDR", ihdr);
    pngChunk(file, "IDAT", compressed);
    pngChunk(file, "IEND", std::vector<uint8_t>());

    fclose(file);
    return true;
}

/* --- scenario driving ---------------------------------------------------- */

void applyEntry(const Entry &entry)
{
    const std::string &cmd = entry.command;

    if(cmd == "step" && entry.args.size() == 1)
    {
        const long ms = atol(entry.args[0].c_str());
        g_step_ms = ms > 0 ? static_cast<unsigned long>(ms) : 1;
        if(g_step_ms > 1000)
            g_step_ms = 1000;
    }
    else if(cmd == "key" && entry.args.size() == 2)
    {
        const int code = keyCode(entry.args[0]);
        if(code == 0)
            return;
        if(entry.args[1] == "down")
            g_keys[code] = 1;
        else if(entry.args[1] == "up")
            g_keys[code] = 0;
        else if(entry.args[1] == "tap")
        {
            g_keys[code] = 1;
            g_pending_releases.push_back(code);
        }
        else
            fprintf(stderr, "[sim] key needs down/up/tap, got '%s'\n", entry.args[1].c_str());
    }
    else if(cmd == "look" && entry.args.size() >= 2)
    {
        g_rel_x = static_cast<float>(atof(entry.args[0].c_str()));
        g_rel_y = static_cast<float>(atof(entry.args[1].c_str()));
        g_look_frames = entry.args.size() >= 3 ? atoi(entry.args[2].c_str()) : -1;
    }
    else if(cmd == "mpos" && entry.args.size() == 2)
    {
        g_mouse_x = static_cast<float>(atof(entry.args[0].c_str()));
        g_mouse_y = static_cast<float>(atof(entry.args[1].c_str()));
    }
    else if(cmd == "btn" && entry.args.size() == 2)
    {
        const int mask = buttonMask(entry.args[0]);
        if(mask == 0)
            return;
        if(entry.args[1] == "down")
            g_mouse_buttons |= static_cast<Uint8>(mask);
        else if(entry.args[1] == "up")
            g_mouse_buttons &= static_cast<Uint8>(~mask);
        else if(entry.args[1] == "tap")
        {
            g_mouse_buttons |= static_cast<Uint8>(mask);
            // Released on the next frame; the game reads the button state, so a
            // tap has to survive exactly one logic tick.
            g_pending_releases.push_back(-mask);
        }
    }
    else if(cmd == "shot")
    {
        const std::string label = entry.args.empty() ? "shot" : entry.args[0];
        Capture capture;
        capture.label = label;
        capture.file = label;
        capture.caption = g_caption;
        g_capture_queue.push_back(capture);
    }
    else if(cmd == "clip" && entry.args.size() >= 2)
    {
        const std::string label = entry.args[0];
        int count = atoi(entry.args[1].c_str());
        const int stride = entry.args.size() >= 3 ? atoi(entry.args[2].c_str()) : 1;
        if(count < 1)
            count = 1;
        for(int i = 0; i < count; ++i)
        {
            char name[128];
            snprintf(name, sizeof(name), "%s_%03d", label.c_str(), i);
            Capture capture;
            capture.label = label;
            capture.file = name;
            capture.caption = g_caption;
            g_capture_queue.push_back(capture);
            // The stride is honoured by skipping frames in SDL_UpdateRect.
            if(stride > 1 && i + 1 < count)
                for(int skip = 1; skip < stride; ++skip)
                {
                    Capture skipped;
                    skipped.label.clear();
                    skipped.file.clear();
                    g_capture_queue.push_back(skipped);
                }
        }
        g_clip_index = 0;
    }
    else if(cmd == "echo")
    {
        std::string text;
        for(size_t i = 0; i < entry.args.size(); ++i)
            text += (i ? " " : "") + entry.args[i];
        printf("[sim] t=%6.2fs %s\n", g_virtual_ms / 1000.0, text.c_str());
        fflush(stdout);
    }
    else if(cmd == "scan")
    {
        pcsim_probe_scan(entry.args.empty() ? 6 : atoi(entry.args[0].c_str()));
    }
    else if(cmd == "caption")
    {
        g_caption.clear();
        for(size_t i = 0; i < entry.args.size(); ++i)
            g_caption += (i ? " " : "") + entry.args[i];
    }
    else if(cmd == "command")
    {
        std::string text;
        for(size_t i = 0; i < entry.args.size(); ++i)
            text += (i ? " " : "") + entry.args[i];
        pcsim_run_command(text.c_str());
    }
    else if(cmd == "probe")
    {
        std::string text;
        for(size_t i = 0; i < entry.args.size(); ++i)
            text += (i ? " " : "") + entry.args[i];
        pcsim_probe(text.c_str());
    }
    else if(cmd == "aim" && entry.args.size() >= 2)
    {
        pcsim_aim(atoi(entry.args[0].c_str()), atoi(entry.args[1].c_str()));
    }
    else if(cmd == "sky")
    {
        std::string text;
        for(size_t i = 0; i < entry.args.size(); ++i)
            text += (i ? " " : "") + entry.args[i];
        pcsim_sky(text.c_str());
    }
    else if(cmd == "hurt")
    {
        pcsim_hurt(entry.args.empty() ? 1 : atoi(entry.args[0].c_str()));
    }
    else if(cmd == "type")
    {
        // One character per frame, which is exactly the edge a text key needs:
        // down for one tick, up for the next.
        for(size_t i = 0; i < entry.args.size(); ++i)
        {
            if(i)
                g_type_queue += ' ';
            g_type_queue += entry.args[i];
        }
    }
    else if(cmd == "end")
    {
        g_quit_requested = true;
        g_quit_frame = g_frame;
    }
    else
        fprintf(stderr, "[sim] unknown command '%s'\n", cmd.c_str());
}

void loadScript(const char *path)
{
    FILE *file = fopen(path, "r");
    if(!file)
    {
        fprintf(stderr, "[sim] cannot open scenario %s\n", path);
        exit(2);
    }

    char line[1024];
    while(fgets(line, sizeof(line), file))
    {
        char *hash = strchr(line, '#');
        if(hash)
            *hash = '\0';

        Entry entry;
        char *cursor = line;
        char *time = strtok(cursor, " \t\r\n");
        if(!time)
            continue;

        entry.time = atof(time);
        char *word = strtok(nullptr, " \t\r\n");
        if(!word)
            continue;
        entry.command = word;

        while((word = strtok(nullptr, " \t\r\n")) != nullptr)
            entry.args.push_back(word);

        if(entry.time > g_last_entry_time)
            g_last_entry_time = entry.time;
        g_script.push_back(entry);
    }
    fclose(file);
}

void runDueEntries()
{
    const double now = g_virtual_ms / 1000.0;
    while(g_next_entry < g_script.size() && g_script[g_next_entry].time <= now)
    {
        applyEntry(g_script[g_next_entry]);
        ++g_next_entry;
    }
}

void writeManifest()
{
    if(g_manifest.empty())
        return;

    const std::string path = g_out_dir + "/manifest.txt";
    FILE *file = fopen(path.c_str(), "w");
    if(!file)
        return;
    for(size_t i = 0; i < g_manifest.size(); ++i)
        fprintf(file, "%s\n", g_manifest[i].c_str());
    fclose(file);
    printf("[sim] %zu frames captured, listed in %s\n", g_manifest.size(), path.c_str());
}

void ensureInit()
{
    if(g_initialized)
        return;
    g_initialized = true;
    atexit(writeManifest);

    signal(SIGFPE, crashHandler);
    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);

    const char *out = getenv("MUNT_SIM_SHOTS");
    if(out && *out)
        g_out_dir = out;

    const char *step = getenv("MUNT_SIM_STEP_MS");
    if(step && *step)
        g_step_ms = strtoul(step, nullptr, 10);

    const char *script = getenv("MUNT_SIM_SCRIPT");
    if(script && *script)
        loadScript(script);

    ensureDir(g_out_dir);
    runDueEntries(); // applies anything scheduled at t=0, e.g. the first shot

    printf("[sim] scenario: %s\n", script && *script ? script : "(none)");
    printf("[sim] frames go to %s, %lu ms per frame\n", g_out_dir.c_str(), g_step_ms);
    fflush(stdout);
}

} // namespace

/* --- SDL surface --------------------------------------------------------- */

extern "C" int SDL_Init(Uint32)
{
    ensureInit();
    return 0;
}

extern "C" int SDL_InitSubSystem(Uint32)
{
    ensureInit();
    return 0;
}

extern "C" void SDL_QuitSubSystem(Uint32) {}
extern "C" void SDL_Quit(void) {}

extern "C" SDL_Surface *SDL_SetVideoMode(int width, int height, int, Uint32 flags)
{
    ensureInit();

    g_pixels.assign(static_cast<size_t>(width) * height, 0);
    g_surface.flags = flags;
    g_surface.format = &g_format;
    g_surface.w = width;
    g_surface.h = height;
    g_surface.pitch = static_cast<Uint16>(width * 2);
    g_surface.pixels = g_pixels.data();

    g_mouse_x = width / 2.0f;
    g_mouse_y = height / 2.0f;

    return &g_surface;
}

extern "C" int SDL_LockSurface(SDL_Surface *)
{
    return 0;
}

extern "C" void SDL_UnlockSurface(SDL_Surface *) {}

extern "C" void SDL_UpdateRect(SDL_Surface *, Sint32, Sint32, Uint32, Uint32)
{
    // One call per rendered frame: nglDisplay() ends with it, and it is the one
    // place that sees the finished picture.
    if(g_capture_queue.empty())
        return;

    Capture capture = g_capture_queue.front();
    g_capture_queue.erase(g_capture_queue.begin());

    if(capture.file.empty())
        return; // a skipped frame of a strided clip

    const std::string path = g_out_dir + "/" + capture.file + ".png";
    if(writePng(path))
    {
        char line[1024];
        snprintf(line, sizeof(line), "%s|%s.png|%.2f|%s", capture.label.c_str(),
                 capture.file.c_str(), g_virtual_ms / 1000.0, capture.caption.c_str());
        g_manifest.push_back(line);
        printf("[shot] %-22s t=%6.2fs frame=%lu\n", capture.file.c_str(),
               g_virtual_ms / 1000.0, g_frame);
        fflush(stdout);
    }
}

/* --- timing -------------------------------------------------------------- */

extern "C" Uint32 SDL_GetTicks(void)
{
    ensureInit();
    return static_cast<Uint32>(g_virtual_ms);
}

extern "C" void SDL_Delay(Uint32)
{
    ensureInit();

    // A frame boundary: release what was tapped, then advance the clock and run
    // whatever the scenario scheduled for the new time. Doing it here means the
    // game's next logic() sees the new input, and a tap is visible to exactly one
    // tick (press on frame N, release on frame N+1).
    for(size_t i = 0; i < g_pending_releases.size(); ++i)
    {
        const int code = g_pending_releases[i];
        if(code > 0)
            g_keys[code] = 0;
        else
            g_mouse_buttons &= static_cast<Uint8>(~(-code));
    }
    g_pending_releases.clear();

    // Typing runs at half the frame rate on purpose: a key is pressed on an even
    // frame and released on the next one, so a doubled letter ("noon") is two
    // edges rather than one held key, which the game's edge detection needs.
    if(!g_type_queue.empty() && g_frame % 2 == 0)
    {
        const char next = g_type_queue[0];
        g_type_queue.erase(0, 1);
        const int code = keyCode(std::string(1, next));
        if(code != 0)
        {
            g_keys[code] = 1;
            g_pending_releases.push_back(code);
        }
    }

    g_virtual_ms += g_step_ms;
    ++g_frame; // frame boundaries are counted here, next to the clock

    // The look delta is a per-frame rate: the world task adds it to its angles
    // once per tick through SDL_GetRelativeMouseState().
    g_rel_accum_x += g_rel_x;
    g_rel_accum_y += g_rel_y;
    g_mouse_x += g_rel_x;
    g_mouse_y += g_rel_y;

    if(g_look_frames > 0 && --g_look_frames == 0)
    {
        g_rel_x = g_rel_y = 0.0f; // the turn is over; hold this heading
        g_look_frames = -1;
    }

    runDueEntries();

    if(!g_quit_requested && g_next_entry >= g_script.size()
       && g_virtual_ms / 1000.0 > g_last_entry_time + 2.0)
    {
        // The scenario ran out without an `end`: ask the game to quit the polite
        // way so main()'s teardown still runs.
        printf("[sim] scenario finished at frame %lu\n", g_frame);
        g_quit_requested = true;
        g_quit_frame = g_frame;
    }

    if(g_quit_requested && g_frame > g_quit_frame + 300)
    {
        printf("[sim] the game did not stop on its own, leaving after %lu frames\n", g_frame);
        exit(0);
    }

    if(getenv("MUNT_SIM_MAX_FRAMES") &&
       g_frame >= strtoul(getenv("MUNT_SIM_MAX_FRAMES"), nullptr, 10))
    {
        printf("[sim] frame limit reached\n");
        exit(0);
    }
}

unsigned long pcsim_frames() { return g_frame; }
unsigned long pcsim_virtual_ms() { return g_virtual_ms; }

/*
 * The game seeds its world with `srand(time(nullptr))`, which would make every
 * run of a tour a different world. The harness' object files are linked ahead of
 * libc, so this definition wins and the clock stands still: the same chunks, the
 * same terrain and the same screenshots every time. MUNT_SIM_TIME picks the
 * moment; the default is an ordinary Tuesday.
 */
extern "C" time_t time(time_t *out)
{
    time_t fixed = 1700000000;
    if(const char *env = getenv("MUNT_SIM_TIME"))
    {
        const long long value = atoll(env);
        if(value != 0)
            fixed = static_cast<time_t>(value);
    }

    if(out)
        *out = fixed;
    return fixed;
}

/* --- events -------------------------------------------------------------- */

extern "C" void SDL_PumpEvents(void) {}

extern "C" int SDL_PollEvent(SDL_Event *event)
{
    if(g_quit_requested && !g_quit_sent)
    {
        // Delivered once: task.cpp turns it into `Task::running = false`, so the
        // game leaves through its own main() teardown rather than being killed.
        g_quit_sent = true;
        if(event)
        {
            memset(event, 0, sizeof(*event));
            event->type = SDL_QUIT;
        }
        return 1;
    }
    return 0;
}

extern "C" Uint8 *SDL_GetKeyState(int *)
{
    return g_keys;
}

extern "C" Uint8 SDL_GetMouseState(int *x, int *y)
{
    if(x)
        *x = static_cast<int>(g_mouse_x);
    if(y)
        *y = static_cast<int>(g_mouse_y);
    return g_mouse_buttons;
}

extern "C" Uint8 SDL_GetRelativeMouseState(int *x, int *y)
{
    const int dx = static_cast<int>(g_rel_accum_x);
    const int dy = static_cast<int>(g_rel_accum_y);
    g_rel_accum_x -= dx;
    g_rel_accum_y -= dy;

    if(x)
        *x = dx;
    if(y)
        *y = dy;
    return g_mouse_buttons;
}

/* --- audio --------------------------------------------------------------- */

extern "C" int SDL_OpenAudio(SDL_AudioSpec *, SDL_AudioSpec *)
{
    // No device: the mixer in audio_output.cpp simply has nowhere to go, which
    // is the same situation as a desktop build with no sound card.
    return -1;
}

extern "C" void SDL_CloseAudio(void) {}
extern "C" void SDL_PauseAudio(int) {}
extern "C" void SDL_LockAudio(void) {}
extern "C" void SDL_UnlockAudio(void) {}
