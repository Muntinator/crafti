#include "audio_pack.h"

#include <stdio.h>
#include <string.h>

namespace GameAudioPack
{
namespace
{
	const char PackMagic[4] = {'A', 'U', 'D', '1'};
	const uint16_t PackVersion = 1;
	/** magic(4) version(2) rate(2) flags(2) count(2) index(4) data(4) size(4). */
	const uint32_t HeaderBytes = 24;
	const uint32_t EntryBytes = 16;

	/** Bytes pulled from the pack per streamPump call, per stream. */
	const uint32_t PumpChunk = 512;

	FILE *pack_file = nullptr;
	Entry index_[MaxSounds];
	uint32_t sound_count = 0;
	uint32_t sample_rate = 0;
	uint32_t data_offset = 0;
	uint32_t data_size = 0;
	bool open_ = false;
	char pack_path[128] = {0};
	const char *error_ = "audio pack not opened";

	uint8_t cache_data[CacheSlots][CacheSlotBytes];
	uint32_t cache_sound[CacheSlots];
	uint32_t cache_frames[CacheSlots];
	uint16_t cache_gain[CacheSlots];
	uint16_t cache_refs[CacheSlots];
	uint8_t cache_used[CacheSlots];
	uint32_t cache_stamp[CacheSlots];
	uint32_t cache_clock = 0;
	uint32_t cache_hit_count = 0;
	uint32_t cache_miss_count = 0;

	struct StreamState
	{
		bool open;
		bool loop;
		uint32_t sound;
		uint32_t file_offset; // next byte to pull inside the data section
		uint32_t remaining; // bytes left in the current pass
		uint32_t underruns;
		volatile uint32_t head; // frames produced by streamPump
		uint32_t tail; // frames consumed by the audio clock
		int16_t ring[StreamFrames];
	};

	StreamState streams[StreamSlots];
	uint8_t pump_scratch[PumpChunk];

	void resetPack()
	{
		pack_file = nullptr;
		sound_count = 0;
		sample_rate = 0;
		data_offset = 0;
		data_size = 0;
		open_ = false;
		pack_path[0] = 0;

		for(unsigned int i = 0; i < CacheSlots; ++i)
		{
			cache_sound[i] = 0;
			cache_frames[i] = 0;
			cache_gain[i] = 255;
			cache_refs[i] = 0;
			cache_used[i] = 0;
			cache_stamp[i] = 0;
		}
		cache_clock = 0;
		cache_hit_count = 0;
		cache_miss_count = 0;

		for(unsigned int i = 0; i < StreamSlots; ++i)
		{
			streams[i].open = false;
			streams[i].loop = false;
			streams[i].sound = 0;
			streams[i].file_offset = 0;
			streams[i].remaining = 0;
			streams[i].underruns = 0;
			streams[i].head = 0;
			streams[i].tail = 0;
		}
	}

	bool readAt(uint32_t offset, void *dst, uint32_t bytes)
	{
		if(!open_ || pack_file == nullptr)
			return false;
		if(fseek(pack_file, static_cast<long>(offset), SEEK_SET) != 0)
			return false;
		return fread(dst, 1, bytes, pack_file) == bytes;
	}

