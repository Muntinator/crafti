// Host tests for the audio pack reader: header parsing, the reference counted
// RAM cache with LRU eviction, and the streaming rings.
//
// Build and run with `make -C tests`.

#include "audio_pack.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

namespace
{
	const char *pack_path = "build/audio_pack_fixture.audp";
	const uint32_t sample_rate = 8000;
	const int sound_total = 10;

	// Entry 1..9 are cache sized, entry 10 is far too long to keep resident.
	uint32_t length_of(int sound)
	{
		if(sound == 10)
			return GameAudioPack::CacheSlotBytes + 64;
		return static_cast<uint32_t>(200 + sound * 40);
	}

	void writeEntry(FILE *file, uint32_t offset, uint32_t length, uint16_t category, uint16_t flags)
	{
		uint8_t raw[16];
		raw[0] = static_cast<uint8_t>(offset);
		raw[1] = static_cast<uint8_t>(offset >> 8);
		raw[2] = static_cast<uint8_t>(offset >> 16);
		raw[3] = static_cast<uint8_t>(offset >> 24);
		raw[4] = static_cast<uint8_t>(length);
		raw[5] = static_cast<uint8_t>(length >> 8);
		raw[6] = static_cast<uint8_t>(length >> 16);
		raw[7] = static_cast<uint8_t>(length >> 24);
		raw[8] = static_cast<uint8_t>(category);
		raw[9] = static_cast<uint8_t>(category >> 8);
		raw[10] = static_cast<uint8_t>(flags);
		raw[11] = static_cast<uint8_t>(flags >> 8);
		raw[12] = 255; // gain
		raw[13] = 0;
		raw[14] = 0;
		raw[15] = 0;
		fwrite(raw, 1, sizeof(raw), file);
	}

	bool buildFixture()
	{
		const uint32_t index_offset = 24;
		const uint32_t data_offset = index_offset + 16 * sound_total;

		uint32_t data_size = 0;
		for(int sound = 1; sound <= sound_total; ++sound)
			data_size += length_of(sound);

		FILE *file = fopen(pack_path, "wb");
		if(file == nullptr)
			return false;

		uint8_t header[24];
		memcpy(header, "AUD1", 4);
		header[4] = 1; header[5] = 0;
		header[6] = static_cast<uint8_t>(sample_rate); header[7] = static_cast<uint8_t>(sample_rate >> 8);
		header[8] = 0; header[9] = 0;
		header[10] = static_cast<uint8_t>(sound_total); header[11] = 0;
		const uint32_t words[3] = {index_offset, data_offset, data_size};
		for(int word = 0; word < 3; ++word)
		{
			header[12 + word * 4] = static_cast<uint8_t>(words[word]);
			header[13 + word * 4] = static_cast<uint8_t>(words[word] >> 8);
			header[14 + word * 4] = static_cast<uint8_t>(words[word] >> 16);
			header[15 + word * 4] = static_cast<uint8_t>(words[word] >> 24);
		}
		fwrite(header, 1, sizeof(header), file);

		uint32_t offset = 0;
		for(int sound = 1; sound <= sound_total; ++sound)
		{
			// The last entry is flagged as music so streaming can be checked.
			const uint16_t flags = sound == 10 ? GameAudioPack::FlagStream : 0;
			writeEntry(file, offset, length_of(sound), static_cast<uint16_t>(sound % 9), flags);
			offset += length_of(sound);
		}

		for(int sound = 1; sound <= sound_total; ++sound)
		{
			const uint32_t length = length_of(sound);
			for(uint32_t i = 0; i < length; ++i)
			{
				const uint8_t sample = static_cast<uint8_t>(128 + ((i + sound) % 100) - 50);
				fputc(sample, file);
			}
		}

		fclose(file);
		return true;
	}
}

static void testOpenAndMetadata()
{
	CHECK(GameAudioPack::open(pack_path));
	CHECK(GameAudioPack::isOpen());
	CHECK(GameAudioPack::sampleRate() == sample_rate);
	CHECK(GameAudioPack::soundCount() == static_cast<uint32_t>(sound_total));

	const GameAudioPack::Entry *entry = GameAudioPack::entry(1);
	CHECK(entry != nullptr);
	if(entry != nullptr)
	{
		CHECK(entry->length == length_of(1));
		CHECK(entry->gain == 255);
	}
	CHECK(GameAudioPack::entry(0) == nullptr);
	CHECK(GameAudioPack::entry(sound_total + 1) == nullptr);
}

