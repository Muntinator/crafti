#include "audio_manager.h"
#include "audio_output.h"
#include "audio_pack.h"

#include <string.h>

namespace GameAudio
{
namespace
{
	/** Largest block the mixer produces in one call, and the stream ring headroom. */
	const unsigned int MaxMixFrames = 512;
	/** Commands queued from the game loop to the audio clock. */
	const unsigned int CommandRingSize = 32;
	/**
	 * The gain the limiter's make-up is ramping towards, and the most it will
	 * ever lift by (about +3 dB). See the make-up pass at the end of mixMono().
	 */
	double makeup_gain = 1.0;
	const double MakeupCap = 1.4125;

	struct Tone
	{
		uint16_t frequency;
		uint16_t duration_ms;
		uint8_t volume;
		Category category;
	};

	struct ToneVoice
	{
		uint32_t phase;
		uint32_t phase_step;
		uint32_t samples_left;
		uint32_t release_samples;
		uint8_t volume;
		uint8_t category;
		bool active;
	};

	struct SampleVoice
	{
		uint16_t slot;
		uint32_t frame;
		uint32_t length;
		uint32_t step; // 16.16 source step (pack rate -> mixer rate)
		uint32_t frac;
		uint32_t release;
		uint8_t volume;
		uint8_t category;
		uint8_t loop;
		bool active;
	};

	enum CommandType
	{
		CommandNone = 0,
		CommandPlaySample,
		CommandPlayTone,
		CommandStopCategory,
		CommandStopAll
	};

	struct Command
	{
		uint32_t type;
		uint32_t slot;
		uint32_t volume;
		uint8_t category;
		uint8_t loop;
	};

	struct StreamVoice
	{
		volatile uint32_t sound; // 0 = off; published by the game loop
		volatile uint32_t category;
		volatile uint32_t generation;
		uint32_t seen_generation;
		uint32_t fade; // fade-in frames still to run
		uint32_t fade_total;
		bool active;
	};

	const Tone event_tones[EventCount] = {
		{880, 45, 70, CategoryUI}, // menu select
		{660, 70, 65, CategoryPlayer}, // jump
		{150, 95, 75, CategoryPlayer}, // landing
		{520, 45, 55, CategoryBlocks}, // place
		{220, 85, 70, CategoryBlocks}, // break
		{110, 150, 90, CategoryPlayer}, // damage
		{330, 60, 75, CategoryCombat} // mob hit
	};

	// Material families, indexed the same way as enum Material.
	const uint16_t step_table[MaterialCount][3] = {
		{Sound::StepGrass1, Sound::StepGrass2, Sound::StepGrass3},
		{Sound::StepStone1, Sound::StepStone2, Sound::StepStone3},
		{Sound::StepWood1, Sound::StepWood2, Sound::StepWood3},
		{Sound::StepGravel1, Sound::StepGravel2, Sound::StepGravel3},
		{Sound::StepSand1, Sound::StepSand2, Sound::StepSand3},
		{Sound::StepSnow1, Sound::StepSnow2, Sound::StepSnow3},
		{Sound::StepCloth1, Sound::StepCloth2, Sound::StepCloth3},
		{Sound::StepWetGrass1, Sound::StepWetGrass2, Sound::StepWetGrass3},
		{Sound::StepCoral1, Sound::StepCoral2, Sound::StepCoral3},
		{Sound::StepLadder1, Sound::StepLadder2, Sound::StepLadder3},
		{Sound::StepScaffold1, Sound::StepScaffold2, Sound::StepScaffold3}
	};

	const uint16_t dig_table[MaterialCount][3] = {
		{Sound::DigGrass1, Sound::DigGrass2, Sound::DigGrass3},
		{Sound::DigStone1, Sound::DigStone2, Sound::DigStone3},
		{Sound::DigWood1, Sound::DigWood2, Sound::DigWood3},
		{Sound::DigGravel1, Sound::DigGravel2, Sound::DigGravel3},
		{Sound::DigSand1, Sound::DigSand2, Sound::DigSand3},
		{Sound::DigSnow1, Sound::DigSnow2, Sound::DigSnow3},
		{Sound::DigCloth1, Sound::DigCloth2, Sound::DigCloth3},
		{Sound::DigWetGrass1, Sound::DigWetGrass2, Sound::DigWetGrass3},
		{Sound::DigCoral1, Sound::DigCoral2, Sound::DigCoral3},
		{Sound::DigWood1, Sound::DigWood2, Sound::DigWood3},
		{Sound::DigWood1, Sound::DigWood2, Sound::DigWood3}
	};

