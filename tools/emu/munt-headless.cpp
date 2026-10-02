/*
 * munt-headless -- scriptable headless frontend for the Firebird TI-Nspire
 * emulator, used by Muntcraft to capture real calculator screenshots from a
 * sandbox (no X server, no GUI, no window manager).
 *
 * It links against Firebird's emulation core, which is
 *   Copyright (C) Firebird Emu contributors, GPLv3.
 * so this file is GPLv3 too. It is a development tool only: it is not part of
 * the crafti.tns build and never runs on a calculator.
 *
 * What it adds over upstream's headless/main.cpp:
 *   * virtual-time scheduling (the emulator ticks every 10ms of virtual time),
 *     so screenshots and key presses are deterministic even in turbo mode
 *   * PPM screenshots of the emulated LCD (320x240, CX RGB565)
 *   * scripted key presses, including the CX touchpad (arrows + click)
 *   * pushing files onto the emulated calculator over the emulated USB link
 *   * creating a flash image (with an OS) for a chosen model, so a calculator
 *     dump of the flash is not needed
 *
 * Examples:
 *   munt-headless --model cx --create-flash cx.img --os TI-NspireCX-4.5.0.tcc
 *   munt-headless --boot1 boot1.img --flash cx.img --shot-dir shots \
 *                 --shot-at 3,10,25 --run 30
 *   munt-headless --boot1 boot1.img --flash cx.img \
 *                 --put crafti.tns:/ndless/startup/crafti.tns --run 60 \
 *                 --shot-dir shots --shot-at 20,30,45
 */

#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <unistd.h>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <zlib.h>

#include "core/cpu.h"
#include "core/debug.h"
#include "core/emu.h"
#include "core/keypad.h"
#include "core/lcd.h"
#include "core/mem.h"
#include "core/usblink.h"
#include "core/usblink_queue.h"
#include "keymap.h"

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

static bool opt_quiet = false;
static bool opt_realtime = false;
static double opt_run_ms = 30000.0; // --run <seconds>, virtual time
static std::string opt_shot_dir;    // --shot-dir <dir>
static std::vector<double> opt_shot_at_ms;
static double opt_shot_every_ms = 0.0; // --shot-every <seconds>
static std::vector<std::string> opt_put;    // local:remote
static std::vector<std::string> opt_mkdir;  // remote folder to create first
static std::vector<std::string> opt_install_os;
static std::vector<std::string> opt_keys; // "12.5:esc" or "12.5:esc=down"
static std::vector<std::string> opt_type; // "5:hello", typed 120ms per char
static bool opt_shot_png = true;          // PNG by default, PPM with --shot-format ppm
static std::vector<std::string> opt_dump_mem; // "0x11000000:256:file.bin"
static std::string lcd_dump_path;         // --dump-lcd <file>
static double opt_put_at_ms = 20000.0;    // --put-at <seconds>
static std::string opt_live_path;         // --live <file>
static double opt_live_interval_ms = 125.0; // --live-fps
static std::string opt_input_dir;         // --input-dir <dir>
static bool opt_kpc_dump = false;         // --kpc-dump <ms>: print the keypad controller
static double opt_kpc_dump_ms = 0.0;
static double last_kpc_dump_ms = -1e9;
static bool usb_connected = false;
static bool transfers_queued = false;
static double last_usb_connect_ms = -1e9;

static const double TICK_MS = 10.0; // one scheduler tick == 10ms virtual time

// Virtual time counts the emulator's 100Hz scheduler ticks, so scripts and
// screenshots behave the same at any emulation speed.
static double vtime_ms = 0;
static unsigned long frames = 0;

// ---------------------------------------------------------------------------
// GUI callbacks required by the emulation core
// ---------------------------------------------------------------------------

void do_stuff(int i) { (void) i; }

void gui_debug_vprintf(const char *fmt, va_list ap)
{
    if(opt_quiet)
        return;
    vfprintf(stderr, fmt, ap);
}

void gui_debug_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    gui_debug_vprintf(fmt, ap);
    va_end(ap);
}

void gui_status_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void gui_perror(const char *msg)
{
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
}

void gui_debugger_entered_or_left(bool entered) { (void) entered; }

void gui_debugger_request_input(debug_input_cb callback)
{
    if(!callback)
        return;
    static char debug_in[40];
    if(!fgets(debug_in, sizeof(debug_in), stdin))
        debug_in[0] = '\0';
    callback(debug_in);
}

void gui_putchar(char c) { fputc(c, stdout); }
int gui_getchar() { return -1; }
void gui_set_busy(bool busy) { (void) busy; }
void gui_usblink_changed(bool state)
{
    usb_connected = state;
    fprintf(stderr, "[emu] t=%.1fs usblink %s\n", vtime_ms / 1000.0, state ? "connected" : "disconnected");
}

static void transfer_progress(int progress, void *)
{
    if(progress < 0)
        fprintf(stderr, "[emu] t=%.1fs transfer failed\n", vtime_ms / 1000.0);
    else if(progress == 100)
        fprintf(stderr, "[emu] t=%.1fs transfer complete\n", vtime_ms / 1000.0);
}