static void testCacheAcquireRelease()
{
	const uint32_t slot = GameAudioPack::acquire(1);
	CHECK(slot != GameAudioPack::InvalidSlot);
	if(slot == GameAudioPack::InvalidSlot)
		return;

	CHECK(GameAudioPack::slotFrames(slot) == length_of(1));
	CHECK(GameAudioPack::slotRefCount(slot) == 1);
	CHECK(GameAudioPack::slotData(slot) != nullptr);

	// A second acquire of the same sound must hit the cache, not the file.
	const uint32_t again = GameAudioPack::acquire(1);
	CHECK(again == slot);
	CHECK(GameAudioPack::slotRefCount(slot) == 2);
	CHECK(GameAudioPack::cacheHits() == 1);
	CHECK(GameAudioPack::cacheMisses() == 1);

	// The cached bytes must be the ones from the pack.
	const uint8_t *data = GameAudioPack::slotData(slot);
	CHECK(data != nullptr && data[0] == static_cast<uint8_t>(128 + ((0 + 1) % 100) - 50));

	GameAudioPack::release(slot);
	GameAudioPack::release(slot);
	CHECK(GameAudioPack::slotRefCount(slot) == 0);
}

static void testUncacheableSound()
{
	// Too long for a cache slot: the engine streams it instead.
	CHECK(GameAudioPack::acquire(sound_total) == GameAudioPack::InvalidSlot);
}

static void testLruEviction()
{
	GameAudioPack::close();
	CHECK(GameAudioPack::open(pack_path));

	// Hold the first slot, then occupy every other slot.
	const uint32_t held = GameAudioPack::acquire(1);
	CHECK(held != GameAudioPack::InvalidSlot);
	for(int sound = 2; sound <= static_cast<int>(GameAudioPack::CacheSlots); ++sound)
		CHECK(GameAudioPack::acquire(sound) != GameAudioPack::InvalidSlot);

	// Every slot is referenced, so nothing may be evicted.
	CHECK(GameAudioPack::acquire(GameAudioPack::CacheSlots + 1) == GameAudioPack::InvalidSlot);

	// Free the others and the next acquire reclaims one of them.
	for(unsigned int i = 0; i < GameAudioPack::CacheSlots; ++i)
		if(i != held)
			GameAudioPack::release(i);

	const uint32_t extra = GameAudioPack::acquire(GameAudioPack::CacheSlots + 1);
	CHECK(extra != GameAudioPack::InvalidSlot);
	CHECK(extra != held);

	// A referenced slot is never stolen, and its contents stay intact.
	CHECK(GameAudioPack::slotRefCount(held) == 1);
	CHECK(GameAudioPack::slotData(held)[0] == static_cast<uint8_t>(128 + ((0 + 1) % 100) - 50));
}

static void testStreaming()
{
	CHECK(GameAudioPack::streamOpen(0, 10));
	CHECK(GameAudioPack::streamIsOpen(0));
	CHECK(GameAudioPack::streamSound(0) == 10);

	// Nothing has been pumped yet, so the ring is empty.
	int16_t block[512];
	GameAudioPack::streamRead(0, block, 512);
	CHECK(GameAudioPack::streamUnderruns(0) == 1);

	// Pumping from the game loop fills the ring without any further file IO.
	for(int i = 0; i < 8; ++i)
		GameAudioPack::streamPump(0);

	const uint32_t read = GameAudioPack::streamRead(0, block, 512);
	CHECK(read == 512);

	bool nonzero = false;
	for(unsigned int i = 0; i < 512; ++i)
		if(block[i] != 0)
			nonzero = true;
	CHECK(nonzero);

	// Playing the whole sound out closes a non-looping stream. The clock has to
	// consume the ring for the pump to make progress, which is exactly how the
	// real backend behaves.
	int guard = 0;
	while(GameAudioPack::streamIsOpen(0) && guard++ < 200)
	{
		GameAudioPack::streamRead(0, block, 256);
		GameAudioPack::streamPump(0);
	}
	CHECK(!GameAudioPack::streamIsOpen(0));
	CHECK(guard < 200);

	GameAudioPack::streamClose(0);
	CHECK(!GameAudioPack::streamIsOpen(0));
}

static void testRejectsInvalidPacks()
{
	CHECK(!GameAudioPack::open("build/does_not_exist.audp"));
	CHECK(!GameAudioPack::isOpen());

	FILE *file = fopen("build/audio_pack_bad.audp", "wb");
	if(file != nullptr)
	{
		fputs("NOPE-not-a-pack-at-all", file);
		fclose(file);
	}
	CHECK(!GameAudioPack::open("build/audio_pack_bad.audp"));
	CHECK(!GameAudioPack::isOpen());
}

int main()
{
	printf("audio_pack_test\n");
	CHECK(buildFixture());

	testOpenAndMetadata();
	testCacheAcquireRelease();
	testUncacheableSound();
	testLruEviction();
	testStreaming();
	testRejectsInvalidPacks();

	GameAudioPack::close();

	printf("%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