	bool tryOpen(const char *candidate)
	{
		FILE *file = fopen(candidate, "rb");
		if(file == nullptr)
			return false;

		uint8_t header[HeaderBytes];
		if(fread(header, 1, HeaderBytes, file) != HeaderBytes)
		{
			fclose(file);
			return false;
		}

		if(memcmp(header, PackMagic, 4) != 0)
		{
			fclose(file);
			return false;
		}

		const uint16_t version = static_cast<uint16_t>(header[4] | (header[5] << 8));
		const uint32_t rate = static_cast<uint32_t>(header[6] | (header[7] << 8));
		const uint32_t sounds = static_cast<uint32_t>(header[10] | (header[11] << 8));
		const uint32_t index_offset = static_cast<uint32_t>(header[12]) | (static_cast<uint32_t>(header[13]) << 8)
			| (static_cast<uint32_t>(header[14]) << 16) | (static_cast<uint32_t>(header[15]) << 24);
		const uint32_t data = static_cast<uint32_t>(header[16]) | (static_cast<uint32_t>(header[17]) << 8)
			| (static_cast<uint32_t>(header[18]) << 16) | (static_cast<uint32_t>(header[19]) << 24);
		const uint32_t size = static_cast<uint32_t>(header[20]) | (static_cast<uint32_t>(header[21]) << 8)
			| (static_cast<uint32_t>(header[22]) << 16) | (static_cast<uint32_t>(header[23]) << 24);

		if(version != PackVersion || rate == 0 || sounds == 0 || sounds > MaxSounds
			|| index_offset != HeaderBytes
			|| data != index_offset + sounds * EntryBytes
			|| static_cast<uint64_t>(data) + size > 0x7FFFFFFFull)
		{
			fclose(file);
			error_ = "unsupported audio pack";
			return false;
		}

		for(uint32_t i = 0; i < sounds; ++i)
		{
			uint8_t raw[EntryBytes];
			if(fseek(file, static_cast<long>(index_offset + i * EntryBytes), SEEK_SET) != 0
				|| fread(raw, 1, EntryBytes, file) != EntryBytes)
			{
				fclose(file);
				return false;
			}
			Entry &e = index_[i];
			e.offset = static_cast<uint32_t>(raw[0]) | (static_cast<uint32_t>(raw[1]) << 8)
				| (static_cast<uint32_t>(raw[2]) << 16) | (static_cast<uint32_t>(raw[3]) << 24);
			e.length = static_cast<uint32_t>(raw[4]) | (static_cast<uint32_t>(raw[5]) << 8)
				| (static_cast<uint32_t>(raw[6]) << 16) | (static_cast<uint32_t>(raw[7]) << 24);
			e.category = static_cast<uint16_t>(raw[8] | (raw[9] << 8));
			e.flags = static_cast<uint16_t>(raw[10] | (raw[11] << 8));
			e.gain = static_cast<uint16_t>(raw[12] | (raw[13] << 8));
			e.reserved = 0;

			if(e.offset + e.length > size)
			{
				fclose(file);
				error_ = "audio pack index out of range";
				return false;
			}
		}

		pack_file = file;
		open_ = true;
		sound_count = sounds;
		sample_rate = rate;
		data_offset = data;
		data_size = size;
		error_ = nullptr;

		strncpy(pack_path, candidate, sizeof(pack_path) - 1);
		pack_path[sizeof(pack_path) - 1] = 0;
		return true;
	}
}

bool open(const char *path)
{
	close();

	if(path != nullptr && tryOpen(path))
		return true;
	if(path != nullptr)
		return false;

#ifdef _TINSPIRE
	static const char *candidates[] = {
		"/documents/ndless/crafti.audp",
		"/documents/crafti.audp"
	};
#else
	static const char *candidates[] = {
		"crafti.audp",
		"../crafti.audp"
	};
#endif

	for(unsigned int i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
	{
		if(tryOpen(candidates[i]))
			return true;
	}

	if(error_ == nullptr)
		error_ = "audio pack not found";
	return false;
}

void close()
{
	if(pack_file != nullptr)
	{
		fclose(pack_file);
		pack_file = nullptr;
	}
	resetPack();
}

bool isOpen() { return open_; }
uint32_t sampleRate() { return sample_rate; }
uint32_t soundCount() { return sound_count; }
const char *path() { return pack_path; }
const char *lastError() { return error_; }

const Entry *entry(uint32_t sound)
{
	if(!open_ || sound == 0 || sound > sound_count)
		return nullptr;
	return &index_[sound - 1];
}

uint32_t acquire(uint32_t sound)
{
	const Entry *e = entry(sound);
	if(e == nullptr)
		return InvalidSlot;

	for(unsigned int i = 0; i < CacheSlots; ++i)
	{
		if(cache_used[i] && cache_sound[i] == sound)
		{
			++cache_refs[i];
			cache_stamp[i] = ++cache_clock;
			++cache_hit_count;
			return i;
		}
	}

	if(e->length > CacheSlotBytes)
		return InvalidSlot; // too long to keep resident; play it streamed instead

	unsigned int victim = CacheSlots;
	for(unsigned int i = 0; i < CacheSlots; ++i)
	{
		if(!cache_used[i])
		{
			victim = i;
			break;
		}
	}
	if(victim == CacheSlots)
	{
		uint32_t oldest = 0xFFFFFFFFu;
		for(unsigned int i = 0; i < CacheSlots; ++i)
		{
			if(cache_refs[i] == 0 && cache_stamp[i] <= oldest)
			{
				oldest = cache_stamp[i];
				victim = i;
			}
		}
	}
	if(victim == CacheSlots)
		return InvalidSlot; // every slot is in use

	if(!readAt(data_offset + e->offset, cache_data[victim], e->length))
		return InvalidSlot;

	cache_used[victim] = 1;
	cache_sound[victim] = sound;
	cache_frames[victim] = e->length;
	cache_gain[victim] = e->gain;
	cache_refs[victim] = 1;
	cache_stamp[victim] = ++cache_clock;
	++cache_miss_count;
	return victim;
}

void release(uint32_t slot)
{
	if(slot >= CacheSlots)
		return;
	if(cache_refs[slot] > 0)
		--cache_refs[slot];
}

const uint8_t *slotData(uint32_t slot)
{
	return slot < CacheSlots ? cache_data[slot] : nullptr;
}

uint32_t slotFrames(uint32_t slot)
{
	return slot < CacheSlots ? cache_frames[slot] : 0;
}

uint16_t slotGain(uint32_t slot)
{
	return slot < CacheSlots ? cache_gain[slot] : 255;
}

uint32_t slotRefCount(uint32_t slot)
{
	return slot < CacheSlots ? cache_refs[slot] : 0;
}

uint32_t cacheHits() { return cache_hit_count; }
uint32_t cacheMisses() { return cache_miss_count; }

bool streamOpen(uint32_t slot, uint32_t sound)
{
	if(slot >= StreamSlots)
		return false;
	const Entry *e = entry(sound);
	if(e == nullptr)
		return false;

	StreamState &s = streams[slot];
	s.open = true;
	s.loop = (e->flags & FlagLoop) != 0;
	s.sound = sound;
	s.file_offset = 0;
	s.remaining = e->length;
	s.underruns = 0;
	s.head = 0;
	s.tail = 0;
	return true;
}

void streamClose(uint32_t slot)
{
	if(slot >= StreamSlots)
		return;
	streams[slot].open = false;
	streams[slot].head = 0;
	streams[slot].tail = 0;
}

bool streamIsOpen(uint32_t slot) { return slot < StreamSlots && streams[slot].open; }
uint32_t streamSound(uint32_t slot) { return slot < StreamSlots ? streams[slot].sound : 0; }
uint32_t streamUnderruns(uint32_t slot) { return slot < StreamSlots ? streams[slot].underruns : 0; }
bool streamLoops(uint32_t slot) { return slot < StreamSlots && streams[slot].loop; }

void streamRewind(uint32_t slot)
{
	if(slot >= StreamSlots)
		return;
	const Entry *e = entry(streams[slot].sound);
	if(e == nullptr)
		return;
	streams[slot].file_offset = 0;
	streams[slot].remaining = e->length;
}

uint32_t streamRead(uint32_t slot, int16_t *out, uint32_t frames)
{
	if(slot >= StreamSlots || out == nullptr)
		return 0;

	StreamState &s = streams[slot];
	uint32_t available = s.head - s.tail;
	if(available < frames)
	{
		if(s.open && available < frames)
			++s.underruns;
	}

	uint32_t produced = 0;
	while(produced < frames && produced < available)
	{
		out[produced++] = s.ring[s.tail & (StreamFrames - 1)];
		++s.tail;
	}
	for(; produced < frames; ++produced)
		out[produced] = 0;

	return produced;
}

void streamPump(uint32_t slot)
{
	if(slot >= StreamSlots || !open_)
		return;

	StreamState &s = streams[slot];
	if(!s.open)
		return;

	const Entry *e = entry(s.sound);
	if(e == nullptr)
		return;

	while(s.head - s.tail < StreamFrames - PumpChunk)
	{
		if(s.remaining == 0)
		{
			if(!s.loop)
			{
				s.open = false;
				return;
			}
			s.file_offset = 0;
			s.remaining = e->length;
			if(s.remaining == 0)
			{
				s.open = false;
				return;
			}
		}

		uint32_t take = s.remaining < PumpChunk ? s.remaining : PumpChunk;
		if(!readAt(data_offset + e->offset + s.file_offset, pump_scratch, take))
			return;

		for(uint32_t i = 0; i < take; ++i)
		{
			const int16_t sample = static_cast<int16_t>((static_cast<int>(pump_scratch[i]) - 128) << 8);
			s.ring[s.head & (StreamFrames - 1)] = sample;
			++s.head;
		}

		s.file_offset += take;
		s.remaining -= take;
	}
}
}