	const uint16_t mob_say_table[MobCount][3] = {
		{Sound::MobChickenSay1, Sound::MobChickenSay2, Sound::MobChickenSay3},
		{Sound::MobCowSay1, Sound::MobCowSay2, Sound::MobCowSay3},
		{Sound::MobPigSay1, Sound::MobPigSay2, Sound::MobPigSay3},
		{Sound::MobSheepSay1, Sound::MobSheepSay2, Sound::MobSheepSay3},
		{Sound::MobZombieSay1, Sound::MobZombieSay2, Sound::MobZombieSay3},
		{Sound::MobSkeletonSay1, Sound::MobSkeletonSay2, Sound::MobSkeletonSay3},
		{Sound::MobCreeperSay1, Sound::MobCreeperSay2, Sound::MobCreeperSay3},
		{Sound::MobSpiderSay1, Sound::MobSpiderSay2, Sound::MobSpiderSay3},
		{Sound::MobSlimeSmall1, Sound::MobSlimeSmall2, Sound::MobSlimeSmall3},
		{Sound::MobVillagerIdle1, Sound::MobVillagerIdle2, Sound::MobVillagerIdle3},
		{Sound::MobWolfBark1, Sound::MobWolfBark2, Sound::MobWolfBark3},
		{Sound::MobHorseIdle1, Sound::MobHorseIdle2, Sound::MobHorseIdle3}
	};

	const uint16_t mob_hurt_table[MobCount] = {
		Sound::MobChickenHurt1,
		Sound::MobCowHurt1,
		Sound::MobPigDeath,
		Sound::MobSheepSay1,
		Sound::MobZombieHurt1,
		Sound::MobSkeletonHurt1,
		Sound::RandomClassicHurt,
		Sound::MobSpiderDeath,
		Sound::MobSlimeAttack1,
		Sound::MobVillagerHit1,
		Sound::MobWolfHurt1,
		Sound::MobHorseHit1
	};

	const uint16_t ambience_cues[] = {
		Sound::AmbientCaveCave1,
		Sound::AmbientCaveCave2,
		Sound::AmbientCaveCave3,
		Sound::AmbientUnderwaterEnter1,
		Sound::AmbientUnderwaterExit1
	};

	const uint16_t thunder_cues[] = {
		Sound::AmbientWeatherThunder1,
		Sound::AmbientWeatherThunder2,
		Sound::AmbientWeatherThunder3
	};

	ToneVoice tone_voices[MixerVoiceCount];
	SampleVoice voices[MixerVoiceCount];

	Command command_ring[CommandRingSize];
	volatile uint32_t command_head = 0;
	volatile uint32_t command_tail = 0;

	StreamVoice stream_voices[GameAudioPack::StreamSlots];
	int16_t stream_block[GameAudioPack::StreamSlots][MaxMixFrames];

	unsigned int master_volume = 100;
	// Vanilla's defaults: everything at full except music, which sits at 60.
	// The engine used to open at 80/45/70, which left the dock output about 3 dB
	// under the rail for most material -- on a 1-bit output that is a quieter
	// sound, not just a smaller number.
	unsigned int category_volumes[CategoryCount] = {
		100, // UI
		60, // Music
		100, // Ambience
		100, // Weather
		100, // Blocks
		100, // Footsteps
		100, // Mobs
		100, // Player
		100 // Combat
	};

	/**
	 * Vanilla's `music.menu`: the four `music/menu*.ogg` files, named rather than
	 * positional so the split survives a rebuild of the pack. Everything else the
	 * pack flags as music is the world's soundtrack.
	 */
	const uint16_t menu_music_table[] = {
		Sound::MusicMenu1, Sound::MusicMenu2, Sound::MusicMenu3, Sound::MusicMenu4
	};

	bool isMenuMusic(unsigned int id)
	{
		for(unsigned int i = 0; i < sizeof(menu_music_table) / sizeof(menu_music_table[0]); ++i)
			if(menu_music_table[i] == id)
				return true;
		return false;
	}

	uint32_t game_music_ids[16];
	unsigned int game_music_count = 0;
	uint32_t menu_music_ids[16];
	unsigned int menu_music_count = 0;
	unsigned int music_index = 0;
	MusicScene music_scene = MusicSceneGame;

	/** Picks up the pack's music tracks, split into the menu pool and the soundtrack. */
	void collectMusicIds()
	{
		game_music_count = 0;
		menu_music_count = 0;
		music_index = 0;
		for(uint32_t id = 1; id <= GameAudioPack::soundCount(); ++id)
		{
			const GameAudioPack::Entry *e = GameAudioPack::entry(id);
			if(e == nullptr || (e->flags & GameAudioPack::FlagMusic) == 0)
				continue;
			if(isMenuMusic(id))
			{
				if(menu_music_count < sizeof(menu_music_ids) / sizeof(menu_music_ids[0]))
					menu_music_ids[menu_music_count++] = id;
			}
			else if(game_music_count < sizeof(game_music_ids) / sizeof(game_music_ids[0]))
			{
				game_music_ids[game_music_count++] = id;
			}
		}
	}

