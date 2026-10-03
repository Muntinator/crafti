#include "audio_manager.h"
#include "audio_pack.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

bool testOutputIsAsLoudAsItCanBeSafely()
{
    // The last stage before the 1-bit modulator, where level *is* loudness: the
    // pin's swing is the whole of the output, so whatever the knee takes is gone
    // unless it is given back. The knee has to bend rather than clamp (a clamp is
    // audible crackle), and the make-up has to stop at the ceiling.
    resetVolumes();
    GameAudio::setMasterVolume(100);
    GameAudio::setCategoryVolume(GameAudio::CategoryPlayer, 100);

    int16_t samples[512] = {};

    // One loud voice. The make-up is ramped across a block so it cannot click, so
    // it takes a block or two to arrive; by the third it has put the voice back
    // on the ceiling it started above.
    GameAudio::play(GameAudio::EventPlayerDamage);
    int peak_one = 0;
    for(int block = 0; block < 3; ++block)
    {
        CHECK(GameAudio::mixMono(samples, 512) == 512);
        for(size_t i = 0; i < 512; ++i)
            CHECK(samples[i] <= 32767 && samples[i] >= -32768);
        for(size_t i = 0; i < 512; ++i)
            if(samples[i] > peak_one)
                peak_one = samples[i];
    }
    CHECK(peak_one >= 32000); // as loud as the output can be
    CHECK(peak_one <= 32767); // and not one sample past it

    // Four of the same voice, all in phase, sum to far past full scale: the knee
    // bends, the make-up lifts, and the result is still inside the rail.
    GameAudio::stopAll();
    GameAudio::mixMono(samples, 512);
    for(int i = 0; i < 4; ++i)
        GameAudio::play(GameAudio::EventPlayerDamage);

    int peak_four = 0;
    for(int block = 0; block < 3; ++block)
    {
        GameAudio::mixMono(samples, 512);
        for(size_t i = 0; i < 512; ++i)
        {
            CHECK(samples[i] <= 32767 && samples[i] >= -32768);
            if(samples[i] > peak_four)
                peak_four = samples[i];
        }
    }
    CHECK(peak_four >= peak_one);  // never quieter than one voice
    CHECK(peak_four <= 32767);     // and never past the ceiling, however loud

    // Quiet material must be left alone: the make-up only gives back what the
    // knee took, so a block that never reached the knee is not turned up to meet
    // the rail.
    GameAudio::stopAll();
    GameAudio::mixMono(samples, 512);
    GameAudio::setMasterVolume(20);
    GameAudio::play(GameAudio::EventPlayerDamage);
    int peak_quiet = 0;
    for(int block = 0; block < 3; ++block)
    {
        GameAudio::mixMono(samples, 512);
        for(size_t i = 0; i < 512; ++i)
            if(-samples[i] > peak_quiet)
                peak_quiet = -samples[i];
    }
    CHECK(peak_quiet > 2000);   // still audible
    CHECK(peak_quiet < 12000);  // but nowhere near the ceiling

    GameAudio::stopAll();
    GameAudio::mixMono(samples, 512);
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

typedef void (*Cue)();

struct CueCase
{
    Cue fire;
    GameAudio::Category category;
    const char *name;
};

// Drives `fire` and reports whether anything it started is audible over the
// next 512 ms. When `mute` is true the cue's own category is zeroed first, so
// the same call proves the sample is routed to the category it belongs to.
bool cueAudible(Cue fire, GameAudio::Category category, bool mute)
{
    resetVolumes();
    if(mute)
        GameAudio::setCategoryVolume(category, 0);

    fire();

    int16_t samples[512] = {};
    bool heard = false;
    for(int block = 0; block < 8; ++block)
    {
        GameAudio::mixMono(samples, 512);
        if(anyNonzero(samples, 512))
            heard = true;
    }
    return heard;
}

bool testVanillaCues()
{
    if(!pack_ready)
        return true; // no pack next to the tests: only the fallback tones exist

    // Every cue the game raises, with the mixer category the pack files it
    // under. A cue has to reach a real sample (audible with its category on)
    // and only that category (silent once the category is muted).
    const CueCase cases[] = {
        { [](){ GameAudio::uiClick(); }, GameAudio::CategoryUI, "uiClick" },
        { [](){ GameAudio::playerHurt(); }, GameAudio::CategoryCombat, "playerHurt" },
        { [](){ GameAudio::playerAttack(false); }, GameAudio::CategoryCombat, "playerAttack" },
        { [](){ GameAudio::playerFall(false); }, GameAudio::CategoryCombat, "playerFall" },
        { [](){ GameAudio::playerEat(); }, GameAudio::CategoryPlayer, "playerEat" },
        { [](){ GameAudio::playerLevelUp(); }, GameAudio::CategoryPlayer, "playerLevelUp" },
        { [](){ GameAudio::itemPickup(); }, GameAudio::CategoryPlayer, "itemPickup" },
        { [](){ GameAudio::chestOpen(); }, GameAudio::CategoryUI, "chestOpen" },
        { [](){ GameAudio::chestClose(); }, GameAudio::CategoryUI, "chestClose" },
        { [](){ GameAudio::doorOpen(); }, GameAudio::CategoryUI, "doorOpen" },
        { [](){ GameAudio::doorClose(); }, GameAudio::CategoryUI, "doorClose" }
    };

    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        if(!cueAudible(cases[i].fire, cases[i].category, false))
        {
            printf("cue %s produced no audio\n", cases[i].name);
            return false;
        }
        if(cueAudible(cases[i].fire, cases[i].category, true))
        {
            printf("cue %s is not routed to its category\n", cases[i].name);
            return false;
        }
    }
    return true;
}
}

