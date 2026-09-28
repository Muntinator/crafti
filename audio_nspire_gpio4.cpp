#include "audio_nspire_gpio4.h"

#include "audio_gpio4_hw.h"
#include "audio_manager.h"

#include <stdio.h>
#include <string.h>

#ifdef _TINSPIRE
#include <libndls.h>
#endif

namespace GameAudioGpio4
{
using namespace Gpio4Hw;

namespace
{
	const unsigned int RingFrames = 2048;
	/** Cycles spent per microsecond of the blocking diagnostic on a 150 MHz CX. */
	const uint32_t BusyLoopCyclesPerMicrosecond = 150;
	/** Longest blocking diagnostic so the user can never hang the calculator. */
	const uint32_t MaxPolledMilliseconds = 4000;

	int16_t pcm_ring[RingFrames];
	volatile uint32_t ring_head = 0;
	volatile uint32_t ring_tail = 0;
	volatile uint32_t underrun_count = 0;
	volatile uint32_t frames_consumed = 0;

	int32_t sd_acc = 0;
	int32_t sd_level = 0;
	uint32_t carrier_tick = 0;

	bool enabled_ = false;
	VectorTable table_ = VectorNone;
	uint32_t vector_address = 0;
	uint32_t vector_original[2] = {0, 0};
	unsigned int vector_words = 0;
	RegisterSnapshot snapshot_ = {};
	uint32_t saved_cpsr = 0;
	const char *error_ = nullptr;
	char status_buffer[160] = {0};

	uint32_t readReg(uint32_t address);
	void writeReg(uint32_t address, uint32_t value);

#ifdef _TINSPIRE
	uint32_t readReg(uint32_t address)
	{
		return *reinterpret_cast<volatile uint32_t *>(address);
	}

	void writeReg(uint32_t address, uint32_t value)
	{
		*reinterpret_cast<volatile uint32_t *>(address) = value;
	}
#else
	uint32_t readReg(uint32_t address) { return Sim::read(address); }
	void writeReg(uint32_t address, uint32_t value) { Sim::write(address, value); }
#endif

	void setPinLevel(bool high)
	{
		const uint32_t current = readReg(Gpio4OutputAddress);
		writeReg(Gpio4OutputAddress, high ? (current | Gpio4Mask) : (current & ~Gpio4Mask));
	}

	void resetRing()
	{
		ring_head = 0;
		ring_tail = 0;
		underrun_count = 0;
		frames_consumed = 0;
		carrier_tick = 0;
		sd_acc = 0;
		sd_level = 0;
	}

	uint32_t irqSave()
	{
#ifdef _TINSPIRE
		uint32_t cpsr;
		__asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
		return cpsr;
#else
		return 0;
#endif
	}

	void irqMask()
	{
#ifdef _TINSPIRE
		uint32_t cpsr;
		__asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
		cpsr |= 0x80u;
		__asm__ volatile("msr cpsr_c, %0" ::"r"(cpsr));
#endif
	}

	void irqRestore(uint32_t cpsr)
	{
#ifdef _TINSPIRE
		__asm__ volatile("msr cpsr_c, %0" ::"r"(cpsr));
#else
		(void)cpsr;
#endif
	}

	bool isBranchInstruction(uint32_t instruction)
	{
		return (instruction & 0x0F000000u) == 0x0A000000u;
	}

	uint32_t branchTarget(uint32_t address, uint32_t instruction)
	{
		int32_t offset = static_cast<int32_t>(instruction & 0x00FFFFFFu);
		if(offset & 0x00800000)
			offset |= static_cast<int32_t>(0xFF000000u);
		return static_cast<uint32_t>(static_cast<int32_t>(address + 8) + (offset << 2));
	}