	/**
	 * The pool the current scene draws on. A pack with no menu music (a test
	 * fixture, say) falls back to the soundtrack so a screen always has
	 * something to play rather than going silent.
	 */
	const uint32_t *musicPool(unsigned int &count)
	{
		if(music_scene == MusicSceneMenu && menu_music_count != 0)
		{
			count = menu_music_count;
			return menu_music_ids;
		}
		if(game_music_count != 0)
		{
			count = game_music_count;
			return game_music_ids;
		}
		count = menu_music_count;
		return menu_music_ids;
	}

	uint32_t rng_state = 0x12345678u;

	uint32_t nextRandom()
	{
		rng_state = rng_state * 1664525u + 1013904223u;
		return rng_state >> 16;
	}

	bool validCategory(Category category)
	{
		return static_cast<unsigned int>(category) < CategoryCount;
	}

	unsigned int categoryOf(unsigned int id, Category fallback)
	{
		const GameAudioPack::Entry *e = GameAudioPack::entry(id);
		if(e == nullptr || e->category >= CategoryCount)
			return static_cast<unsigned int>(fallback);
		return e->category;
	}

	/** (master% * category%) as 16.16. */
	uint32_t combinedGain(unsigned int master, unsigned int category)
	{
		const uint32_t combined = master * category;
		return static_cast<uint32_t>((static_cast<uint64_t>(combined) << 16) / 10000u);
	}

	/**
	 * The output stage's last stage before the 1-bit modulator: a soft knee
	 * instead of a hard clamp. Eight voices can each arrive peak-normalised, so a
	 * busy moment (a step under a sword hit under music) easily sums past full
	 * scale, and a hard clamp turns that into the square-wave crackle that reads
	 * as "broken" rather than "loud". Above 3/4 scale the curve bends instead,
	 * monotone and asymptotic to full scale, so nothing ever wraps and only the
	 * material that would have clipped is touched at all.
	 */
	constexpr int32_t LimiterKnee = 24576;  // 3/4 of full scale

	int32_t softClip(int32_t sample)
	{
		const int32_t headroom = 32767 - LimiterKnee;
		if(sample > LimiterKnee)
		{
			const int32_t over = sample - LimiterKnee;
			return LimiterKnee + static_cast<int32_t>((static_cast<int64_t>(over) * headroom) / (over + headroom));
		}
		if(sample < -LimiterKnee)
		{
			const int32_t over = -sample - LimiterKnee;
			return -(LimiterKnee + static_cast<int32_t>((static_cast<int64_t>(over) * headroom) / (over + headroom)));
		}
		return sample;
	}

	// ------------------------------------------------------------------ tones
	void clearTone(ToneVoice &voice)
	{
		voice.active = false;
		voice.phase = 0;
		voice.samples_left = 0;
		voice.release_samples = 0;
	}

	void startTone(const Tone &tone)
	{
		if(tone.frequency == 0 || tone.duration_ms == 0 || !validCategory(tone.category))
			return;

		ToneVoice *voice = nullptr;
		for(unsigned int i = 0; i < MixerVoiceCount; ++i)
		{
			if(!tone_voices[i].active)
			{
				voice = &tone_voices[i];
				break;
			}
		}
		if(voice == nullptr)
		{
			voice = &tone_voices[0];
			for(unsigned int i = 1; i < MixerVoiceCount; ++i)
				if(tone_voices[i].samples_left < voice->samples_left)
					voice = &tone_voices[i];
		}

		voice->phase = 0;
		voice->phase_step = static_cast<uint32_t>((static_cast<uint64_t>(tone.frequency) << 32) / MixerSampleRate);
		voice->samples_left = (MixerSampleRate * tone.duration_ms) / 1000u;
		voice->release_samples = voice->samples_left / 5u;
		if(voice->release_samples == 0)
			voice->release_samples = 1;
		voice->volume = tone.volume;
		voice->category = static_cast<uint8_t>(tone.category);
		voice->active = voice->samples_left != 0;
	}

	// -------------------------------------------------------------- commands
	bool pushCommand(const Command &command)
	{
		const uint32_t head = command_head;
		if(head - command_tail >= CommandRingSize)
			return false;
		command_ring[head & (CommandRingSize - 1)] = command;
		command_head = head + 1;
		return true;
	}

	Command makeCommand(uint32_t type)
	{
		Command command;
		command.type = type;
		command.slot = 0;
		command.volume = 0;
		command.category = 0;
		command.loop = 0;
		return command;
	}

	void clearVoice(SampleVoice &voice)
	{
		if(voice.active && voice.slot < GameAudioPack::CacheSlots)
			GameAudioPack::release(voice.slot);
		voice.active = false;
		voice.frame = 0;
		voice.length = 0;
	}

