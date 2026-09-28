#include <SDL/SDL.h>

namespace
{
SDL_AudioSpec audio_spec = {};
bool audio_open = false;
bool audio_paused = true;
int audio_lock_depth = 0;
int nested_audio_lock_count = 0;
}

extern "C" int SDL_InitSubSystem(Uint32 flags)
{
    return flags == SDL_INIT_AUDIO ? 0 : -1;
}

extern "C" void SDL_QuitSubSystem(Uint32)
{
}

extern "C" int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    if(desired == nullptr || desired->callback == nullptr)
        return -1;
    audio_spec = *desired;
    if(obtained != nullptr)
        *obtained = audio_spec;
    audio_open = true;
    audio_paused = true;
    return 0;
}

extern "C" void SDL_CloseAudio(void)
{
    audio_open = false;
    audio_spec = {};
}

extern "C" void SDL_PauseAudio(int pause_on)
{
    audio_paused = pause_on != 0;
}

extern "C" void SDL_LockAudio(void)
{
    if(audio_lock_depth > 0)
        ++nested_audio_lock_count;
    ++audio_lock_depth;
}

extern "C" void SDL_UnlockAudio(void)
{
    --audio_lock_depth;
}

void SDL_TestRunAudioCallback(Uint8 *stream, int length)
{
    if(audio_open && !audio_paused && audio_spec.callback != nullptr)
    {
        ++audio_lock_depth;
        audio_spec.callback(audio_spec.userdata, stream, length);
        --audio_lock_depth;
    }
}

int SDL_TestAudioIsOpen()
{
    return audio_open;
}

int SDL_TestAudioIsPaused()
{
    return audio_paused;
}

int SDL_TestLockDepth()
{
    return audio_lock_depth;
}

int SDL_TestNestedLockCount()
{
    return nested_audio_lock_count;
}