	/** Writes a B to `target`, verifying the write by reading it back. */
	bool writeBranch(uint32_t address, uint32_t target)
	{
		const int32_t delta = static_cast<int32_t>(target) - static_cast<int32_t>(address + 8);
		if((delta & 3) != 0)
			return false;
		if(delta < -(1 << 25) || delta >= (1 << 25))
			return false;

		const uint32_t instruction = 0x0A000000u | (static_cast<uint32_t>(delta >> 2) & 0x00FFFFFFu);
		const uint32_t original = readReg(address);
		writeReg(address, instruction);
		if(readReg(address) == instruction)
			return true;

		writeReg(address, original);
		return false;
	}

	/**
	 * Installs the classic `ldr pc, [pc, #-4]` / address pair.
	 *
	 * A plain branch can only reach +/-32 MiB, which is not enough if the OS
	 * keeps its handlers in a vector page far away from program memory, so the
	 * two word stub is the portable option. It is only ever written to a decoded
	 * branch target inside a handler, never to a vector entry itself (that would
	 * clobber the following vector).
	 */	bool writeHookStub(uint32_t address, uint32_t target)
	{
		const uint32_t first = 0xE51FF004u; // ldr pc, [pc, #-4]
		const uint32_t original0 = readReg(address);
		const uint32_t original1 = readReg(address + 4);
		if(original0 == 0 && original1 == 0)
			return false;

		writeReg(address, first);
		writeReg(address + 4, target);
		if(readReg(address) == first && readReg(address + 4) == target)
		{
			vector_address = address;
			vector_original[0] = original0;
			vector_original[1] = original1;
			vector_words = 2;
			return true;
		}

		writeReg(address, original0);
		writeReg(address + 4, original1);
		return false;
	}

#ifdef _TINSPIRE
	extern "C" void gpio4IsrTrampoline() __attribute__((naked));
	extern "C" void gpio4IsrTrampoline()
	{
		// A naked body: save what the C handler may clobber, run it, then return
		// from the interrupt. Masking every other VIC source means the only IRQ
		// that can land here is the fast timer, so no chaining is needed.
		__asm__ volatile(
			"stmfd sp!, {r0-r3, r12, lr}\n"
			"bl gpio4IsrBody\n"
			"ldmfd sp!, {r0-r3, r12, lr}\n"
			"subs pc, lr, #4\n");
	}
#else
	extern "C" void gpio4IsrTrampoline() {}
#endif

	/** Address handed to the interrupt branch. On the host this is a plausible
	 *  SDRAM slot, because host code addresses cannot be reached by an ARM
	 *  branch and the simulation never executes the trampoline anyway. */
	uint32_t trampolineAddress()
	{
#ifdef _TINSPIRE
		return reinterpret_cast<uint32_t>(&gpio4IsrTrampoline);
#else
		return 0x10002000u;
#endif
	}

	void busyWaitMicroseconds(uint32_t microseconds)
	{
#ifdef _TINSPIRE
		volatile uint32_t spins = microseconds * BusyLoopCyclesPerMicrosecond;
		while(spins != 0)
			--spins;
#else
		(void)microseconds;
#endif
	}