	void startSampleVoice(uint32_t slot, uint32_t volume, uint8_t category, uint8_t loop)
	{
		if(slot >= GameAudioPack::CacheSlots)
			return;
		const uint32_t length = GameAudioPack::slotFrames(slot);
		if(length == 0)
		{
			GameAudioPack::release(slot);
			return;
		}

		SampleVoice *voice = nullptr;
		for(unsigned int i = 0; i < MixerVoiceCount; ++i)
		{
			if(!voices[i].active)
			{
				voice = &voices[i];
				break;
			}
		}
		if(voice == nullptr)
		{
			// Steal the voice with the least left to play.
			voice = &voices[0];
			for(unsigned int i = 1; i < MixerVoiceCount; ++i)
			{
				const uint32_t left = voices[i].length - voices[i].frame;
				if(left < voice->length - voice->frame)
					voice = &voices[i];
			}
			clearVoice(*voice);
		}

		const uint32_t rate = GameAudioPack::sampleRate();
		voice->slot = static_cast<uint16_t>(slot);
		voice->frame = 0;
		voice->frac = 0;
		voice->length = length;
		voice->step = rate == MixerSampleRate
			? 65536u
			: static_cast<uint32_t>((static_cast<uint64_t>(rate) << 16) / MixerSampleRate);
		voice->release = length / 8u;
		if(voice->release == 0)
			voice->release = 1;
		voice->volume = static_cast<uint8_t>(volume > 255 ? 255 : volume);
		voice->category = category;
		voice->loop = loop;
		voice->active = true;
	}

	void applyCommand(const Command &command)
	{
		switch(command.type)
		{
		case CommandPlaySample:
			startSampleVoice(command.slot, command.volume, command.category, command.loop);
			break;

		case CommandPlayTone:
			if(command.slot < EventCount)
				startTone(event_tones[command.slot]);
			break;

		case CommandStopCategory:
			for(unsigned int i = 0; i < MixerVoiceCount; ++i)
				if(voices[i].active && voices[i].category == command.category)
					clearVoice(voices[i]);
			break;

		case CommandStopAll:
			for(unsigned int i = 0; i < MixerVoiceCount; ++i)
			{
				clearVoice(voices[i]);
				clearTone(tone_voices[i]);
			}
			break;

		default:
			break;
		}
	}

	void drainCommands()
	{
		while(command_tail != command_head)
		{
			applyCommand(command_ring[command_tail & (CommandRingSize - 1)]);
			++command_tail;
		}
	}

	// -------------------------------------------------------------- streaming
	bool startStream(uint32_t slot, unsigned int id, unsigned int category, uint32_t fade_frames)
	{
		if(slot >= GameAudioPack::StreamSlots)
			return false;

		if(id == 0 || GameAudioPack::entry(id) == nullptr)
		{
			GameAudioPack::streamClose(slot);
			stream_voices[slot].sound = 0;
			++stream_voices[slot].generation;
			return false;
		}

		if(!GameAudioPack::streamOpen(slot, id))
			return false;

		stream_voices[slot].category = category;
		stream_voices[slot].fade = fade_frames;
		stream_voices[slot].fade_total = fade_frames;
		stream_voices[slot].sound = id;
		++stream_voices[slot].generation;
		return true;
	}