static double last_reported_speed = 0;

void gui_show_speed(double speed)
{
    last_reported_speed = speed;
}

void throttle_timer_off() {}
void throttle_timer_on() {}

void throttle_timer_wait(unsigned int usec)
{
    if(!opt_realtime)
        return;
    struct timespec ts = { (time_t)(usec / 1000000), (long)((usec % 1000000) * 1000) };
    nanosleep(&ts, nullptr);
}

// ---------------------------------------------------------------------------
// Virtual time
// ---------------------------------------------------------------------------

struct TimedKey {
    double t_ms;
    int id;       // keymap id, or a negative touchpad id (see named_keys)
    bool down;
};

struct TimedShot {
    double t_ms;
    std::string path;
};

static std::vector<TimedKey> keys;
static std::vector<TimedShot> shots;
static size_t next_key = 0, next_shot = 0;

// ---------------------------------------------------------------------------
// Key injection
// ---------------------------------------------------------------------------

enum {
    PAD_UP = -1,
    PAD_DOWN = -2,
    PAD_LEFT = -3,
    PAD_RIGHT = -4,
    PAD_CLICK = -5,
};

struct NamedKey {
    const char *name;
    int id;
};

// Letters and digits live in the "alpha" plane of the keypad; the ids are not
// in arithmetic order, so they are listed explicitly.
static const int letter_ids[26] = {
    keymap::aa, keymap::ab, keymap::ac, keymap::ad, keymap::ae, keymap::af,
    keymap::ag, keymap::ah, keymap::ai, keymap::aj, keymap::ak, keymap::al,
    keymap::am, keymap::an, keymap::ao, keymap::ap, keymap::aq, keymap::ar,
    keymap::as, keymap::at, keymap::au, keymap::av, keymap::aw, keymap::ax,
    keymap::ay, keymap::az,
};

static const int digit_ids[10] = {
    keymap::n0, keymap::n1, keymap::n2, keymap::n3, keymap::n4,
    keymap::n5, keymap::n6, keymap::n7, keymap::n8, keymap::n9,
};

static const NamedKey named_keys[] = {
    { "esc", keymap::esc },     { "tab", keymap::tab },
    { "menu", keymap::menu },   { "doc", keymap::doc },
    { "enter", keymap::enter }, { "ret", keymap::ret },
    { "space", keymap::space }, { "del", keymap::del },
    { "ctrl", keymap::ctrl },   { "shift", keymap::shift },
    { "on", keymap::on },       { "pad", keymap::pad },
    { "cat", keymap::cat },     { "var", keymap::var },
    { "flag", keymap::flag },   { "dot", keymap::dot },
    { "minus", keymap::minus }, { "plus", keymap::plus },
    { "up", PAD_UP },           { "down", PAD_DOWN },
    { "left", PAD_LEFT },       { "right", PAD_RIGHT },
    { "click", PAD_CLICK },
};

static void touchpad(int x, int y, bool down)
{
    keypad.touchpad_x = (uint16_t) x;
    keypad.touchpad_y = (uint16_t) y;
    if(down)
        keypad.touchpad_contact = keypad.touchpad_down = true;
    else
        keypad.touchpad_contact = keypad.touchpad_down = false;
    keypad.kpc.gpio_int_active |= 0x800;
    keypad_int_check();
}

static void apply_key(int id, bool down)
{
    switch(id)
    {
    case PAD_UP:    touchpad(TOUCHPAD_X_MAX / 2, down ? TOUCHPAD_Y_MAX : TOUCHPAD_Y_MAX / 2, down); return;
    case PAD_DOWN:  touchpad(TOUCHPAD_X_MAX / 2, down ? 0 : TOUCHPAD_Y_MAX / 2, down); return;
    case PAD_LEFT:  touchpad(down ? 0 : TOUCHPAD_X_MAX / 2, TOUCHPAD_Y_MAX / 2, down); return;
    case PAD_RIGHT: touchpad(down ? TOUCHPAD_X_MAX : TOUCHPAD_X_MAX / 2, TOUCHPAD_Y_MAX / 2, down); return;
    case PAD_CLICK: touchpad(TOUCHPAD_X_MAX / 2, TOUCHPAD_Y_MAX / 2, down); return;
    default:
        keypad_set_key(id / KEYPAD_COLS, id % KEYPAD_COLS, down);
        return;
    }
}

static bool lookup_key(const std::string &name, int &id)
{
    if(name.size() == 1)
    {
        char c = name[0];
        if(c >= 'a' && c <= 'z') { id = letter_ids[c - 'a']; return true; }
        if(c >= 'A' && c <= 'Z') { id = letter_ids[c - 'A']; return true; }
        if(c >= '0' && c <= '9') { id = digit_ids[c - '0']; return true; }
    }
    for(const NamedKey &k : named_keys)
        if(name == k.name) { id = k.id; return true; }
    return false;
}