	void waitOneMillisecond()
	{
#ifdef _TINSPIRE
		msleep(1);
#else
		Sim::advanceCarrierTicks(CarrierHz / 1000);
#endif
	}
}

// The interrupt body is shared verbatim by the device trampoline and the host
// simulation, so the modulator and the register sequence are the same code.
// `externally_visible` matters under LTO: the only references from this file are
// assembler strings in the trampoline, which link-time optimisation cannot see.
extern "C" void gpio4IsrBody() __attribute__((used, externally_visible));
extern "C" void gpio4IsrBody()
{
	// PL190 acknowledgement sequence (Hackspire, "Handling Interrupts").
	const uint32_t irq = readReg(VicBase + VicIrqVector);
	const uint32_t previous_priority = readReg(VicBase + VicIrqAcknowledge);

	if(irq == FastTimerIrqNumber)
	{
		writeReg(TimerBase + Timer1InterruptClear, 1);

		if(++carrier_tick >= OversamplingRatio)
		{
			carrier_tick = 0;
			if(ring_tail != ring_head)
			{
				sd_level = static_cast<int32_t>(pcm_ring[ring_tail & (RingFrames - 1)]);
				++ring_tail;
				++frames_consumed;
			}
			else
			{
				sd_level = 0;
				++underrun_count;
			}
		}

		// First order sigma-delta: the 16-bit sample becomes a 1-bit stream
		// whose average tracks the sample, which is all a single digital pin
		// can reproduce.
		sd_acc += sd_level;
		if(sd_acc >= 0)
		{
			sd_acc -= 32767;
			setPinLevel(true);
		}
		else
		{
			sd_acc += 32768;
			setPinLevel(false);
		}
	}

	writeReg(VicBase + VicIrqMaxPriority, previous_priority);
}

namespace
{
	/** Installs the interrupt branch, trying the vector and its branch target. */
	bool installVector()
	{
		const uint32_t low = readReg(LowIrqVectorAddress);
		const uint32_t high = readReg(HighIrqVectorAddress);
		const uint32_t trampoline = trampolineAddress();

		if(isBranchInstruction(low) && writeHookStub(branchTarget(LowIrqVectorAddress, low), trampoline))
		{
			table_ = VectorLow;
			return true;
		}
		if(low != 0 && low != 0xFFFFFFFFu && writeBranch(LowIrqVectorAddress, trampoline))
		{
			vector_address = LowIrqVectorAddress;
			vector_original[0] = low;
			vector_original[1] = 0;
			vector_words = 1;
			table_ = VectorLow;
			return true;
		}

		// Boot1 ROM is mapped at address 0, so a writable vector entry usually
		// lives in the ARM high vector table instead.
		if(isBranchInstruction(high) && writeHookStub(branchTarget(HighIrqVectorAddress, high), trampoline))
		{
			table_ = VectorHigh;
			return true;
		}
		if(high != 0 && high != 0xFFFFFFFFu && writeBranch(HighIrqVectorAddress, trampoline))
		{
			vector_address = HighIrqVectorAddress;
			vector_original[0] = high;
			vector_original[1] = 0;
			vector_words = 1;
			table_ = VectorHigh;
			return true;
		}

		return false;
	}

