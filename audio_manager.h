#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <stddef.h>
#include <stdint.h>

#include "audio_sounds.h"

/**
 * Crafti audio engine.
 *
 * Structure
 * ---------
 *  - The game loop calls the `play*` / `set*` helpers. They may touch the pack
 *    (filesystem) but never the voice arrays directly.
 *  - The output backend's audio clock (a timer interrupt on the calculator, the
 *    SDL callback on the desktop, or the simulated clock in the host tests)
 *    calls mixMono(). That side owns the voices and never does file IO.
 *  - pump() is called once per game frame. It only tops up the streaming rings,
 *    so playback speed is set by the audio clock and not by the frame rate.
 *
 * Categories and volumes follow Minecraft's model: a master volume multiplied by
 * a per-category volume, then by the sound's own normalised gain.
 */
namespace GameAudio
{
	enum Category
	{
		CategoryUI,
		CategoryMusic,
		CategoryAmbience,
		CategoryWeather,
		CategoryBlocks,
		CategoryFootsteps,
		CategoryMobs,
		CategoryPlayer,
		CategoryCombat,
		CategoryCount
	};

	/** Procedural fallback tones, used when no audio pack is installed. */
	enum Event
	{
		EventMenuSelect,
		EventJump,
		EventLand,
		EventBlockPlace,
		EventBlockBreak,
		EventPlayerDamage,
		EventMobHit,
		EventCount
	};

	/** Footstep/break material families present in the pack. */
	enum Material
	{
		MaterialGrass,
		MaterialStone,
		MaterialWood,
		MaterialGravel,
		MaterialSand,
		MaterialSnow,
		MaterialCloth,
		MaterialWetGrass,
		MaterialCoral,
		MaterialLadder,
		MaterialScaffold,
		MaterialCount
	};

	enum MobKind
	{
		MobChicken,
		MobCow,
		MobPig,
		MobSheep,
		MobZombie,
		MobSkeleton,
		MobCreeper,
		MobSpider,
		MobSlime,
		MobVillager,
		MobWolf,
		MobHorse,
		MobCount
	};

	static const unsigned int MixerSampleRate = 8000;
	static const unsigned int MixerVoiceCount = 8;

	/** Opens the audio pack and resets all mixer state. */
	void initialize();
	void shutdown();

	/** True when an audio pack was found and parsed. */
	bool packAvailable();
	/** Human readable pack status for the settings/test screens. */
	const char *packStatus();

	/** Recorder/attenuation helper: distance in blocks (attenuated over 32). */
	unsigned int distanceVolume(int distance);

	// --- one shots -----------------------------------------------------------
	bool playSound(unsigned int id);
	bool playSoundAt(unsigned int id, int distance);
	void footstep(Material material, int distance = 0);
	void digBlock(Material material);
	void placeBlock(Material material);
	void mobSound(MobKind kind, bool hurt, int distance = 0);
	bool playUiSound(unsigned int id);

	// --- music ---------------------------------------------------------------
	bool startMusic(); // starts the next track, wrapping when the pack ends
	void stopMusic();
	bool musicPlaying();
	unsigned int currentMusicTrack();
	unsigned int musicTrackCount();

	// --- ambience and weather ------------------------------------------------
	void setAmbience(unsigned int id);
	unsigned int ambience();
	void setWeather(unsigned int id, bool active);
	bool weatherActive();
	/** Weather thunder cue, timed by the caller. */
	void weatherThunder(int distance = 0);
	/** Occasional cave/underwater ambience cue, timed by the caller. */
	void ambienceCue(int distance = 0);

	// --- legacy procedural API ----------------------------------------------
	void play(Event event);
	void stopAll();
	void setMasterVolume(unsigned int percent);
	void setCategoryVolume(Category category, unsigned int percent);
	unsigned int masterVolume();
	unsigned int categoryVolume(Category category);

	// --- audio clock / game loop --------------------------------------------
	/** Game loop: refills streaming rings. Cheap and safe to call every frame. */
	void pump();
	/** Audio clock: mixes signed 16-bit mono PCM. Never does file IO. */
	size_t mixMono(int16_t *output, size_t frames);

	bool outputAvailable();
	unsigned int activeVoiceCount();
	unsigned int streamUnderruns();
}

#endif // AUDIO_MANAGER_H
