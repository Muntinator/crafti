#include "audio_manager.h"
#include "audio_output.h"
#include <SDL/SDL.h>

#include <stdint.h>

#define CHECK(condition) do { if(!(condition)) return false; } while(0)

namespace
{
int test_event_callback()
{
    int16_t samples[512] = {};
    GameAudio::stopAll();
    GameAudio::play(GameAudio::EventMenuSelect);
    SDL_TestRunAudioCallback(reinterpret_cast<Uint8 *>(samples), sizeof(samples));
    for(unsigned int i = 0; i < 300; ++i)
        if(samples[i] != 0)
            return true;
    return false;
}

bool test_backendLifecycleAndCallback()
{
    CHECK(!GameAudio::outputAvailable());
    CHECK(GameAudioOutput::initialize());
    CHECK(GameAudio::outputAvailable());
    CHECK(SDL_TestAudioIsOpen());
    CHECK(!SDL_TestAudioIsPaused());
    CHECK(SDL_TestLockDepth() == 0);
    CHECK(test_event_callback());
    CHECK(SDL_TestLockDepth() == 0);
    CHECK(SDL_TestNestedLockCount() == 0);

    GameAudio::stopAll();
    Uint8 odd_stream[2049];
    for(unsigned int i = 0; i < sizeof(odd_stream); ++i)
        odd_stream[i] = 0x7f;
    SDL_TestRunAudioCallback(odd_stream, sizeof(odd_stream));
    for(unsigned int i = 0; i < sizeof(odd_stream); ++i)
        CHECK(odd_stream[i] == 0);
    CHECK(SDL_TestLockDepth() == 0);
    CHECK(SDL_TestNestedLockCount() == 0);

    Uint8 invalid_stream[4] = {0x7f, 0x7f, 0x7f, 0x7f};
    SDL_TestRunAudioCallback(invalid_stream, -1);
    for(unsigned int i = 0; i < sizeof(invalid_stream); ++i)
        CHECK(invalid_stream[i] == 0x7f);

    GameAudioOutput::shutdown();
    CHECK(!GameAudio::outputAvailable());
    CHECK(!SDL_TestAudioIsOpen());
    CHECK(SDL_TestAudioIsPaused());
    CHECK(SDL_TestLockDepth() == 0);
    return true;
}
}

int main()
{
    return test_backendLifecycleAndCallback() ? 0 : 1;
}