// Schedule press+release for a key name; returns false if unknown.
static bool schedule_key(double t_ms, const std::string &name, bool down_only, bool up_only)
{
    int id;
    if(!lookup_key(name, id))
        return false;

    if(!up_only)
        keys.push_back({ t_ms, id, true });
    if(!down_only)
        keys.push_back({ t_ms + 150.0, id, false });
    return true;
}

// ---------------------------------------------------------------------------
// Screenshots
// ---------------------------------------------------------------------------

static void png_chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
    uint8_t sz[4] = { (uint8_t)(len >> 24), (uint8_t)(len >> 16), (uint8_t)(len >> 8), (uint8_t) len };
    fwrite(sz, 1, 4, f);
    fwrite(type, 1, 4, f);
    if(len)
        fwrite(data, 1, len, f);

    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef *) type, 4);
    if(len)
        crc = crc32(crc, data, (uInt) len);
    uint8_t cbuf[4] = { (uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t) crc };
    fwrite(cbuf, 1, 4, f);
}

// Minimal 8-bit truecolour PNG writer (the emulator already links zlib).
static bool write_png(const std::string &path, const uint8_t *rgb)
{
    const int w = 320, h = 240;
    FILE *f = fopen(path.c_str(), "wb");
    if(!f)
    {
        gui_perror(path.c_str());
        return false;
    }

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    fwrite(sig, 1, 8, f);

    uint8_t ihdr[13] = { 0 };
    ihdr[0] = (uint8_t)(w >> 24); ihdr[1] = (uint8_t)(w >> 16); ihdr[2] = (uint8_t)(w >> 8); ihdr[3] = (uint8_t) w;
    ihdr[4] = (uint8_t)(h >> 24); ihdr[5] = (uint8_t)(h >> 16); ihdr[6] = (uint8_t)(h >> 8); ihdr[7] = (uint8_t) h;
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 2;  // truecolour
    png_chunk(f, "IHDR", ihdr, sizeof(ihdr));

    std::vector<uint8_t> raw;
    raw.reserve((size_t) h * (w * 3 + 1));
    for(int y = 0; y < h; ++y)
    {
        raw.push_back(0); // filter: none
        raw.insert(raw.end(), rgb + (size_t) y * w * 3, rgb + (size_t)(y + 1) * w * 3);
    }

    uLongf bound = compressBound(raw.size());
    std::vector<uint8_t> z(bound);
    if(compress2(z.data(), &bound, raw.data(), raw.size(), 6) != Z_OK)
    {
        fclose(f);
        return false;
    }
    png_chunk(f, "IDAT", z.data(), bound);
    png_chunk(f, "IEND", nullptr, 0);
    fclose(f);
    return true;
}

// Turn the emulated panel into 8-bit RGB, the format every output uses.
static void render_panel(std::vector<uint8_t> &rgb)
{
    static std::vector<uint16_t> fb(320 * 240);
    lcd_cx_draw_frame(fb.data());

    rgb.resize(320 * 240 * 3);

    for(size_t i = 0; i < fb.size(); ++i)
    {
        uint16_t c = fb[i];
        uint8_t r, g, b;
        if(emulate_cx)
        {
            r = (uint8_t)((c >> 11 & 0x1F) * 255 / 31);
            g = (uint8_t)((c >> 5 & 0x3F) * 255 / 63);
            b = (uint8_t)((c & 0x1F) * 255 / 31);
        }
        else
        {
            // Same expansion the Qt frontend uses for classic/Touchpad panels
            uint8_t v4 = (uint8_t)(~(uint16_t)((c & 0xF) << 8 | (c & 0xF) << 4 | (c & 0xF)) & 0xFFF);
            r = (uint8_t)((v4 >> 8 & 0xF) * 17);
            g = (uint8_t)((v4 >> 4 & 0xF) * 17);
            b = (uint8_t)((v4 & 0xF) * 17);
        }
        rgb[i * 3] = r;
        rgb[i * 3 + 1] = g;
        rgb[i * 3 + 2] = b;
    }
}

static void write_shot(const std::string &path)
{
    std::vector<uint8_t> rgb;
    render_panel(rgb);

    if(opt_shot_png)
    {
        if(!write_png(path, rgb.data()))
            return;
    }
    else
    {
        FILE *f = fopen(path.c_str(), "wb");
        if(!f)
        {
            gui_perror(path.c_str());
            return;
        }
        fputs("P6\n320 240\n255\n", f);
        fwrite(rgb.data(), 1, rgb.size(), f);
        fclose(f);
    }

    fprintf(stderr, "[emu] t=%.1fs screenshot %s\n", vtime_ms / 1000.0, path.c_str());
}

// Live view: keep overwriting a single PNG. Written via a temporary file and
// renamed, so a reader never sees half a frame. The interval is measured in
// real time, so a viewer sees the same frame rate whatever speed the emulator
// runs at.
static std::chrono::steady_clock::time_point next_live_time;

static void update_live_frame()
{
    auto now = std::chrono::steady_clock::now();
    if(now < next_live_time)
        return;
    next_live_time = now + std::chrono::milliseconds((long) opt_live_interval_ms);

    static std::vector<uint8_t> rgb;
    render_panel(rgb);

    std::string tmp = opt_live_path + ".tmp";
    if(write_png(tmp, rgb.data()))
        rename(tmp.c_str(), opt_live_path.c_str());
}