	void restoreVector()
	{
		if(vector_address == 0)
			return;

		writeReg(vector_address, vector_original[0]);
		if(vector_words == 2)
			writeReg(vector_address + 4, vector_original[1]);

		vector_address = 0;
		vector_words = 0;
	}
}

bool supported()
{
#ifdef _TINSPIRE
	return !is_classic;
#else
	return true;
#endif
}

bool enable()
{
	if(enabled_)
		return true;

	error_ = nullptr;
	resetRing();

	snapshot_.timer_control = readReg(TimerBase + Timer1Control);
	snapshot_.timer_load = readReg(TimerBase + Timer1Load);
	snapshot_.timer_clock_select = readReg(TimerBase + TimerClockSelect);
	snapshot_.vic_mask = readReg(VicBase + VicIntEnable);
	snapshot_.gpio_direction = readReg(Gpio4DirectionAddress);
	snapshot_.gpio_output = readReg(Gpio4OutputAddress);
	snapshot_.valid = true;

	saved_cpsr = irqSave();
	irqMask();

	// The vector must be in place before any interrupt source is unmasked.
	if(!installVector())
	{
		error_ = "IRQ vector is not writable; use the polled test";
		restoreVector();
		irqRestore(saved_cpsr);
		return false;
	}

	// GPIO4 as an output, idle low.
	writeReg(Gpio4DirectionAddress, readReg(Gpio4DirectionAddress) & ~Gpio4Mask);
	setPinLevel(false);

	// Fast timer: periodic, 32-bit, interrupt every carrier period.
	writeReg(TimerBase + TimerClockSelect, TimerClockSelect10MHz);
	writeReg(TimerBase + Timer1Load, TimerReload);
	writeReg(TimerBase + Timer1Control,
		TimerControlEnable | TimerControlPeriodic | TimerControlInterruptEnable | TimerControl32Bit);
	writeReg(TimerBase + Timer1InterruptClear, 1);

	// Only the fast timer may interrupt while audio owns the vector.
	writeReg(VicBase + VicIntDisable, VicAllIrqs);
	writeReg(VicBase + VicIntEnable, 1u << FastTimerIrqNumber);
	(void)readReg(VicBase + VicIrqAcknowledge);

	enabled_ = true;
	irqRestore(saved_cpsr & ~0x80u); // clear the I bit: interrupts on

	return true;
}

void disable()
{
	if(!enabled_)
		return;

	saved_cpsr = irqSave();
	irqMask();

	writeReg(TimerBase + Timer1Control, 0);
	writeReg(TimerBase + Timer1InterruptClear, 1);

	writeReg(VicBase + VicIntDisable, VicAllIrqs);
	writeReg(VicBase + VicIntEnable, snapshot_.vic_mask);
	(void)readReg(VicBase + VicIrqAcknowledge);

	restoreVector();

	writeReg(Gpio4DirectionAddress, snapshot_.gpio_direction);
	writeReg(Gpio4OutputAddress, snapshot_.gpio_output);
	writeReg(TimerBase + TimerClockSelect, snapshot_.timer_clock_select);
	writeReg(TimerBase + Timer1Load, snapshot_.timer_load);
	writeReg(TimerBase + Timer1Control, snapshot_.timer_control);

	resetRing();
	enabled_ = false;
	snapshot_.valid = false;

	irqRestore(saved_cpsr);
}

bool active() { return enabled_; }
VectorTable vectorTable() { return table_; }
uint32_t carrierHz() { return CarrierHz; }
uint32_t ringUnderruns() { return underrun_count; }

void pump()
{
	if(!enabled_)
		return;

	// Keep the ring comfortably full. Blocks stay well under the stream ring's
	// headroom so no streamed voice can starve.
	while(ring_head - ring_tail < RingFrames - 128)
	{
		const uint32_t free_frames = RingFrames - (ring_head - ring_tail);
		uint32_t chunk = free_frames < 256 ? free_frames : 256;
		if(chunk == 0)
			break;

		int16_t scratch[256];
		const size_t produced = GameAudio::mixMono(scratch, chunk);
		if(produced == 0)
			break;

		for(size_t i = 0; i < produced; ++i)
		{
			pcm_ring[ring_head & (RingFrames - 1)] = scratch[i];
			++ring_head;
		}
	}
}

const char *lastError() { return error_; }

const char *status()
{
	const char *vector_name = "none";
	switch(table_)
	{
	case VectorLow:
		vector_name = "low";
		break;
	case VectorHigh:
		vector_name = "high";
		break;
	default:
		break;
	}

	snprintf(status_buffer, sizeof(status_buffer),
		"GPIO4 dock pin 6: %s, %u Hz carrier, vector %s, %u underruns",
		enabled_ ? "on" : "off", static_cast<unsigned int>(CarrierHz), vector_name,
		static_cast<unsigned int>(underrun_count));
	return status_buffer;
}

int testPolled(uint32_t frequency_hz, uint32_t duration_ms)
{
	if(frequency_hz == 0 || duration_ms == 0)
		return -1;
	if(frequency_hz > 20000)
		frequency_hz = 20000;
	if(duration_ms > MaxPolledMilliseconds)
		duration_ms = MaxPolledMilliseconds;
	if(!supported())
		return -1;

	const uint32_t direction = readReg(Gpio4DirectionAddress);
	const uint32_t output = readReg(Gpio4OutputAddress);

	writeReg(Gpio4DirectionAddress, direction & ~Gpio4Mask);

	const uint32_t half_period_us = 500000u / frequency_hz;
	const uint32_t periods = (frequency_hz * duration_ms) / 1000u;

	for(uint32_t i = 0; i < periods; ++i)
	{
		writeReg(Gpio4OutputAddress, readReg(Gpio4OutputAddress) | Gpio4Mask);
		busyWaitMicroseconds(half_period_us);
		writeReg(Gpio4OutputAddress, readReg(Gpio4OutputAddress) & ~Gpio4Mask);
		busyWaitMicroseconds(half_period_us);
	}

	writeReg(Gpio4DirectionAddress, direction);
	writeReg(Gpio4OutputAddress, output);
	return static_cast<int>(periods);
}

int testTimer(uint32_t duration_ms)
{
	if(duration_ms == 0)
		return -1;
	if(duration_ms > MaxPolledMilliseconds)
		duration_ms = MaxPolledMilliseconds;

	if(!enable())
		return -1;

	// A generated chirp means the diagnostic needs no audio pack.
	const uint32_t total_frames = (MixerRateHz * duration_ms) / 1000u;
	uint32_t produced = 0;
	uint32_t phase = 0;

	while(produced < total_frames)
	{
		while(produced < total_frames && ring_head - ring_tail < RingFrames - 128)
		{
			// 300 Hz to 2 kHz over the whole diagnostic.
			const uint32_t frequency = 300u + (1700u * produced) / (total_frames == 0 ? 1 : total_frames);
			phase += static_cast<uint32_t>((static_cast<uint64_t>(frequency) << 16) / MixerRateHz);
			const int32_t sample = (phase & 0x8000u) ? 12000 : -12000;
			pcm_ring[ring_head & (RingFrames - 1)] = static_cast<int16_t>(sample);
			++ring_head;
			++produced;
		}
		waitOneMillisecond();
	}

	// Let the ring drain before tearing the timer down.
	while(ring_head != ring_tail)
		waitOneMillisecond();

	const uint32_t underruns = underrun_count;
	disable();
	return underruns == 0 ? 0 : static_cast<int>(underruns);
}

#ifndef _TINSPIRE
// ---------------------------------------------------------------------------
// Host simulation: a sparse register file plus a hand driven carrier tick, so
// the exact register sequence above can be exercised in unit tests. Writing to
// an unknown address is a no-op and reading it returns 0, standing in for the
// unwritten parts of the memory map.
namespace Sim
{
namespace
{
	struct RegisterPair
	{
		uint32_t address;
		uint32_t value;
		bool present;
	};