// ---- the music manager policy -------------------------------------------

namespace
{
const char *fixture_path = "build/audio_manager_fixture.audp";

/** A tiny pack with two short music tracks and one ordinary sound. */
bool buildMusicFixture()
{
    const uint32_t sample_rate = 8000;
    const uint32_t sound_total = 3;
    const uint32_t lengths[3] = {400, 400, 200};
    const uint16_t flags[3] = {GameAudioPack::FlagMusic, GameAudioPack::FlagMusic, 0};
    const uint32_t index_offset = 24;
    const uint32_t data_offset = index_offset + 16 * sound_total;
    uint32_t data_size = 0;
    for(uint32_t sound = 0; sound < sound_total; ++sound)
        data_size += lengths[sound];

    FILE *file = fopen(fixture_path, "wb");
    if(file == nullptr)
        return false;

    uint8_t header[24];
    memcpy(header, "AUD1", 4);
    header[4] = 1; header[5] = 0;
    header[6] = static_cast<uint8_t>(sample_rate); header[7] = static_cast<uint8_t>(sample_rate >> 8);
    header[8] = 0; header[9] = 0;
    header[10] = static_cast<uint8_t>(sound_total); header[11] = 0;
    const uint32_t words[3] = {index_offset, data_offset, data_size};
    for(int w = 0; w < 3; ++w)
    {
        header[12 + w * 4] = static_cast<uint8_t>(words[w]);
        header[13 + w * 4] = static_cast<uint8_t>(words[w] >> 8);
        header[14 + w * 4] = static_cast<uint8_t>(words[w] >> 16);
        header[15 + w * 4] = static_cast<uint8_t>(words[w] >> 24);
    }
    fwrite(header, 1, sizeof(header), file);

    uint32_t offset = 0;
    for(uint32_t sound = 0; sound < sound_total; ++sound)
    {
        uint8_t raw[16] = {};
        raw[0] = static_cast<uint8_t>(offset);
        raw[1] = static_cast<uint8_t>(offset >> 8);
        raw[2] = static_cast<uint8_t>(offset >> 16);
        raw[3] = static_cast<uint8_t>(offset >> 24);
        raw[4] = static_cast<uint8_t>(lengths[sound]);
        raw[5] = static_cast<uint8_t>(lengths[sound] >> 8);
        raw[6] = static_cast<uint8_t>(lengths[sound] >> 16);
        raw[7] = static_cast<uint8_t>(lengths[sound] >> 24);
        raw[8] = 1; raw[9] = 0;
        raw[10] = static_cast<uint8_t>(flags[sound]);
        raw[11] = static_cast<uint8_t>(flags[sound] >> 8);
        raw[12] = 255; // gain
        fwrite(raw, 1, sizeof(raw), file);
        offset += lengths[sound];
    }

    for(uint32_t sound = 0; sound < sound_total; ++sound)
        for(uint32_t i = 0; i < lengths[sound]; ++i)
            fputc(static_cast<int>(128 + ((i + sound) % 100) - 50), file);

    fclose(file);
    return true;
}
}

