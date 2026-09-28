#include "audio_manager.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if(!(condition)) return false; } while(0)

namespace
{
bool pack_ready = false;

void resetVolumes()
{
    // The mixer is pumped by the game loop, and the audio clock consumes the
    // streamed rings, so every check that streams must pump first.
    GameAudio::pump();

    GameAudio::setMasterVolume(100);
    GameAudio::setCategoryVolume(GameAudio::CategoryUI, 70);
    GameAudio::setCategoryVolume(GameAudio::CategoryMusic, 45);
    GameAudio::setCategoryVolume(GameAudio::CategoryPlayer, 70);
    GameAudio::setCategoryVolume(GameAudio::CategoryBlocks, 70);
    GameAudio::setCategoryVolume(GameAudio::CategoryCombat, 70);
    GameAudio::stopAll();
}

bool anyNonzero(const int16_t *samples, size_t count)
{
    for(size_t i = 0; i < count; ++i)
        if(samples[i] != 0)
            return true;
    return false;
}

bool allZero(const int16_t *samples, size_t count)
{
    for(size_t i = 0; i < count; ++i)
        if(samples[i] != 0)
            return false;
    return true;
}

bool testEventToneAndDuration()
{
    resetVolumes();
    int16_t samples[1024] = {};
    CHECK(GameAudio::mixMono(nullptr, 10) == 0);
    CHECK(!GameAudio::outputAvailable());

    GameAudio::play(GameAudio::EventMenuSelect);
    CHECK(GameAudio::mixMono(samples, 512) == 512);
    CHECK(anyNonzero(samples, 360));
    CHECK(allZero(samples + 360, 152));
    CHECK(GameAudio::mixMono(samples + 512, 512) == 512);
    CHECK(allZero(samples + 512, 512));
    return true;
}

bool testVolumeControls()
{
    resetVolumes();
    int16_t samples[512] = {};

    GameAudio::setCategoryVolume(GameAudio::CategoryUI, 0);
    GameAudio::play(GameAudio::EventMenuSelect);
    GameAudio::mixMono(samples, 512);
    CHECK(allZero(samples, 512));

    GameAudio::stopAll();
    GameAudio::setCategoryVolume(GameAudio::CategoryUI, 70);
    GameAudio::setMasterVolume(0);
    GameAudio::play(GameAudio::EventMenuSelect);
    GameAudio::mixMono(samples, 512);
    CHECK(allZero(samples, 512));

    GameAudio::setMasterVolume(150);
    CHECK(GameAudio::masterVolume() == 100);
    GameAudio::setCategoryVolume(GameAudio::CategoryUI, 150);
    CHECK(GameAudio::categoryVolume(GameAudio::CategoryUI) == 100);
    CHECK(GameAudio::categoryVolume(GameAudio::CategoryCount) == 0);
    return true;
}

bool testMusicStartMuteAndStop()
{
    resetVolumes();
    int16_t samples[512] = {};

    if(!pack_ready)
        return true; // no audio pack next to the tests, nothing to stream

    GameAudio::setCategoryVolume(GameAudio::CategoryMusic, 0);
    CHECK(GameAudio::startMusic());
    for(int i = 0; i < 8; ++i)
        GameAudio::pump();
    GameAudio::mixMono(samples, 512);
    CHECK(allZero(samples, 512));

    GameAudio::setCategoryVolume(GameAudio::CategoryMusic, 100);
    GameAudio::mixMono(samples, 256);
    CHECK(anyNonzero(samples, 256));

    GameAudio::stopMusic();
    GameAudio::mixMono(samples, 512);
    CHECK(allZero(samples, 512));
    return true;
}

bool testMixAndStopAllClearsVoices()
{
    resetVolumes();
    int16_t samples[256] = {};
    GameAudio::play(GameAudio::EventMenuSelect);
    GameAudio::play(GameAudio::EventMenuSelect);
    GameAudio::play(GameAudio::EventMenuSelect);
    GameAudio::mixMono(samples, 1);
    CHECK(samples[0] > 0);

    GameAudio::stopAll();
    GameAudio::mixMono(samples, 256);
    CHECK(allZero(samples, 256));
    return true;
}
}

int main()
{
    GameAudio::initialize();
    pack_ready = GameAudio::packAvailable();
    printf("audio_manager_test (pack: %s)\n", GameAudio::packStatus());

    int failures = 0;
    if(!testEventToneAndDuration()) { printf("FAIL testEventToneAndDuration\n"); ++failures; }
    if(!testVolumeControls()) { printf("FAIL testVolumeControls\n"); ++failures; }
    if(!testMusicStartMuteAndStop()) { printf("FAIL testMusicStartMuteAndStop\n"); ++failures; }
    if(!testMixAndStopAllClearsVoices()) { printf("FAIL testMixAndStopAllClearsVoices\n"); ++failures; }
    printf("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
