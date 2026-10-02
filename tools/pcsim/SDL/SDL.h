#ifndef PCSIM_SDL_H
#define PCSIM_SDL_H

/*
 * A tiny stand-in for SDL 1.2, used by the headless desktop harness in
 * tools/pcsim. It lets the real game (main.cpp, task.cpp, worldtask.cpp and the
 * nGL renderer, none of which are modified) run on a machine that has no SDL
 * installed: the video surface is a plain memory buffer, keys and the mouse come
 * from a script, and the clock is virtual, so a run is reproducible and can be
 * much faster than real time.
 *
 * Only the parts the game actually calls are provided -- the list was taken by
 * grepping the sources for SDL_. Anything else is a deliberate omission, not a
 * missing feature.
 *
 * Keycodes follow SDL 1.2: printable keys use their ASCII value (which is what
 * makes Task::textKeyPressed() and the command console work unchanged), and the
 * special keys use SDL's own numbering.
 */

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t Uint8;
typedef int8_t Sint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;

/* --- init flags and surface flags --------------------------------------- */
#define SDL_INIT_TIMER 0x00000001u
#define SDL_INIT_AUDIO 0x00000010u
#define SDL_INIT_VIDEO 0x00000020u

#define SDL_SWSURFACE 0x00000000u
#define SDL_HWSURFACE 0x00000001u

#define SDL_AUDIO_S16SYS 0x8010u
#define AUDIO_S16SYS SDL_AUDIO_S16SYS

/* --- video --------------------------------------------------------------- */

typedef struct SDL_PixelFormat
{
    Uint32 BitsPerPixel;
    Uint32 BytesPerPixel;
} SDL_PixelFormat;

typedef struct SDL_Surface
{
    Uint32 flags;
    SDL_PixelFormat *format;
    int w, h;
    Uint16 pitch;
    void *pixels;
} SDL_Surface;

typedef struct SDL_Rect
{
    Sint16 x, y;
    Uint16 w, h;
} SDL_Rect;

int SDL_Init(Uint32 flags);
int SDL_InitSubSystem(Uint32 flags);
void SDL_QuitSubSystem(Uint32 flags);
void SDL_Quit(void);

SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags);
int SDL_LockSurface(SDL_Surface *surface);
void SDL_UnlockSurface(SDL_Surface *surface);
void SDL_UpdateRect(SDL_Surface *surface, Sint32 x, Sint32 y, Uint32 w, Uint32 h);

/* --- timing ------------------------------------------------------------- */

Uint32 SDL_GetTicks(void);
void SDL_Delay(Uint32 ms);

/* --- events ------------------------------------------------------------- */

enum
{
    SDL_NOEVENT = 0,
    SDL_ACTIVEEVENT = 1,
    SDL_KEYDOWN = 2,
    SDL_KEYUP = 3,
    SDL_MOUSEMOTION = 4,
    SDL_MOUSEBUTTONDOWN = 5,
    SDL_MOUSEBUTTONUP = 6,
    SDL_QUIT = 12
};

typedef int SDLKey;

enum
{
    SDLK_BACKSPACE = 8,
    SDLK_TAB = 9,
    SDLK_RETURN = 13,
    SDLK_ESCAPE = 27,
    SDLK_SPACE = 32,
    SDLK_PLUS = 43,
    SDLK_MINUS = 45,
    SDLK_PERIOD = 46,
    SDLK_SLASH = 47,
    SDLK_0 = 48, SDLK_1, SDLK_2, SDLK_3, SDLK_4,
    SDLK_5, SDLK_6, SDLK_7, SDLK_8, SDLK_9,
    SDLK_COMMA = 44,
    SDLK_COLON = 59,
    SDLK_EQUALS = 61,
    SDLK_LEFTBRACKET = 91,
    SDLK_RIGHTBRACKET = 93,
    SDLK_a = 97, SDLK_b, SDLK_c, SDLK_d, SDLK_e, SDLK_f, SDLK_g, SDLK_h,
    SDLK_i, SDLK_j, SDLK_k, SDLK_l, SDLK_m, SDLK_n, SDLK_o, SDLK_p, SDLK_q,
    SDLK_r, SDLK_s, SDLK_t, SDLK_u, SDLK_v, SDLK_w, SDLK_x, SDLK_y, SDLK_z,
    SDLK_DELETE = 127,
    SDLK_KP_ENTER = 271,
    SDLK_UP = 273,
    SDLK_DOWN = 274,
    SDLK_RIGHT = 275,
    SDLK_LEFT = 276,
    SDLK_NUMLOCK = 300,
    SDLK_CAPSLOCK = 301,
    SDLK_SCROLLOCK = 302,
    SDLK_RSHIFT = 303,
    SDLK_LSHIFT = 304,
    SDLK_RCTRL = 305,
    SDLK_LCTRL = 306,
    SDLK_RALT = 307,
    SDLK_LALT = 308,
    SDLK_RMETA = 309,
    SDLK_LMETA = 310,
    SDLK_LAST = 323
};

typedef struct SDL_keysym
{
    Uint8 scancode;
    SDLKey sym;
    int mod;
    Uint16 unicode;
} SDL_keysym;

typedef struct SDL_KeyboardEvent
{
    Uint8 type;
    Uint8 state;
    SDL_keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_MouseButtonEvent
{
    Uint8 type;
    Uint8 button;
    Uint8 state;
    Uint16 x, y;
} SDL_MouseButtonEvent;

typedef struct SDL_MouseMotionEvent
{
    Uint8 type;
    Uint8 state;
    Uint16 x, y;
    Sint16 xrel, yrel;
} SDL_MouseMotionEvent;

typedef union SDL_Event
{
    Uint8 type;
    SDL_KeyboardEvent key;
    SDL_MouseButtonEvent button;
    SDL_MouseMotionEvent motion;
} SDL_Event;

void SDL_PumpEvents(void);
int SDL_PollEvent(SDL_Event *event);

/* --- input state --------------------------------------------------------- */

Uint8 *SDL_GetKeyState(int *numkeys);

#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT 3
#define SDL_BUTTON(X) (1u << ((X) - 1))

Uint8 SDL_GetMouseState(int *x, int *y);
Uint8 SDL_GetRelativeMouseState(int *x, int *y);

/* --- audio (the game keeps its mixer; this only has to accept a device) --- */

typedef struct SDL_AudioSpec
{
    int freq;
    Uint16 format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint32 size;
    void (*callback)(void *userdata, Uint8 *stream, int len);
    void *userdata;
} SDL_AudioSpec;

#define SDL_memset memset

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
void SDL_CloseAudio(void);
void SDL_PauseAudio(int pause_on);
void SDL_LockAudio(void);
void SDL_UnlockAudio(void);

/* --- the harness' own knobs, not part of SDL ---------------------------- */

/** Frames rendered so far, and the virtual clock they were rendered at. */
unsigned long pcsim_frames(void);
unsigned long pcsim_virtual_ms(void);

#ifdef __cplusplus
}
#endif

#endif // PCSIM_SDL_H