	const unsigned int RegisterCount = 48;
	RegisterPair registers[RegisterCount];

	uint32_t pin_transitions = 0;
	uint32_t pin_high_count = 0;
	uint32_t pin_low_count = 0;
	uint32_t carrier_ticks = 0;
	uint32_t last_pin = 0;
	uint32_t vic_max_priority = 8;

	RegisterPair *find(uint32_t address)
	{
		for(unsigned int i = 0; i < RegisterCount; ++i)
			if(registers[i].present && registers[i].address == address)
				return &registers[i];
		return nullptr;
	}

	RegisterPair *store(uint32_t address)
	{
		RegisterPair *existing = find(address);
		if(existing != nullptr)
			return existing;
		for(unsigned int i = 0; i < RegisterCount; ++i)
		{
			if(!registers[i].present)
			{
				registers[i].present = true;
				registers[i].address = address;
				registers[i].value = 0;
				return &registers[i];
			}
		}
		return nullptr;
	}

	void observePin(uint32_t value)
	{
		const uint32_t level = value & Gpio4Mask ? 1u : 0u;
		if(level != last_pin)
			++pin_transitions;
		if(level)
			++pin_high_count;
		else
			++pin_low_count;
		last_pin = level;
	}
}

void reset()
{
	for(unsigned int i = 0; i < RegisterCount; ++i)
	{
		registers[i].present = false;
		registers[i].address = 0;
		registers[i].value = 0;
	}

	pin_transitions = 0;
	pin_high_count = 0;
	pin_low_count = 0;
	carrier_ticks = 0;
	last_pin = 0;
	vic_max_priority = 8;

	// A plausible boot state: the low IRQ vector branches into RAM, GPIO4 is an
	// input, no IRQs are unmasked, and the timer is stopped.
	auto seed = [](uint32_t address, uint32_t value)
	{
		RegisterPair *pair = store(address);
		if(pair != nullptr)
			pair->value = value;
	};

	// The low table sits in ROM, so its IRQ entry is a plain branch to a handler
	// that is out of branch range for the program: patching it cannot work. The
	// high table holds the writable handler the backend is expected to patch.
	seed(LowIrqVectorAddress, 0xE59FF018u); // ldr pc, [pc, #24], not a branch
	seed(LowIrqVectorAddress + 4, 0xEAFFFFFEu);
	seed(HighIrqVectorAddress, 0x0A000038u); // b 0xFFFF0100
	seed(HighIrqVectorAddress + 4, 0xEAFFFFFEu);
	seed(0xFFFF0100u, 0xE1A00000u); // the OS handler this build patches
	seed(0xFFFF0104u, 0xE1A00000u);
	seed(Gpio4DirectionAddress, Gpio4Mask); // input
	seed(Gpio4OutputAddress, 0);
	seed(VicBase + VicIrqCurrent, FastTimerIrqNumber);
	seed(VicBase + VicIrqVector, FastTimerIrqNumber);
	seed(VicBase + VicIrqAcknowledge, vic_max_priority);
	seed(VicBase + VicIrqMaxPriority, vic_max_priority);
	seed(VicBase + VicIntEnable, 0x00000003u); // keypad and timer sources, say
	seed(TimerBase + Timer1Control, 0);
	seed(TimerBase + Timer1Load, 0);
	seed(TimerBase + TimerClockSelect, 0);
}

uint32_t read(uint32_t address)
{
	const RegisterPair *pair = find(address);
	if(pair == nullptr)
		return 0;
	if(address == VicBase + VicIrqAcknowledge)
		return vic_max_priority;
	if(address == VicBase + VicIrqVector)
		return FastTimerIrqNumber;
	if(address == VicBase + VicIntDisable)
	{
		// Both mask addresses read back the same register on real hardware.
		const RegisterPair *mask = find(VicBase + VicIntEnable);
		return mask != nullptr ? mask->value : 0;
	}
	return pair->value;
}

void write(uint32_t address, uint32_t value)
{
	// On the PL190 the enable and disable addresses are two views of one mask
	// register, so they must not be modelled as separate storage.
	if(address == VicBase + VicIntEnable || address == VicBase + VicIntDisable)
	{
		RegisterPair *mask = store(VicBase + VicIntEnable);
		if(mask == nullptr)
			return;
		if(address == VicBase + VicIntEnable)
			mask->value |= value;
		else
			mask->value &= ~value;
		return;
	}

	RegisterPair *pair = store(address);
	if(pair == nullptr)
		return;

	if(address == VicBase + VicIrqMaxPriority)
		vic_max_priority = value;
	else
		pair->value = value;

	if(address == Gpio4OutputAddress)
		observePin(pair->value);
}

void advanceCarrierTicks(uint32_t ticks)
{
	for(uint32_t i = 0; i < ticks; ++i)
	{
		++carrier_ticks;
		gpio4IsrBody();
	}
}

uint32_t pinTransitions() { return pin_transitions; }
uint32_t pinHighCount() { return pin_high_count; }
uint32_t pinLowCount() { return pin_low_count; }
uint32_t carrierTickCount() { return carrier_ticks; }

uint32_t timerReloadValue()
{
	const RegisterPair *pair = find(TimerBase + Timer1Load);
	return pair != nullptr ? pair->value : 0;
}

bool vectorInstalled()
{
	const RegisterPair *first = find(0xFFFF0100u);
	const RegisterPair *second = find(0xFFFF0104u);
	return first != nullptr && second != nullptr
		&& first->value == 0xE51FF004u && second->value == 0x10002000u;
}

/** True when the handler slot still holds its original instruction. */
bool vectorRestored()
{
	const RegisterPair *pair = find(0xFFFF0100u);
	return pair != nullptr && pair->value == 0xE1A00000u;
}

uint32_t vicEnabledMask()
{
	const RegisterPair *pair = find(VicBase + VicIntEnable);
	return pair != nullptr ? pair->value : 0;
}

uint32_t pcmFramesConsumed()
{
	return static_cast<uint32_t>(frames_consumed);
}

bool writeWav(const char *path, uint32_t milliseconds)
{
	if(path == nullptr || milliseconds == 0)
		return false;

	const uint32_t frames = (MixerRateHz * milliseconds) / 1000u;
	FILE *file = fopen(path, "wb");
	if(file == nullptr)
		return false;

	resetRing();

	const uint32_t data_bytes = frames * 2;
	const uint32_t riff_size = 36 + data_bytes;

	uint8_t header[44];
	memcpy(header, "RIFF", 4);
	header[4] = static_cast<uint8_t>(riff_size);
	header[5] = static_cast<uint8_t>(riff_size >> 8);
	header[6] = static_cast<uint8_t>(riff_size >> 16);
	header[7] = static_cast<uint8_t>(riff_size >> 24);
	memcpy(header + 8, "WAVEfmt ", 8);
	header[16] = 16;
	header[17] = 0; header[18] = 0; header[19] = 0;
	header[20] = 1; header[21] = 0; // PCM
	header[22] = 1; header[23] = 0; // mono
	const uint32_t rate = MixerRateHz;
	header[24] = static_cast<uint8_t>(rate); header[25] = static_cast<uint8_t>(rate >> 8);
	header[26] = static_cast<uint8_t>(rate >> 16); header[27] = static_cast<uint8_t>(rate >> 24);
	const uint32_t byte_rate = rate * 2;
	header[28] = static_cast<uint8_t>(byte_rate); header[29] = static_cast<uint8_t>(byte_rate >> 8);
	header[30] = static_cast<uint8_t>(byte_rate >> 16); header[31] = static_cast<uint8_t>(byte_rate >> 24);
	header[32] = 2; header[33] = 0;
	header[34] = 16; header[35] = 0;
	memcpy(header + 36, "data", 4);
	header[40] = static_cast<uint8_t>(data_bytes);
	header[41] = static_cast<uint8_t>(data_bytes >> 8);
	header[42] = static_cast<uint8_t>(data_bytes >> 16);
	header[43] = static_cast<uint8_t>(data_bytes >> 24);
	fwrite(header, 1, sizeof(header), file);

	// Push a known 440 Hz tone through the real modulator and reconstruct the
	// audio by averaging each oversampling window, which is what the 1-bit
	// stream means once it leaves the pin.
	uint32_t phase = 0;
	for(uint32_t frame = 0; frame < frames; ++frame)
	{
		phase += static_cast<uint32_t>((static_cast<uint64_t>(440) << 16) / MixerRateHz);
		const int32_t sample = (phase & 0x8000u) ? 12000 : -12000;

		pcm_ring[ring_head & (RingFrames - 1)] = static_cast<int16_t>(sample);
		++ring_head;

		uint32_t high = 0;
		for(uint32_t i = 0; i < OversamplingRatio; ++i)
		{
			advanceCarrierTicks(1);
			if((read(Gpio4OutputAddress) & Gpio4Mask) != 0)
				++high;
		}

		int32_t value = static_cast<int32_t>((high * 65535u) / OversamplingRatio) - 32768;
		if(value > 32767) value = 32767;
		if(value < -32768) value = -32768;
		const uint16_t raw = static_cast<uint16_t>(static_cast<int16_t>(value));
		const uint8_t bytes[2] = {static_cast<uint8_t>(raw & 0xFF), static_cast<uint8_t>(raw >> 8)};
		fwrite(bytes, 1, 2, file);
	}

	fclose(file);
	return true;
}
}
#endif
}
