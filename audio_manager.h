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

	// --- vanilla one-shots ---------------------------------------------------
	/**
	 * The cues the game itself raises, each named after the sound it is rather
	 * than after a sample id, so no gameplay code has to know the pack's ids.
	 *
	 * Every one of them plays the official sample when a pack is installed and
	 * falls back to the matching procedural tone when it is not: a build with no
	 * pack is quiet rather than wrong, and never silent. The tone fallbacks are
	 * the same ones the old `play(Event)` calls used, which is why that API is
	 * still here -- it is the no-pack path, not a second sound set.
	 */
	void uiClick();
	void playerHurt();
	/** An attack swing; `strong` picks the heavier of the two families. */
	void playerAttack(bool strong = false);
	/** Landing after a fall; `big` for a fall tall enough to hurt. */
	void playerFall(bool big);
	void playerEat();
	void playerLevelUp();
	void itemPickup();
	void chestOpen();
	void chestClose();
	void doorOpen();
	void doorClose();
	/** A creeper's fuse lighting. Vanilla's `random/fuse`. */
	void fuse(int distance = 0);
	/** A blast: vanilla's `random/explode`. Used by creepers and by TNT. */
	void explosion(int distance = 0);

	// --- music ---------------------------------------------------------------
	/**
	 * Vanilla keeps two separate music pools and never mixes them: the title
	 * screen (and the other front-end screens) draw on `music/menu*`, and a
	 * loaded world draws on the soundtrack. Picking the pool is therefore part
	 * of asking for music, and a scene change with a track already live stops it,
	 * the way vanilla's `MusicManager` does when the track's type no longer
	 * matches the screen.
	 */
	enum MusicScene
	{
		MusicSceneMenu,
		MusicSceneGame
	};

	bool startMusic(); // starts the next track, wrapping when the pool ends
	void stopMusic();
	/** True while a track is streaming, not merely started. */
	bool musicPlaying();
	unsigned int currentMusicTrack();
	/** Tracks in the pool the current scene draws on (falling back to the other). */
	unsigned int musicTrackCount();
	/** Re-reads the pack's music lists, for a pack opened after initialize(). */
	void rescanMusic();
	/**
	 * Vanilla's `MusicManager` shape: a screen that wants background music says
	 * so (the title screen's `music.menu`, the world's own tracks), a track plays
	 * to its end, and updateMusic() starts the next one after the random quiet
	 * spell vanilla leaves between two tracks. The sound test drives the music by
	 * hand and turns the desire off while it is open.
	 */
	void setMusicDesired(bool desired, MusicScene scene = MusicSceneGame);
	void updateMusic(unsigned int elapsed_ms);

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