	void playTone(Event event)
	{
		Command command = makeCommand(CommandPlayTone);
		command.slot = static_cast<uint32_t>(event);
		pushCommand(command);
	}
}

// -------------------------------------------------------------------- public
void initialize()
{
	for(unsigned int i = 0; i < MixerVoiceCount; ++i)
	{
		clearTone(tone_voices[i]);
		memset(&voices[i], 0, sizeof(voices[i]));
		voices[i].slot = GameAudioPack::InvalidSlot;
		voices[i].active = false;
	}

	for(unsigned int i = 0; i < GameAudioPack::StreamSlots; ++i)
	{
		stream_voices[i].sound = 0;
		stream_voices[i].category = static_cast<uint32_t>(CategoryMusic);
		stream_voices[i].generation = 0;
		stream_voices[i].seen_generation = 0;
		stream_voices[i].fade = 0;
		stream_voices[i].fade_total = 0;
		stream_voices[i].active = false;
	}

	command_head = 0;
	command_tail = 0;
	game_music_count = 0;
	menu_music_count = 0;
	music_index = 0;
	music_scene = MusicSceneGame;
	rng_state = 0x12345678u;

	GameAudioPack::open();
	collectMusicIds();
}

void shutdown()
{
	for(unsigned int i = 0; i < GameAudioPack::StreamSlots; ++i)
	{
		GameAudioPack::streamClose(i);
		stream_voices[i].sound = 0;
	}
	GameAudioPack::close();
}

bool packAvailable() { return GameAudioPack::isOpen(); }

const char *packStatus()
{
	if(GameAudioPack::isOpen())
		return GameAudioPack::path();
	return GameAudioPack::lastError() != nullptr ? GameAudioPack::lastError() : "audio pack unavailable";
}

unsigned int distanceVolume(int distance)
{
	if(distance <= 0)
		return 100;
	if(distance >= 32)
		return 0;
	return static_cast<unsigned int>(1600 - distance * 50) / 16;
}

// ------------------------------------------------------------------ one shot
bool playSound(unsigned int id)
{
	return playSoundAt(id, 0);
}

bool playSoundAt(unsigned int id, int distance)
{
	if(id == 0)
		return false;

	const int volume = (static_cast<int>(distanceVolume(distance)) * 255) / 100;
	if(volume <= 0)
		return false;

	if(!GameAudioPack::isOpen())
		return false;

	const uint32_t slot = GameAudioPack::acquire(id);
	if(slot == GameAudioPack::InvalidSlot)
	{
		// Too long to keep resident: play it as a streamed one shot instead.
		return startStream(GameAudioPack::StreamSlots - 1, id, categoryOf(id, CategoryUI), 0);
	}

	Command command = makeCommand(CommandPlaySample);
	command.slot = slot;
	command.volume = static_cast<uint32_t>(volume);
	command.category = static_cast<uint8_t>(categoryOf(id, CategoryUI));
	command.loop = 0;

	if(!pushCommand(command))
	{
		GameAudioPack::release(slot);
		return false;
	}
	return true;
}

void footstep(Material material, int distance)
{
	if(static_cast<unsigned int>(material) >= MaterialCount)
		return;

	if(!GameAudioPack::isOpen())
	{
		playTone(EventLand);
		return;
	}

	const uint16_t *family = step_table[material];
	playSoundAt(family[nextRandom() % 3], distance);
}

void digBlock(Material material)
{
	if(static_cast<unsigned int>(material) >= MaterialCount)
		return;

	if(!GameAudioPack::isOpen())
	{
		playTone(EventBlockBreak);
		return;
	}

	const uint16_t *family = dig_table[material];
	playSoundAt(family[nextRandom() % 3], 0);
}

void placeBlock(Material material)
{
	if(static_cast<unsigned int>(material) >= MaterialCount)
		return;

	if(!GameAudioPack::isOpen())
	{
		playTone(EventBlockPlace);
		return;
	}

	// Vanilla reuses the dig samples for placing.
	const uint16_t *family = dig_table[material];
	playSoundAt(family[nextRandom() % 3], 0);
}

void mobSound(MobKind kind, bool hurt, int distance)
{
	if(static_cast<unsigned int>(kind) >= MobCount)
		return;

	if(!GameAudioPack::isOpen())
	{
		playTone(EventMobHit);
		return;
	}

	const unsigned int id = hurt ? mob_hurt_table[kind] : mob_say_table[kind][nextRandom() % 3];
	playSoundAt(id, distance);
}

bool playUiSound(unsigned int id)
{
	if(!GameAudioPack::isOpen())
	{
		playTone(EventMenuSelect);
		return false;
	}
	return playSound(id);
}

// -------------------------------------------------------------- one-shots
// The official samples behind each named cue. The arrays are the three takes
// vanilla ships for the sounds that have them; one is picked per play so a run
// of steps or hits is not the same file over and over.
void uiClick()
{
	if(!GameAudioPack::isOpen()) { play(EventMenuSelect); return; }
	playSound(Sound::RandomClick);
}

void playerHurt()
{
	if(!GameAudioPack::isOpen()) { play(EventPlayerDamage); return; }
	static const uint16_t hits[3] = { Sound::DamageHit1, Sound::DamageHit2, Sound::DamageHit3 };
	playSound(hits[nextRandom() % 3]);
}

void playerAttack(bool strong)
{
	if(!GameAudioPack::isOpen()) { play(EventMobHit); return; }
	static const uint16_t weak[3] = {
		Sound::EntityPlayerAttackWeak1, Sound::EntityPlayerAttackWeak2, Sound::EntityPlayerAttackWeak3
	};
	static const uint16_t heavy[3] = {
		Sound::EntityPlayerAttackStrong1, Sound::EntityPlayerAttackStrong2, Sound::EntityPlayerAttackStrong3
	};
	const uint16_t *family = strong ? heavy : weak;
	playSound(family[nextRandom() % 3]);
}

void playerFall(bool big)
{
	if(!GameAudioPack::isOpen()) { play(EventLand); return; }
	playSound(big ? Sound::DamageFallbig : Sound::DamageFallsmall);
}

void playerEat()
{
	if(!GameAudioPack::isOpen()) { play(EventMenuSelect); return; }
	static const uint16_t eats[3] = { Sound::RandomEat1, Sound::RandomEat2, Sound::RandomEat3 };
	playSound(eats[nextRandom() % 3]);
}

void playerLevelUp()
{
	if(!GameAudioPack::isOpen()) { play(EventMenuSelect); return; }
	playSound(Sound::RandomLevelup);
}

void itemPickup()
{
	if(!GameAudioPack::isOpen()) { play(EventMenuSelect); return; }
	playSound(Sound::RandomPop);
}

void chestOpen()
{
	if(!GameAudioPack::isOpen()) { play(EventMenuSelect); return; }
	playSound(Sound::RandomChestopen);
}

void chestClose()
{
	if(!GameAudioPack::isOpen()) { play(EventMenuSelect); return; }
	playSound(Sound::RandomChestclosed);
}

void doorOpen()
{
	if(!GameAudioPack::isOpen()) { play(EventMenuSelect); return; }
	playSound(Sound::RandomDoorOpen);
}

void doorClose()
{
	if(!GameAudioPack::isOpen()) { play(EventMenuSelect); return; }
	playSound(Sound::RandomDoorClose);
}

void fuse(int distance)
{
	if(!GameAudioPack::isOpen()) { play(EventBlockPlace); return; }
	playSoundAt(Sound::RandomFuse, distance);
}

void explosion(int distance)
{
	if(!GameAudioPack::isOpen()) { play(EventBlockBreak); return; }
	static const uint16_t blasts[3] = {
		Sound::RandomExplode1, Sound::RandomExplode2, Sound::RandomExplode3
	};
	playSoundAt(blasts[nextRandom() % 3], distance);
}

void play(Event event)
{
	if(static_cast<unsigned int>(event) >= EventCount)
		return;
	playTone(event);
}

// -------------------------------------------------------------------- music
unsigned int musicTrackCount()
{
	unsigned int count = 0;
	musicPool(count);
	return count;
}
void rescanMusic() { collectMusicIds(); }

namespace
{
	/**
	 * Vanilla's `MusicManager` in three lines of state: whether a screen wants
	 * background music, whether a track is live, and the random quiet spell
	 * vanilla leaves between two tracks -- shortened here so a device session
	 * hears more than one of them.
	 */
	bool music_desired = false;
	bool music_track_live = false;
	unsigned int music_gap_ms = 0;