// Live input: one command per file in a directory, so a writer never collides
// with the reader. "esc" presses and releases, "+up" presses, "-up" releases.
struct PendingRelease {
    double t_ms;
    int id;
};

static std::vector<PendingRelease> pending_releases;

static void poll_input()
{
    if(opt_input_dir.empty())
        return;

    DIR *dir = opendir(opt_input_dir.c_str());
    if(!dir)
        return;

    std::vector<std::string> commands;
    struct dirent *entry;
    while((entry = readdir(dir)) != nullptr)
    {
        std::string name = entry->d_name;
        if(name.size() > 4 && name.compare(name.size() - 4, 4, ".cmd") == 0)
            commands.push_back(opt_input_dir + "/" + name);
    }
    closedir(dir);

    std::sort(commands.begin(), commands.end()); // names are timestamped

    for(const std::string &path : commands)
    {
        std::string cmd;
        if(FILE *f = fopen(path.c_str(), "rb"))
        {
            char buf[64] = { 0 };
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            cmd.assign(buf, n);
            fclose(f);
        }
        unlink(path.c_str());

        while(!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r' || cmd.back() == ' '))
            cmd.pop_back();
        if(cmd.empty())
            continue;

        bool hold = false, release = false;
        if(cmd[0] == '+')
        {
            hold = true;
            cmd = cmd.substr(1);
        }
        else if(cmd[0] == '-')
        {
            release = true;
            cmd = cmd.substr(1);
        }

        int id;
        if(!lookup_key(cmd, id))
        {
            fprintf(stderr, "[emu] unknown key command '%s'\n", cmd.c_str());
            continue;
        }

        fprintf(stderr, "[emu] t=%.1fs live key %s%s\n", vtime_ms / 1000.0,
                hold ? "+" : (release ? "-" : ""), cmd.c_str());

        if(release)
            apply_key(id, false);
        else
        {
            apply_key(id, true);
            if(!hold) // "+name" stays held until a matching "-name"
                pending_releases.push_back({ vtime_ms + 150.0, id });
        }
    }
}

static void service_pending_releases()
{
    for(size_t i = 0; i < pending_releases.size();)
    {
        if(pending_releases[i].t_ms <= vtime_ms)
        {
            apply_key(pending_releases[i].id, false);
            pending_releases.erase(pending_releases.begin() + i);
        }
        else
            ++i;
    }
}

static std::string shot_path(double t_ms)
{
    char name[64];
    snprintf(name, sizeof(name), "shot-%06.1fs.%s", t_ms / 1000.0, opt_shot_png ? "png" : "ppm");
    return opt_shot_dir.empty() ? std::string(name) : opt_shot_dir + "/" + name;
}

// ---------------------------------------------------------------------------
// Main loop hooks
// ---------------------------------------------------------------------------

// Debugging aid: dump raw emulator memory (e.g. the framebuffer) at exit.
static void do_mem_dumps()
{
    for(const std::string &spec : opt_dump_mem)
    {
        size_t first = spec.find(':');
        size_t second = first == std::string::npos ? std::string::npos : spec.find(':', first + 1);
        if(first == std::string::npos || second == std::string::npos)
        {
            fprintf(stderr, "Bad --dump-mem entry '%s' (expected address:length:file)\n", spec.c_str());
            continue;
        }

        uint32_t addr = (uint32_t) strtoul(spec.substr(0, first).c_str(), nullptr, 0);
        uint32_t len = (uint32_t) strtoul(spec.substr(first + 1, second - first - 1).c_str(), nullptr, 0);
        std::string path = spec.substr(second + 1);

        void *p = phys_mem_ptr(addr, len);
        if(!p)
        {
            fprintf(stderr, "--dump-mem: 0x%X+%u is not mapped\n", addr, len);
            continue;
        }

        FILE *f = fopen(path.c_str(), "wb");
        if(!f)
        {
            gui_perror(path.c_str());
            continue;
        }
        fwrite(p, 1, len, f);
        fclose(f);
        fprintf(stderr, "[emu] dumped %u bytes at 0x%X to %s\n", len, addr, path.c_str());
    }
}

// Debugging aid: dump the panel exactly as the emulator renders it, as raw
// little-endian RGB565 pixels (320*240*2 bytes).
static void do_lcd_dump(bool at_exit)
{
    static bool done = false;
    if(done)
        return;
    done = true;

    static std::vector<uint16_t> fb(320 * 240);
    lcd_cx_draw_frame(fb.data());

    FILE *f = fopen(lcd_dump_path.c_str(), "wb");
    if(!f)
    {
        gui_perror(lcd_dump_path.c_str());
        return;
    }
    fwrite(fb.data(), 2, fb.size(), f);
    fclose(f);
    (void) at_exit;
    fprintf(stderr, "[emu] t=%.1fs panel dump %s\n", vtime_ms / 1000.0, lcd_dump_path.c_str());
}

// File transfers go over the emulated USB link, which the guest can only
// enumerate once its OS is running, so the actions are queued after boot. If
// the enumeration stalls (the OS was not ready yet) it is simply restarted.
static void service_transfers()
{
    if(opt_put.empty() && opt_install_os.empty() && opt_mkdir.empty())
        return;

    if(!transfers_queued)
    {
        if(vtime_ms < opt_put_at_ms)
            return;

        transfers_queued = true;
        for(const std::string &p : opt_install_os)
        {
            usblink_queue_send_os(p, transfer_progress, nullptr);
            fprintf(stderr, "[emu] t=%.1fs will install %s\n", vtime_ms / 1000.0, p.c_str());
        }
        for(const std::string &dir : opt_mkdir)
        {
            usblink_queue_new_dir(dir, transfer_progress, nullptr);
            fprintf(stderr, "[emu] t=%.1fs will create %s\n", vtime_ms / 1000.0, dir.c_str());
        }
        for(const std::string &p : opt_put)
        {
            size_t colon = p.find(':');
            std::string local = p.substr(0, colon), remote = p.substr(colon + 1);
            usblink_queue_put_file(local, remote, transfer_progress, nullptr);
            fprintf(stderr, "[emu] t=%.1fs will send %s -> %s\n", vtime_ms / 1000.0, local.c_str(), remote.c_str());
        }
        last_usb_connect_ms = vtime_ms;
        usblink_connect();
        return;
    }

    if(!usb_connected && usblink_queue_size() > 0 && vtime_ms - last_usb_connect_ms >= 2000)
    {
        last_usb_connect_ms = vtime_ms;
        fprintf(stderr, "[emu] t=%.1fs usblink not up yet, retrying enumeration\n", vtime_ms / 1000.0);
        usblink_connect();
    }
}

static void dump_kpc()
{
    // Columns of the keypad controller, which is what decides whether the OS
    // ever hears about a matrix key: int_active only moves when the controller
    // scans a row, so a press that never changes int_active is invisible.
    fprintf(stderr, "[kpc] t=%.2fs control=%08x size=%08x row=%u ie=%02x ia=%02x map0=%04x map6=%04x d0=%04x d6=%04x\n",
            vtime_ms / 1000.0, keypad.kpc.control, keypad.kpc.size, keypad.kpc.current_row,
            keypad.kpc.int_enable, keypad.kpc.int_active, keypad.key_map[0], keypad.key_map[6],
            keypad.kpc.data[0], keypad.kpc.data[6]);
}

static void run_scheduled_work()
{
    if(opt_kpc_dump && vtime_ms - last_kpc_dump_ms >= opt_kpc_dump_ms)
    {
        last_kpc_dump_ms = vtime_ms;
        dump_kpc();
    }

    service_transfers();
    poll_input();
    service_pending_releases();

    if(!opt_live_path.empty())
        update_live_frame();

    while(next_key < keys.size() && keys[next_key].t_ms <= vtime_ms)
    {
        const TimedKey &k = keys[next_key++];
        apply_key(k.id, k.down);
    }

    while(next_shot < shots.size() && shots[next_shot].t_ms <= vtime_ms)
        write_shot(shots[next_shot++].path);
}

// Keep virtual time in step with real time. Firebird's own throttle only kicks
// in when a virtual scheduler tick took less than 10ms of real time, which does
// not hold while the OS spins through its boot, so a live view is paced here.
static void pace_realtime()
{
    static std::chrono::steady_clock::time_point start;
    static bool started = false;
    if(!started)
    {
        start = std::chrono::steady_clock::now();
        started = true;
        return;
    }

    double wall_ms = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count() / 1000.0;
    double ahead_ms = vtime_ms - wall_ms;
    if(ahead_ms <= 2.0)
        return;
    if(ahead_ms > 50.0) // stay responsive instead of sleeping for ages
        ahead_ms = 50.0;

    struct timespec ts;
    ts.tv_sec = (time_t)(ahead_ms / 1000.0);
    ts.tv_nsec = (long)((ahead_ms - ts.tv_sec * 1000.0) * 1000000.0);
    nanosleep(&ts, nullptr);
}

void gui_do_stuff(bool wait)
{
    // Called from the emitter's 100Hz virtual-time event; the debugger also
    // calls it with wait=false, where no virtual time passes.
    if(!wait)
        return;

    vtime_ms += TICK_MS;
    frames++;

    if(opt_realtime)
        pace_realtime();

    run_scheduled_work();

    if(opt_run_ms >= 0 && vtime_ms >= opt_run_ms)
    {
        if(!opt_shot_dir.empty())
            write_shot(opt_shot_dir + (opt_shot_png ? "/final.png" : "/final.ppm"));
        if(!lcd_dump_path.empty())
            do_lcd_dump(true);
        do_mem_dumps();

        // The emulator buffers flash writes in memory (the GUI's "Save" action
        // flushes them), so without this anything the OS wrote - an installed
        // OS, transferred files - would be lost when we exit.
        if(flash_save_changes())
            fprintf(stderr, "[emu] flash changes saved to %s\n", path_flash.c_str());
        fprintf(stderr, "[emu] done: %.1fs virtual time (~%.1fx real speed)\n",
                vtime_ms / 1000.0, last_reported_speed);
        exiting = true;
    }
}

// ---------------------------------------------------------------------------
// Flash images
// ---------------------------------------------------------------------------

struct Model {
    const char *name;
    unsigned product;
    unsigned features;
};

static const Model models[] = {
    { "touchpad", 0x0E0, 0x000 },
    { "touchpadcas", 0x0C2, 0x000 },
    { "cx", 0x100, 0x005 },
    { "cxcas", 0x0F0, 0x005 },
    { "cx2", 0x1C0, 0x185 },
};

static bool find_model(const char *name, Model &out)
{
    for(const Model &m : models)
        if(!strcmp(m.name, name)) { out = m; return true; }
    return false;
}

static bool create_flash(const std::string &path, const Model &model,
                         const std::string &os, const std::string &boot2,
                         const std::string &diags, const std::string &manuf)
{
    const bool is_cx = model.product >= 0x0F0;
    std::string preload_str[4] = { manuf, boot2, diags, os };
    const char *preload[4] = { nullptr, nullptr, nullptr, nullptr };
    for(int i = 0; i < 4; ++i)
        if(!preload_str[i].empty())
            preload[i] = preload_str[i].c_str();

    uint8_t *nand_data = nullptr;
    size_t nand_size = 0;

    if(!flash_create_new(is_cx, preload, model.product, model.features, is_cx, &nand_data, &nand_size))
    {
        free(nand_data);
        return false;
    }

    FILE *f = fopen(path.c_str(), "wb");
    if(!f)
    {
        gui_perror(path.c_str());
        free(nand_data);
        return false;
    }
    bool ok = fwrite(nand_data, 1, nand_size, f) == nand_size;
    fclose(f);
    free(nand_data);

    if(ok)
        fprintf(stderr, "[emu] created %s: %s, product 0x%X, features 0x%X, %.1f MB%s\n",
                path.c_str(), model.name, model.product, model.features,
                nand_size / 1048576.0, os.empty() ? "" : " (with OS)");
    else
        gui_perror(path.c_str());

    return ok;
}

static void print_flash_type(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if(!f)
    {
        gui_perror(path.c_str());
        return;
    }
    std::string type = flash_read_type(f, false);
    fclose(f);
    printf("%s: %s\n", path.c_str(), type.empty() ? "unknown (not a flash image)" : type.c_str());
}

// ---------------------------------------------------------------------------
// Command line
// ---------------------------------------------------------------------------

static const char *usage =
    "munt-headless: scriptable headless Firebird emulator frontend\n"
    "\n"
    "  --boot1 <file>            Boot1 image (required, dumped from the calculator)\n"
    "  --flash <file>            Flash image (required unless --create-flash)\n"
    "  --snapshot <file>         Resume from a snapshot\n"
    "  --diags                   Use the diagnostics boot order\n"
    "  --realtime                Run at calculator speed instead of flat out\n"
    "  --quiet                   Do not print the emulator's debug output\n"
    "\n"
    "  --create-flash <file>     Write a new flash image and exit\n"
    "  --model <name>            touchpad | touchpadcas | cx | cxcas | cx2 (default cx)\n"
    "  --os <file>               TI OS file (.tnc/.tcc) to include in a created image\n"
    "  --boot2 <file>            Boot2 file to include in a created image\n"
    "  --diags-file <file>       Diagnostics file to include in a created image\n"
    "  --manuf <file>            Manufacturer data file to include in a created image\n"
    "  --info <file>             Print the type of a flash image and exit\n"
    "\n"
    "  --install-os <file>       Send an OS to the running calculator (OS install prompt)\n"
    "  --put <local>:<remote>    Push a file onto the calculator (repeatable)\n"
    "                            (paths are relative to the OS's documents folder,\n"
    "                             so /ndless/startup/x.tns, not /documents/...)\n"
    "  --mkdir <remote>          Create a folder on the calculator first (repeatable)\n"
    "  --put-at <seconds>        When to start USB transfers (default 20, after OS boot)\n"
    "  --keys <spec>             Key script: \"12.5:esc,13:enter\" virtual seconds (repeatable)\n"
    "  --keys-down <spec>        Same but only presses the keys\n"
    "  --type <spec>             Type text: \"5:hello world\" (repeatable)\n"
    "  --kpc-dump <seconds>      Print the keypad controller state at this interval\n"
    "  --shot-dir <dir>          Directory for screenshots (created by the caller)\n"
    "  --live <file>             Keep overwriting this PNG with the live panel\n"
    "  --live-fps <n>            Live frame rate in virtual seconds (default 8)\n"
    "  --input-dir <dir>         Watch this directory for *.cmd key commands\n"
    "                            (esc taps the key, +up/-up press and release)\n"
    "  --shot-at <t,t,...>       Virtual seconds to capture (default 3,10,20)\n"
    "  --shot-every <seconds>    Additionally capture at a fixed interval\n"
    "  --shot-format <png|ppm>   Screenshot format (default png)\n"
    "  --run <seconds>           Stop after this much virtual time (default 30)\n"
    "  --keep-running            Never stop on its own (use with an external timeout)\n"
    "                            (flash changes are only saved when this fires)\n"
    "  --debug-on-start          Enter the debugger on start\n"
    "  --debug-on-warn           Enter the debugger on warnings\n"
    "  --print-on-warn           Print warnings to the console\n"
    "  --dump-mem <a:len:file>   Dump emulator memory at exit, e.g. 0x11000000:512:fb.bin\n"
    "  --dump-lcd <file>         Dump the rendered panel (raw RGB565) at exit\n"
    "  --rampayload <file>       Load a raw ARM payload into RAM and jump to it\n"
    "  --rampayload-address <a>  Address for the RAM payload (default 0x10000000)\n"
    "  --help                    Show this help\n";

int main(int argc, char *argv[])
{
    const char *boot1 = nullptr, *flash = nullptr, *snapshot = nullptr, *rampayload = nullptr;
    uint32_t rampayload_base = 0x10000000;
    std::string create_flash_path, info_path, os_file, boot2_file, diags_file, manuf_file;
    Model model = models[2]; // cx
    bool keys_down_only = false;

    for(int i = 1; i < argc; ++i)
    {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : nullptr;
        auto need = [&](const char *name) -> const char * {
            if(!v)
            {
                fprintf(stderr, "%s requires a value\n", name);
                exit(1);
            }
            ++i;
            return v;
        };

        if(!strcmp(a, "--help")) { fputs(usage, stdout); return 0; }
        else if(!strcmp(a, "--boot1")) boot1 = need("--boot1");
        else if(!strcmp(a, "--flash")) flash = need("--flash");
        else if(!strcmp(a, "--snapshot")) snapshot = need("--snapshot");
        else if(!strcmp(a, "--rampayload")) rampayload = need("--rampayload");
        else if(!strcmp(a, "--rampayload-address")) rampayload_base = (uint32_t) strtoul(need("--rampayload-address"), nullptr, 0);
        else if(!strcmp(a, "--create-flash")) create_flash_path = need("--create-flash");
        else if(!strcmp(a, "--info")) info_path = need("--info");
        else if(!strcmp(a, "--os")) os_file = need("--os");
        else if(!strcmp(a, "--boot2")) boot2_file = need("--boot2");
        else if(!strcmp(a, "--diags-file")) diags_file = need("--diags-file");
        else if(!strcmp(a, "--manuf")) manuf_file = need("--manuf");
        else if(!strcmp(a, "--model"))
        {
            if(!find_model(need("--model"), model))
            {
                fprintf(stderr, "Unknown model '%s'\n", v);
                return 1;
            }
        }
        else if(!strcmp(a, "--install-os")) opt_install_os.push_back(need("--install-os"));
        else if(!strcmp(a, "--put")) opt_put.push_back(need("--put"));
        else if(!strcmp(a, "--put-at")) opt_put_at_ms = strtod(need("--put-at"), nullptr) * 1000.0;
        else if(!strcmp(a, "--mkdir")) opt_mkdir.push_back(need("--mkdir"));
        else if(!strcmp(a, "--dump-mem")) opt_dump_mem.push_back(need("--dump-mem"));
        else if(!strcmp(a, "--dump-lcd")) lcd_dump_path = need("--dump-lcd");
        else if(!strcmp(a, "--keys")) opt_keys.push_back(need("--keys"));
        else if(!strcmp(a, "--keys-down")) { keys_down_only = true; opt_keys.push_back(need("--keys-down")); }
        else if(!strcmp(a, "--type")) opt_type.push_back(need("--type"));
        else if(!strcmp(a, "--shot-dir")) opt_shot_dir = need("--shot-dir");
        else if(!strcmp(a, "--live")) opt_live_path = need("--live");
        else if(!strcmp(a, "--live-fps")) opt_live_interval_ms = 1000.0 / strtod(need("--live-fps"), nullptr);
        else if(!strcmp(a, "--input-dir")) opt_input_dir = need("--input-dir");
        else if(!strcmp(a, "--kpc-dump")) { opt_kpc_dump = true; opt_kpc_dump_ms = strtod(need("--kpc-dump"), nullptr) * 1000.0; }
        else if(!strcmp(a, "--shot-every")) opt_shot_every_ms = strtod(need("--shot-every"), nullptr) * 1000.0;
        else if(!strcmp(a, "--shot-format")) opt_shot_png = strcmp(need("--shot-format"), "ppm") != 0;
        else if(!strcmp(a, "--run")) opt_run_ms = strtod(need("--run"), nullptr) * 1000.0;
        else if(!strcmp(a, "--keep-running")) opt_run_ms = -1;
        else if(!strcmp(a, "--shot-at"))
        {
            std::string list = need("--shot-at");
            size_t pos = 0;
            while(pos <= list.size())
            {
                size_t comma = list.find(',', pos);
                std::string item = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                if(!item.empty())
                    opt_shot_at_ms.push_back(strtod(item.c_str(), nullptr) * 1000.0);
                if(comma == std::string::npos)
                    break;
                pos = comma + 1;
            }
        }
        else if(!strcmp(a, "--quiet")) opt_quiet = true;
        else if(!strcmp(a, "--realtime")) opt_realtime = true;
        else if(!strcmp(a, "--diags")) boot_order = ORDER_DIAGS;
        else if(!strcmp(a, "--debug-on-start")) debug_on_start = true;
        else if(!strcmp(a, "--debug-on-warn")) debug_on_warn = true;
        else if(!strcmp(a, "--print-on-warn")) print_on_warn = true;
        else
        {
            fprintf(stderr, "Unknown argument '%s'\n\n%s", a, usage);
            return 1;
        }
    }

    if(!info_path.empty())
    {
        print_flash_type(info_path);
        return 0;
    }

    if(!create_flash_path.empty())
        return create_flash(create_flash_path, model, os_file, boot2_file, diags_file, manuf_file) ? 0 : 1;

    if(!boot1 || !flash)
    {
        fputs("You need to specify at least --boot1 and --flash.\n\n", stderr);
        fputs(usage, stderr);
        return 2;
    }

    // Key script: "<seconds>:<key>[,<seconds>:<key>...]"
    for(const std::string &spec : opt_keys)
    {
        size_t pos = 0;
        while(pos < spec.size())
        {
            size_t comma = spec.find(',', pos);
            std::string item = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            size_t colon = item.find(':');
            if(colon == std::string::npos)
            {
                fprintf(stderr, "Bad --keys entry '%s' (expected seconds:key)\n", item.c_str());
                return 1;
            }
            double t_ms = strtod(item.substr(0, colon).c_str(), nullptr) * 1000.0;
            std::string name = item.substr(colon + 1);
            bool down = keys_down_only, up = false;
            if(!name.empty() && name[0] == '-') { down = false; up = true; name = name.substr(1); }
            if(!schedule_key(t_ms, name, down, up))
            {
                fprintf(stderr, "Unknown key name '%s'\n", name.c_str());
                return 1;
            }
            if(comma == std::string::npos)
                break;
            pos = comma + 1;
        }
    }

    // Type script: "<seconds>:<text>", 120ms per character
    for(const std::string &spec : opt_type)
    {
        size_t colon = spec.find(':');
        if(colon == std::string::npos)
        {
            fprintf(stderr, "Bad --type entry '%s' (expected seconds:text)\n", spec.c_str());
            return 1;
        }
        double t_ms = strtod(spec.substr(0, colon).c_str(), nullptr) * 1000.0;
        for(char c : spec.substr(colon + 1))
        {
            std::string name;
            if(c == ' ') name = "space";
            else if(c == '\n') name = "enter";
            else if(c == '.') name = "dot";
            else name = std::string(1, c);
            if(!schedule_key(t_ms, name, false, false))
                fprintf(stderr, "Cannot type character '%c'\n", c);
            t_ms += 120.0;
        }
    }

    // Screenshot schedule
    if(!opt_shot_dir.empty() && opt_shot_at_ms.empty() && opt_shot_every_ms <= 0)
        opt_shot_at_ms = { 3000.0, 10000.0, 20000.0 };
    for(double t : opt_shot_at_ms)
        shots.push_back({ t, shot_path(t) });
    if(opt_shot_every_ms > 0)
        for(double t = opt_shot_every_ms; opt_run_ms < 0 || t < opt_run_ms; t += opt_shot_every_ms)
            shots.push_back({ t, shot_path(t) });

    std::sort(keys.begin(), keys.end(), [](const TimedKey &a, const TimedKey &b) { return a.t_ms < b.t_ms; });
    std::sort(shots.begin(), shots.end(), [](const TimedShot &a, const TimedShot &b) { return a.t_ms < b.t_ms; });

    path_boot1 = boot1;
    path_flash = flash;

    if(!emu_start(0, 0, snapshot))
        return 1;

    fprintf(stderr, "[emu] %s, product 0x%X (%s)\n", flash, product, emulate_cx ? "CX" : "classic");

    if(rampayload)
    {
        FILE *f = fopen(rampayload, "rb");
        if(!f)
        {
            gui_perror("Could not open RAM payload");
            return 3;
        }
        fseek(f, 0, SEEK_END);
        size_t size = (size_t) ftell(f);
        rewind(f);
        void *target = phys_mem_ptr(rampayload_base, (uint32_t) size);
        if(!target)
        {
            fprintf(stderr, "RAM payload too big\n");
            fclose(f);
            return 5;
        }
        if(fread(target, size, 1, f) != 1)
        {
            gui_perror("Could not read RAM payload");
            fclose(f);
            return 4;
        }
        fclose(f);
        arm.reg[15] = rampayload_base;
    }

    for(const std::string &p : opt_put)
        if(p.find(':') == std::string::npos)
        {
            fprintf(stderr, "Bad --put entry '%s' (expected local:remote)\n", p.c_str());
            return 1;
        }

    turbo_mode = !opt_realtime;
    emu_loop(false);

    return 0;
}
