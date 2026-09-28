#ifndef TEST_SDL_H
#define TEST_SDL_H

#include <stdint.h>
#include <string.h>

typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;

typedef struct SDL_AudioSpec
{
    int freq;
    Uint16 format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint32 size;
    void (*callback)(void *, Uint8 *, int);
    void *userdata;
} SDL_AudioSpec;

#define SDL_INIT_AUDIO 0x00000010u
#define AUDIO_S16SYS 0x8010u
#define SDL_memset memset

#ifdef __cplusplus
extern "C" {
#endif
int SDL_InitSubSystem(Uint32 flags);
void SDL_QuitSubSystem(Uint32 flags);
int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
void SDL_CloseAudio(void);
void SDL_PauseAudio(int pause_on);
void SDL_LockAudio(void);
void SDL_UnlockAudio(void);
#ifdef __cplusplus
}
#endif

void SDL_TestRunAudioCallback(Uint8 *stream, int length);
int SDL_TestAudioIsOpen();
int SDL_TestAudioIsPaused();
int SDL_TestLockDepth();
int SDL_TestNestedLockCount();

#endif // TEST_SDL_H