	const unsigned int MusicGapMsMin = 5000, MusicGapMsMax = 20000;

	unsigned int musicGap()
	{
		return MusicGapMsMin + nextRandom() % (MusicGapMsMax - MusicGapMsMin + 1);
	}
}

void setMusicDesired(bool desired, MusicScene scene)
{
	// Vanilla's MusicManager swaps pools rather than fading: when the screen's
	// music type changes while a track is live, the track stops and the next one
	// comes from the new pool after the usual quiet spell. Nothing to do here on
	// an ordinary pause, which keeps the world's own scene.
	const bool scene_changed = desired && scene != music_scene;
	music_scene = scene;
	if(scene_changed && musicPlaying())
	{
		stopMusic();
		music_gap_ms = musicGap();
	}

	// A screen that starts wanting music gets it at once; only the transition
	// arms it, so pausing and unpausing a world changes nothing.
	if(desired && !music_desired && !musicPlaying())
		music_gap_ms = 0;
	music_desired = desired;
}

void updateMusic(unsigned int elapsed_ms)
{
	if(!music_desired)
		return;

	if(musicPlaying())
	{
		music_track_live = true;
		return;
	}

	// The track that was playing has ended: leave vanilla's quiet spell behind
	// it before the next one starts.
	if(music_track_live)
	{
		music_track_live = false;
		music_gap_ms = musicGap();
		return;
	}

	if(music_gap_ms > 0)
	{
		if(music_gap_ms > elapsed_ms)
		{
			music_gap_ms -= elapsed_ms;
			return;
		}
		music_gap_ms = 0;
	}

	if(startMusic())
		music_track_live = true;
	// No pack, or no music in it: try again next frame.
}

bool startMusic()
{
	// A pack that was opened after initialize() -- which is what the host tests
	// do -- is scanned for music the first time it is wanted.
	if(game_music_count == 0 && menu_music_count == 0)
		collectMusicIds();

	if(!GameAudioPack::isOpen())
		return false;

	unsigned int count = 0;
	const uint32_t *pool = musicPool(count);
	if(count == 0)
		return false;

	music_index %= count;
	const unsigned int id = pool[music_index];
	music_index = (music_index + 1) % count;

	return startStream(0, id, CategoryMusic, MixerSampleRate / 4);
}

void stopMusic()
{
	GameAudioPack::streamClose(0);
	stream_voices[0].sound = 0;
	++stream_voices[0].generation;

	// A deliberate stop is not "the track ended", so no quiet spell is owed.
	music_track_live = false;
	music_gap_ms = 0;
}

bool musicPlaying() { return stream_voices[0].sound != 0 && GameAudioPack::streamIsOpen(0); }

unsigned int currentMusicTrack() { return stream_voices[0].sound; }

// ---------------------------------------------------------- ambience/weather
void setAmbience(unsigned int id)
{
	if(!GameAudioPack::isOpen())
		id = 0;

	if(id == 0)
	{
		GameAudioPack::streamClose(1);
		stream_voices[1].sound = 0;
		++stream_voices[1].generation;
		return;
	}

	startStream(1, id, CategoryAmbience, MixerSampleRate / 4);
}

unsigned int ambience() { return stream_voices[1].sound; }

void setWeather(unsigned int id, bool active)
{
	if(!active || id == 0 || !GameAudioPack::isOpen())
	{
		GameAudioPack::streamClose(2);
		stream_voices[2].sound = 0;
		++stream_voices[2].generation;
		return;
	}

	startStream(2, id, CategoryWeather, MixerSampleRate / 4);
}

bool weatherActive() { return stream_voices[2].sound != 0; }

void weatherThunder(int distance)
{
	if(!GameAudioPack::isOpen())
		return;
	const unsigned int count = sizeof(thunder_cues) / sizeof(thunder_cues[0]);
	playSoundAt(thunder_cues[nextRandom() % count], distance);
}

void ambienceCue(int distance)
{
	if(!GameAudioPack::isOpen())
		return;
	const unsigned int count = sizeof(ambience_cues) / sizeof(ambience_cues[0]);
	playSoundAt(ambience_cues[nextRandom() % count], distance);
}

// ----------------------------------------------------------------- volumes
void setMasterVolume(unsigned int percent)
{
	master_volume = percent > 100 ? 100 : percent;
}

void setCategoryVolume(Category category, unsigned int percent)
{
	if(!validCategory(category))
		return;
	category_volumes[category] = percent > 100 ? 100 : percent;
}

unsigned int masterVolume() { return master_volume; }

unsigned int categoryVolume(Category category)
{
	if(!validCategory(category))
		return 0;
	return category_volumes[category];
}

void stopAll()
{
	const Command command = makeCommand(CommandStopAll);
	pushCommand(command);
}

// -------------------------------------------------------------------- mixer
size_t mixMono(int16_t *output, size_t frames)
{
	if(output == nullptr)
		return 0;
	if(frames > MaxMixFrames)
		frames = MaxMixFrames;
	if(frames == 0)
		return 0;

	drainCommands();

	uint32_t category_gain[CategoryCount];
	for(unsigned int i = 0; i < CategoryCount; ++i)
		category_gain[i] = combinedGain(master_volume, category_volumes[i]);

	// Drain one block per active stream voice. File IO happens in pump() on the
	// game loop, never here, so this stays safe to run from an interrupt.
	uint32_t stream_gain[GameAudioPack::StreamSlots];
	for(unsigned int slot = 0; slot < GameAudioPack::StreamSlots; ++slot)
	{
		StreamVoice &stream = stream_voices[slot];
		if(stream.generation != stream.seen_generation)
		{
			stream.seen_generation = stream.generation;
			stream.active = stream.sound != 0;
		}

		if(!stream.active)
		{
			stream_gain[slot] = 0;
			continue;
		}

		// A zero master or category volume must really mean silence here, so no
		// floor is applied: gain 0 disables the stream for this block.
		const uint32_t category = stream.category < CategoryCount ? stream.category : static_cast<uint32_t>(CategoryUI);
		uint32_t gain = category_gain[category];

		// The fade ramp is applied per frame further down, so this is the target
		// gain and not the current one.
		stream_gain[slot] = gain;
		GameAudioPack::streamRead(slot, stream_block[slot], static_cast<uint32_t>(frames));
	}

	for(size_t frame = 0; frame < frames; ++frame)
	{
		int32_t mixed = 0;

		for(unsigned int i = 0; i < MixerVoiceCount; ++i)
		{
			SampleVoice &voice = voices[i];
			if(!voice.active)
				continue;

			const uint8_t *data = GameAudioPack::slotData(voice.slot);
			if(data == nullptr)
			{
				clearVoice(voice);
				continue;
			}

			uint32_t index = voice.frame;
			if(index >= voice.length)
			{
				if(voice.loop)
				{
					voice.frame = 0;
					index = 0;
				}
				else
				{
					clearVoice(voice);
					continue;
				}
			}

			const uint32_t category = voice.category < CategoryCount ? voice.category : static_cast<uint32_t>(CategoryUI);
			const uint32_t gain16 = static_cast<uint32_t>(
				((static_cast<uint64_t>(voice.volume) * category_gain[category]) << 16) / (255u * 100u * 100u));

			if(gain16 != 0)
			{
				// Linear interpolation between the two source frames the voice sits
				// between. A pack built at the mixer's own rate steps exactly and the
				// fraction is always zero, so this costs nothing there; a pack at any
				// other rate is resampled without the harshness of dropping samples.
				uint32_t next = index + 1;
				if(next >= voice.length)
					next = voice.loop ? 0 : index;
				const int32_t a = static_cast<int32_t>((static_cast<int>(data[index]) - 128) << 8);
				const int32_t b = static_cast<int32_t>((static_cast<int>(data[next]) - 128) << 8);
				const int32_t sample_in = a
					+ static_cast<int32_t>((static_cast<int64_t>(b - a) * (voice.frac >> 8)) >> 8);

				int32_t sample = sample_in;
				const uint32_t left = voice.length - index;
				if(left < voice.release)
					sample = (sample * static_cast<int32_t>(left)) / static_cast<int32_t>(voice.release);

				mixed += (sample * static_cast<int32_t>(gain16 >> 8)) >> 8;
			}

			voice.frac += voice.step;
			voice.frame = index + (voice.frac >> 16);
			voice.frac &= 0xFFFFu;
		}

		for(unsigned int i = 0; i < MixerVoiceCount; ++i)
		{
			ToneVoice &voice = tone_voices[i];
			if(!voice.active)
				continue;

			const uint32_t combined = (master_volume * category_volumes[voice.category < CategoryCount ? voice.category : 0]);
			const int32_t polarity = (voice.phase & 0x80000000u) ? -1 : 1;
			uint32_t envelope = 32767u;
			if(voice.samples_left <= voice.release_samples)
				envelope = (voice.samples_left * 32767u) / voice.release_samples;

			const uint64_t scaled = static_cast<uint64_t>(envelope) * voice.volume * combined;
			mixed += polarity * static_cast<int32_t>(scaled / (100u * 100u * 100u));

			voice.phase += voice.phase_step;
			if(--voice.samples_left == 0)
				clearTone(voice);
		}

		for(unsigned int slot = 0; slot < GameAudioPack::StreamSlots; ++slot)
		{
			if(stream_gain[slot] == 0)
				continue;

			uint32_t gain = stream_gain[slot];
			StreamVoice &stream = stream_voices[slot];
			if(stream.fade_total != 0)
			{
				// Fade in over the first frames of a music or ambience bed so a
				// newly started stream does not click.
				gain = static_cast<uint32_t>((static_cast<uint64_t>(gain) * (stream.fade_total - stream.fade)) / stream.fade_total);
				if(stream.fade > 0 && --stream.fade == 0)
					stream.fade_total = 0;
			}

			const int32_t sample = stream_block[slot][frame];
			mixed += (sample * static_cast<int32_t>(gain >> 8)) >> 8;
		}

		output[frame] = static_cast<int16_t>(softClip(mixed));
	}

	// --- limiter make-up gain ----------------------------------------------
	// The knee above costs level, and on a one-bit output level *is* loudness:
	// what the knee takes is gone for good, because the pin's swing is already
	// the whole of it. So whatever the knee removed is given back here -- the
	// block is scaled until its loudest sample reaches the ceiling, which is as
	// loud as a digital output can be without clipping.
	//
	// Three things keep that safe rather than merely loud:
	//   * a block that never entered the knee is not lifted at all, and any gain
	//     left over from a louder block decays back to unity across the next one,
	//     so a quiet passage is never left permanently turned up,
	//   * the gain is capped at MakeupCap (+3 dB), so the block-to-block level
	//     cannot pump by more than that however hard the mix is,
	//   * the gain is ramped across the block from the previous block's value, so
	//     the change is a slope and not a step (a step would be a click), and the
	//     result is clamped rather than wrapped: a ramp that briefly overshoots
	//     on a block whose target gain dropped must never invert a sample.
	{
		int32_t peak = 0;
		for(size_t frame = 0; frame < frames; ++frame)
		{
			const int32_t value = output[frame] < 0 ? -static_cast<int32_t>(output[frame])
				: static_cast<int32_t>(output[frame]);
			if(value > peak)
				peak = value;
		}

		double target = 1.0;
		if(peak > LimiterKnee)
		{
			target = static_cast<double>(32767) / static_cast<double>(peak);
			if(target > MakeupCap)
				target = MakeupCap;
		}

		const double from = makeup_gain;
		for(size_t frame = 0; frame < frames; ++frame)
		{
			const double gain = from + (target - from) * (static_cast<double>(frame + 1)
				/ static_cast<double>(frames));
			int32_t value = static_cast<int32_t>(static_cast<double>(output[frame]) * gain);
			if(value > 32767)
				value = 32767;
			else if(value < -32768)
				value = -32768;
			output[frame] = static_cast<int16_t>(value);
		}
		makeup_gain = target;
	}

	return frames;
}

void pump()
{
	for(unsigned int slot = 0; slot < GameAudioPack::StreamSlots; ++slot)
		GameAudioPack::streamPump(slot);

	GameAudioOutput::pump();
}

bool outputAvailable()
{
	return GameAudioOutput::available();
}

unsigned int activeVoiceCount()
{
	unsigned int count = 0;
	for(unsigned int i = 0; i < MixerVoiceCount; ++i)
	{
		if(voices[i].active)
			++count;
		if(tone_voices[i].active)
			++count;
	}
	for(unsigned int i = 0; i < GameAudioPack::StreamSlots; ++i)
		if(stream_voices[i].active)
			++count;
	return count;
}

unsigned int streamUnderruns()
{
	unsigned int total = 0;
	for(unsigned int i = 0; i < GameAudioPack::StreamSlots; ++i)
		total += GameAudioPack::streamUnderruns(i);
	return total;
}
}