bool testMusicManagerPolicy()
{
    resetVolumes();

    // The policy is checked against the tiny fixture pack on purpose: the real
    // pack's tracks are minutes long and would never reach their end inside a
    // host test, and the quiet spell after a track ends is the whole point here.
    if(!buildMusicFixture() || !GameAudioPack::open(fixture_path))
        return false;
    GameAudio::rescanMusic();
    CHECK(GameAudio::musicTrackCount() == 2);

    // A screen that drives the music by hand (the sound test) is left alone.
    GameAudio::setMusicDesired(false);
    GameAudio::stopMusic();
    GameAudio::updateMusic(60000);
    CHECK(!GameAudio::musicPlaying());

    // A screen that starts wanting music -- the title screen, a world -- gets a
    // track at once.
    GameAudio::setMusicDesired(true);
    GameAudio::updateMusic(16);
    CHECK(GameAudio::musicPlaying());

    // Pausing and unpausing does not skip the track: nothing asks for music
    // differently while the same track is live.
    GameAudio::setMusicDesired(true);
    GameAudio::updateMusic(16);
    CHECK(GameAudio::musicPlaying());

    // When the track ends, the next one waits for vanilla's quiet spell first:
    // seconds of silence, not an instant jukebox queue.
    int16_t samples[512] = {};
    for(int i = 0; i < 64 && GameAudio::musicPlaying(); ++i)
    {
        GameAudio::pump();
        GameAudio::mixMono(samples, 512);
    }
    CHECK(!GameAudio::musicPlaying()); // the fixture's tracks are short

    unsigned int waited = 0;
    while(!GameAudio::musicPlaying() && waited <= 60000)
    {
        GameAudio::updateMusic(1000);
        waited += 1000;
    }
    CHECK(GameAudio::musicPlaying()); // the next track, wrapping at the pack's end
    CHECK(waited >= 5000 && waited <= 21000);
    return true;
}

// ---- the two music pools ------------------------------------------------

bool testMusicScenePools()
{
    resetVolumes();
    GameAudio::stopMusic();

    // This one is about the split between vanilla's menu music and the world's
    // soundtrack, which only the real pack has both halves of. The tiny fixture
    // above has no `music/menu*` entry, so there is nothing to split there.
    if(!GameAudioPack::open(nullptr))
        return true;
    GameAudio::rescanMusic();
    CHECK(GameAudio::musicTrackCount() > 0);

    const unsigned int menu_tracks[4] = {
        GameAudio::Sound::MusicMenu1, GameAudio::Sound::MusicMenu2,
        GameAudio::Sound::MusicMenu3, GameAudio::Sound::MusicMenu4
    };
    const bool menu_pool_full = GameAudioPack::entry(menu_tracks[3]) != nullptr;
    if(!menu_pool_full)
        return true; // a pack without the menu tracks: nothing to check

    auto isMenuTrack = [&](unsigned int id) {
        for(int i = 0; i < 4; ++i)
            if(menu_tracks[i] == id)
                return true;
        return false;
    };

    // The title screen asks for the menu pool and gets one of the four menu
    // tracks -- never the soundtrack.
    GameAudio::setMusicDesired(true, GameAudio::MusicSceneMenu);
    GameAudio::updateMusic(16);
    CHECK(GameAudio::musicPlaying());
    CHECK(isMenuTrack(GameAudio::currentMusicTrack()));
    printf("    menu pool: %u tracks, playing %u\n", GameAudio::musicTrackCount(),
           GameAudio::currentMusicTrack());

    // Handing the music to a world swaps pools the way vanilla's MusicManager
    // does: the menu track stops at once and the soundtrack waits out the quiet
    // spell before it starts.
    GameAudio::setMusicDesired(true, GameAudio::MusicSceneGame);
    CHECK(!GameAudio::musicPlaying());

    unsigned int waited = 0;
    while(!GameAudio::musicPlaying() && waited <= 60000)
    {
        GameAudio::updateMusic(1000);
        waited += 1000;
    }
    CHECK(GameAudio::musicPlaying());
    CHECK(!isMenuTrack(GameAudio::currentMusicTrack()));
    CHECK(waited >= 5000 && waited <= 21000);
    printf("    game pool: %u tracks, playing %u after %u ms\n", GameAudio::musicTrackCount(),
           GameAudio::currentMusicTrack(), waited);

    GameAudio::stopMusic();
    return true;
}

int main()
{
    GameAudio::initialize();
    pack_ready = GameAudio::packAvailable();
    // The music policy needs real tracks to stream, so when no pack sits beside
    // the tests a tiny one is written and opened instead.
    if(!pack_ready && buildMusicFixture())
        pack_ready = GameAudioPack::open(fixture_path);
    printf("audio_manager_test (pack: %s)\n", GameAudio::packStatus());

    int failures = 0;
    if(!testEventToneAndDuration()) { printf("FAIL testEventToneAndDuration\n"); ++failures; }
    if(!testVolumeControls()) { printf("FAIL testVolumeControls\n"); ++failures; }
    if(!testOutputIsAsLoudAsItCanBeSafely()) { printf("FAIL testOutputIsAsLoudAsItCanBeSafely\n"); ++failures; }
    if(!testMusicStartMuteAndStop()) { printf("FAIL testMusicStartMuteAndStop\n"); ++failures; }
    if(!testMixAndStopAllClearsVoices()) { printf("FAIL testMixAndStopAllClearsVoices\n"); ++failures; }
    if(!testVanillaCues()) { printf("FAIL testVanillaCues\n"); ++failures; }
    if(!testMusicManagerPolicy()) { printf("FAIL testMusicManagerPolicy\n"); ++failures; }
    if(!testMusicScenePools()) { printf("FAIL testMusicScenePools\n"); ++failures; }
    printf("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
