#ifndef AUDIO_PACK_H
#define AUDIO_PACK_H

#include <stdint.h>
#include <stddef.h>

/**
 * Reader for the on-calculator audio pack built by tools/audio/build_audio_pack.py.
 *
 * The pack starts with a fixed header, followed by a directory of fixed-size
 * entries, followed by 8-bit unsigned mono PCM at the pack's sample rate.
 *
 * Two access strategies keep the CX's RAM and CPU budgets in mind:
 *
 *  - Short, frequently repeated sounds (footsteps, digs, UI clicks) are loaded
 *    once into a small fixed pool of slots with reference counts, so a repeated
 *    footstep never touches the filesystem twice.
 *  - Long sounds (music, ambience and weather beds) are streamed: the game loop
 *    refills a small in-RAM ring and the audio clock only consumes it, so no
 *    file IO ever happens while samples are being produced.
 *
 * There is no dynamic allocation anywhere in this module.
 */
namespace GameAudioPack
{
	enum Flags
	{
		FlagLoop = 1 << 0,
		FlagMusic = 1 << 1,
		FlagStream = 1 << 2
	};

	enum
	{
		InvalidSlot = 0xFFFFu,
		InvalidSound = 0xFFFFFFFFu
	};

	struct Entry
	{
		uint32_t offset;
		uint32_t length;
		uint16_t category;
		uint16_t flags;
		uint16_t gain;
		uint16_t reserved;
	};

	/** Slot pool for cached short sounds. */
	static const unsigned int CacheSlots = 8;
	static const unsigned int CacheSlotBytes = 9216;
	/** Maximum number of sounds a pack may declare. */
	static const unsigned int MaxSounds = 2048;

	/** Ring slots for streamed (long) sounds: music plus ambience/weather beds. */
	static const unsigned int StreamSlots = 4;
	static const unsigned int StreamFrames = 2048;

	/** Opens the pack, searching the standard locations when path is null. */
	bool open(const char *path = nullptr);
	void close();
	bool isOpen();

	uint32_t sampleRate();
	uint32_t soundCount();
	const Entry *entry(uint32_t sound);
	const char *path();
	const char *lastError();

	/** Loads a short sound into the cache. Returns the slot, or InvalidSlot. */
	uint32_t acquire(uint32_t sound);
	void release(uint32_t slot);
	const uint8_t *slotData(uint32_t slot);
	uint32_t slotFrames(uint32_t slot);
	uint16_t slotGain(uint32_t slot);
	uint32_t slotRefCount(uint32_t slot);
	uint32_t cacheHits();
	uint32_t cacheMisses();

	bool streamOpen(uint32_t slot, uint32_t sound);
	void streamClose(uint32_t slot);
	bool streamIsOpen(uint32_t slot);
	uint32_t streamSound(uint32_t slot);
	/** Audio-clock side: copies already-buffered frames, never touches the file. */
	uint32_t streamRead(uint32_t slot, int16_t *out, uint32_t frames);
	/** Game-loop side: tops the ring up from the pack file. */
	void streamPump(uint32_t slot);
	uint32_t streamUnderruns(uint32_t slot);
	void streamRewind(uint32_t slot);
	bool streamLoops(uint32_t slot);
}

#endif // AUDIO_PACK_H
