#include "audio_nspire_gpio.h"

#include "audio_gpio_hw.h"
#include "audio_manager.h"

#include <stdio.h>
#include <string.h>

#ifdef _TINSPIRE
#include <libndls.h>
#endif

namespace GameAudioGpio
{
using namespace GpioAudioHw;

namespace
{
	const unsigned int RingFrames = 2048;
	/** Longest diagnostic so the user can never hang the calculator. */
	const uint32_t MaxSweepMilliseconds = 4000;
	/** Cycles spent per microsecond of the busy wait on a 150 MHz CX. */
	const uint32_t BusyLoopCyclesPerMicrosecond = 150;

	int16_t pcm_ring[RingFrames];
	volatile uint32_t ring_head = 0;
	volatile uint32_t ring_tail = 0;
	volatile uint32_t underrun_count = 0;
	volatile uint32_t frames_consumed = 0;
	volatile uint32_t bits_emitted = 0;

	int32_t sd_acc = 0;
	/** Crystal ticks since the last mixer sample boundary, in bit units. */
	uint32_t sample_phase = 0;
	int32_t current_level = 0;

	bool enabled_ = false;
	/** Square-wave drive for a piezoelectric buzzer instead of the sigma-delta. */
	bool buzzer_drive_ = false;
	/** The GPIO line driven: 22 (dock pin 18) or 4 (dock pin 6, USB D+). */
	uint32_t audio_line = AudioGpioNumber;
	int vector_slot_ = -1;
	/** Whether the OS already had the timer's IRQ unmasked before enable(). */
	bool vic_mask_had_timer_ = false;
	RegisterSnapshot snapshot_ = {};
	uint32_t saved_cpsr = 0;
	const char *error_ = nullptr;
	char status_buffer[160] = {0};

	uint32_t readReg(uint32_t address);
	void writeReg(uint32_t address, uint32_t value);

	/**
	 * The line's place in the GPIO map, resolved at run time because the backend
	 * drives one of two lines and both live in different sections (GPIO 22 is
	 * section 2, USB D+ is section 0). Everything below addresses the selected
	 * line through these three.
	 */
	uint32_t sectionBase() { return sectionBaseFor(audio_line); }
	uint32_t bitMask() { return bitMaskFor(audio_line); }
	uint32_t bitIndex() { return bitFor(audio_line); }

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

	void resetRing()
	{
		ring_head = 0;
		ring_tail = 0;
		underrun_count = 0;
		frames_consumed = 0;
		bits_emitted = 0;
		sd_acc = 0;
		sample_phase = 0;
		current_level = 0;
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

	extern "C" void gpioIsrBody();

	/** Address of the interrupt body as the PL190 vector slot wants it. On the
	 *  host this is a plausible SDRAM slot, because host code addresses are not
	 *  ARM addresses and the simulation calls the body directly. */
	uint32_t handlerAddress()
	{
#ifdef _TINSPIRE
		return reinterpret_cast<uint32_t>(&gpioIsrBody);
#else
		return 0x10003000u;
#endif
	}

	/**
	 * Claims a free PL190 vector slot for `handler` and points it at the timer.
	 * Identical in shape to the UART backend's claim: the OS's interrupt
	 * dispatcher reads VICVECTADDR and calls whatever it returns, so an
	 * ordinary function address is all a service routine needs -- no code is
	 * patched and every other source keeps its own handler. The highest free
	 * slot is used because a lower slot index means a higher dispatch priority,
	 * and audio must never preempt the OS.
	 */
	int claimVectorSlot(uint32_t handler)
	{
		for(int slot = static_cast<int>(VicVectorSlots) - 1; slot >= 0; --slot)
		{
			const uint32_t ctrl_address = VicBase + VicVectorCtrl0 + 4u * static_cast<uint32_t>(slot);
			if((readReg(ctrl_address) & VicVectorCtrlEnable) != 0)
				continue; // the OS owns this slot

			const uint32_t addr_address = VicBase + VicVectorAddr0 + 4u * static_cast<uint32_t>(slot);
			writeReg(addr_address, handler);
			writeReg(ctrl_address, VicVectorCtrlEnable | TimerIrqNumber);
			if(readReg(addr_address) == handler
				&& readReg(ctrl_address) == (VicVectorCtrlEnable | TimerIrqNumber))
				return slot;

			writeReg(ctrl_address, 0);
			writeReg(addr_address, 0);
		}

		return -1;
	}

	void releaseVectorSlot(int slot)
	{
		if(slot < 0)
			return;
		writeReg(VicBase + VicVectorCtrl0 + 4u * static_cast<uint32_t>(slot), 0);
		writeReg(VicBase + VicVectorAddr0 + 4u * static_cast<uint32_t>(slot), 0);
	}

	void waitOneMillisecond()
	{
#ifdef _TINSPIRE
		msleep(1);
#else
		Sim::advanceTimerTicks(TimerClockHz / 1000);
#endif
	}

	/**
	 * Waits out one output bit (~61 us). Far finer than msleep's granularity,
	 * so the diagnostic's drain can stop the stream the moment the last real
	 * sample is consumed instead of counting the silence after it as underruns.
	 */
	void waitOneBit()
	{
#ifdef _TINSPIRE
		volatile uint32_t spins = (1000000u / BitRateHz) * BusyLoopCyclesPerMicrosecond;
		while(spins != 0)
			--spins;
#else
		Sim::advanceTimerTicks(TicksPerBit);
#endif
	}

	/** Reads the registers the backend is about to disturb. */
	void captureRegisters(RegisterSnapshot &s)
	{
		s.gpio_direction = readReg(sectionBase() + GpioDirection);
		s.gpio_output = readReg(sectionBase() + GpioOutput);
		s.timer_load = readReg(TimerBase + TimerLoad);
		s.timer_control = readReg(TimerBase + TimerControl);
		s.valid = true;
	}

	/** One line of one GPIO port: read-modify-write, never a blind store. */
	void setPin(uint32_t level)
	{
		const uint32_t mask = bitMask();
		const uint32_t output_address = sectionBase() + GpioOutput;
		const uint32_t output = readReg(output_address);
		writeReg(output_address, level != 0 ? (output | mask) : (output & ~mask));
	}

	void restoreRegisters(const RegisterSnapshot &s)
	{
		writeReg(sectionBase() + GpioDirection, s.gpio_direction);
		writeReg(sectionBase() + GpioOutput, s.gpio_output);
		writeReg(TimerBase + TimerControl, 0);
		writeReg(TimerBase + TimerIntClear, 1);
		writeReg(TimerBase + TimerLoad, s.timer_load);
		writeReg(TimerBase + TimerControl, s.timer_control);
	}

	/**
	 * Emits one output bit: the symmetric first-order sigma-delta step the
	 * UART backend uses (see emitByte() there for why both feedback steps are
	 * exactly 32768), with the sample advancing through a phase accumulator so
	 * that `BitRateHz` bits carry exactly `SampleRateHz` mixer samples --
	 * two and a little bits per sample on average, with no drift.
	 */
	void emitBit()
	{
		sample_phase += SampleRateHz;
		if(sample_phase >= BitRateHz)
		{
			sample_phase -= BitRateHz;
			if(ring_tail != ring_head)
			{
				current_level = static_cast<int32_t>(pcm_ring[ring_tail & (RingFrames - 1)]);
				++ring_tail;
				++frames_consumed;
			}
			else
			{
				current_level = 0;
				++underrun_count;
			}
		}

		uint32_t bit;
		if(buzzer_drive_)
		{
			// The classic direct drive for a piezoelectric buzzer: a full-swing
			// square wave whose polarity follows the sample (one-bit hard
			// limiting), so the buzzer is driven rail to rail at the audio's own
			// pitch with no filter and no amplifier. Silence holds the pin low,
			// so an idle game is electrically silent too -- no carrier and no
			// idle tone for the buzzer to hiss at. What is lost is loudness
			// dynamics: a buzzer is a tone device, so quiet and loud both come
			// out at full swing.
			bit = current_level > 0 ? 1u : 0u;
		}
		else
		{
			int32_t acc = sd_acc + current_level;
			if(acc >= 0)
			{
				acc -= 32768;
				bit = 1;
			}
			else
			{
				acc += 32768;
				bit = 0;
			}
			sd_acc = acc;
		}

		++bits_emitted;
		setPin(bit);
	}
}

// The interrupt body is shared verbatim by the device vector slot and the host
// simulation, so the modulator and the register sequence are the same code.
// `externally_visible` matters under LTO: the PL190 slot holds its address as
// data, which link-time optimisation cannot see.
extern "C" void gpioIsrBody() __attribute__((used, externally_visible));
extern "C" void gpioIsrBody()
{
	// Called through the PL190 vector slot by the OS's interrupt dispatcher,
	// exactly like one of its own service routines: an ordinary function that
	// serves its source and then signals end of interrupt.
	if((readReg(TimerBase + TimerIntStatus) & 1u) != 0)
	{
		writeReg(TimerBase + TimerIntClear, 1);
		emitBit();
	}

	// End of interrupt: writing VICVECTADDR tells the priority hardware the
	// current interrupt is serviced, re-enabling lower/equal priority sources.
	writeReg(VicBase + VicIrqVector, 0);
}

bool supported()
{
#ifdef _TINSPIRE
	// The GPIO and timer map is the CX's; the classic machines differ.
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

	captureRegisters(snapshot_);

	saved_cpsr = irqSave();
	irqMask();

	// The vector slot must be armed before the timer source is unmasked. Every
	// other interrupt source keeps its own slot and handler: the OS dispatcher
	// simply calls this one when the timer fires.
	vector_slot_ = claimVectorSlot(handlerAddress());
	if(vector_slot_ < 0)
	{
		error_ = "no free interrupt vector slot";
		irqRestore(saved_cpsr);
		return false;
	}

	// The line becomes an output at a known level before the bit clock starts,
	// so the first edge is a modulator bit and not a power-up glitch.
	writeReg(sectionBase() + GpioDirection, snapshot_.gpio_direction & ~bitMask());
	setPin(0);

	// Stand the timer down, clear any stale interrupt, then run it periodic at
	// one interrupt per bit. Writing the load register also arms the reload.
	writeReg(TimerBase + TimerControl, 0);
	writeReg(TimerBase + TimerIntClear, 1);
	writeReg(TimerBase + TimerLoad, TimerReload);
	writeReg(TimerBase + TimerControl, TimerControlValue);

	// Add only the timer's bit to the OS's interrupt mask; nothing else is
	// touched, so the OS keeps running exactly as before. Whether the bit was
	// already there is remembered, so disable() only takes back what it added.
	vic_mask_had_timer_ = (readReg(VicBase + VicIntEnable) & (1u << TimerIrqNumber)) != 0;
	writeReg(VicBase + VicIntEnable, 1u << TimerIrqNumber);

	enabled_ = true;
	irqRestore(saved_cpsr);

	return true;
}

void disable()
{
	if(!enabled_)
		return;

	saved_cpsr = irqSave();
	irqMask();

	// Silence the source first, then take the vector back.
	writeReg(TimerBase + TimerControl, 0);
	writeReg(TimerBase + TimerIntClear, 1);

	if(!vic_mask_had_timer_)
		writeReg(VicBase + VicIntDisable, 1u << TimerIrqNumber);
	releaseVectorSlot(vector_slot_);
	vector_slot_ = -1;
	restoreRegisters(snapshot_);

	resetRing();
	enabled_ = false;
	snapshot_.valid = false;

	irqRestore(saved_cpsr);
}

bool active() { return enabled_; }
int vectorSlot() { return vector_slot_; }
void setBuzzerDrive(bool on) { buzzer_drive_ = on; }
bool buzzerDrive() { return buzzer_drive_; }

bool setAudioLine(unsigned int gpio)
{
	// Only the two dock lines this backend offers, and only while it is off:
	// the section base and the bit mask are read in enable()'s snapshot, so
	// changing the line under a running bit clock would drive one pin's port
	// register while the other was being bit-banged.
	if(enabled_ || !usableAsAudio(gpio))
		return false;

	audio_line = gpio;
	error_ = nullptr;
	return true;
}

unsigned int audioLine() { return audio_line; }
uint32_t bitRateHz() { return BitRateHz; }
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
	snprintf(status_buffer, sizeof(status_buffer),
		"%s (dock pin %u): %s, %s drive, %u Hz bit rate, vector slot %d, %u underruns",
		audio_line == UsbDataPlusGpioNumber ? "USB D+ (GPIO 4)" : "GPIO 22",
		audio_line == UsbDataPlusGpioNumber ? 6u : 18u, enabled_ ? "on" : "off",
		buzzer_drive_ ? "buzzer" : "sigma-delta", static_cast<unsigned int>(BitRateHz),
		vector_slot_, static_cast<unsigned int>(underrun_count));
	return status_buffer;
}

int testSweep(uint32_t duration_ms)
{
	if(duration_ms == 0)
		return -1;
	if(duration_ms > MaxSweepMilliseconds)
		duration_ms = MaxSweepMilliseconds;

	if(!enable())
		return -1;

	// A generated chirp means the diagnostic needs no audio pack.
	const uint32_t total_frames = (SampleRateHz * duration_ms) / 1000u;
	uint32_t produced = 0;
	uint32_t phase = 0;

	while(produced < total_frames)
	{
		while(produced < total_frames && ring_head - ring_tail < RingFrames - 128)
		{
			// 300 Hz to 2 kHz over the whole diagnostic.
			const uint32_t frequency = 300u + (1700u * produced) / (total_frames == 0 ? 1 : total_frames);
			phase += static_cast<uint32_t>((static_cast<uint64_t>(frequency) << 16) / SampleRateHz);
			const int32_t sample = (phase & 0x8000u) ? 12000 : -12000;
			pcm_ring[ring_head & (RingFrames - 1)] = static_cast<int16_t>(sample);
			++ring_head;
			++produced;
		}
		waitOneMillisecond();
	}

	// Let the ring drain before tearing the output down, one bit at a time so
	// the interrupt cannot run past the last real sample.
	while(ring_head != ring_tail)
		waitOneBit();

	const uint32_t underruns = underrun_count;
	disable();
	return underruns == 0 ? 0 : static_cast<int>(underruns);
}

int testBuzzerTone(uint32_t frequency_hz, uint32_t duration_ms)
{
	if(frequency_hz == 0 || duration_ms == 0)
		return -1;
	if(duration_ms > MaxSweepMilliseconds)
		duration_ms = MaxSweepMilliseconds;
	// A square wave cannot be represented above the mixer's Nyquist rate, and a
	// piezo's resonance is a few kHz anyway.
	if(frequency_hz > SampleRateHz / 2 - 200)
		frequency_hz = SampleRateHz / 2 - 200;

	const bool previous_drive = buzzer_drive_;
	buzzer_drive_ = true;
	if(!enable())
	{
		buzzer_drive_ = previous_drive;
		return -1;
	}

	// The beep is square-wave samples: in buzzer drive their polarity alone is
	// the output, so the pin plays exactly this tone at full swing.
	const uint32_t total_frames = (SampleRateHz * duration_ms) / 1000u;
	uint32_t produced = 0;
	uint32_t phase = 0;

	while(produced < total_frames)
	{
		while(produced < total_frames && ring_head - ring_tail < RingFrames - 128)
		{
			phase += static_cast<uint32_t>((static_cast<uint64_t>(frequency_hz) << 16) / SampleRateHz);
			const int32_t sample = (phase & 0x8000u) ? 12000 : -12000;
			pcm_ring[ring_head & (RingFrames - 1)] = static_cast<int16_t>(sample);
			++ring_head;
			++produced;
		}
		waitOneMillisecond();
	}

	while(ring_head != ring_tail)
		waitOneBit();

	const uint32_t underruns = underrun_count;
	disable();
	buzzer_drive_ = previous_drive;
	return underruns == 0 ? 0 : static_cast<int>(underruns);
}

#ifndef _TINSPIRE
// ---------------------------------------------------------------------------
// Host simulation: a sparse register file, one timer and a hand driven bit
// clock. Writing the timer's load register arms it; advanceTimerTicks() runs
// the counter the way the CX's does and then dispatches the interrupt through
// the same PL190 rules the device has. Writing to an unknown address is a
// no-op and reading it returns 0, standing in for the unwritten parts of the
// map -- except that a write outside the backend's own window is exactly the
// bug this simulation exists to catch, so it is counted.
namespace Sim
{
namespace
{
	const unsigned int RegisterCount = 64;

	struct RegisterPair
	{
		uint32_t address;
		uint32_t value;
		bool present;
	};

	RegisterPair registers[RegisterCount];

	// The first timer of block 0x900C0000.
	uint32_t timer_load = 0;
	uint32_t timer_value = 0;
	uint32_t timer_control = 0;
	uint32_t timer_interrupt = 0;
	uint32_t timer_prescale = 0;
	bool timer_reload_pending = false;

	uint32_t vic_irq_mask = 0;    // INTENABLE/INTENCLEAR views of one mask
	uint32_t vic_fiq_select = 0;  // INTSELECT: 1 routes the source to FIQ
	uint32_t eoi_writes = 0;      // end-of-interrupt writes to VICVECTADDR
	uint32_t bad_vic_writes = 0;  // writes to classic-only controller offsets
	uint32_t bad_peripheral_writes = 0; // writes outside the backend's window

	uint32_t pin_level = 0;
	uint32_t pin_transitions = 0;
	uint32_t pin_high_count = 0;
	uint32_t pin_low_count = 0;
	uint32_t timer_raises = 0;
	uint32_t crystal_ticks = 0;

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

	bool isVic(uint32_t address) { return address >= VicBase && address < VicBase + 0x400; }
	bool isGpioSection(uint32_t address) { return (address & ~0x3Fu) == sectionBase(); }
	bool isTimer0(uint32_t address) { return address >= TimerBase && address < TimerBase + 0x20; }

	void observePin(uint32_t level)
	{
		level &= 1u;
		if(level != pin_level)
			++pin_transitions;
		if(level)
			++pin_high_count;
		else
			++pin_low_count;
		pin_level = level;
	}

	/** The timer's IRQ line: level asserted while the flag and its enable agree. */
	bool timerIrqAsserted() { return timer_interrupt != 0 && (timer_control & 0x20u) != 0; }
}

void reset()
{
	for(unsigned int i = 0; i < RegisterCount; ++i)
	{
		registers[i].present = false;
		registers[i].address = 0;
		registers[i].value = 0;
	}

	timer_load = 0;
	timer_value = 0;
	timer_control = 0;
	timer_interrupt = 0;
	timer_prescale = 0;
	timer_reload_pending = false;

	vic_irq_mask = 0;
	vic_fiq_select = 0;
	eoi_writes = 0;
	bad_vic_writes = 0;
	bad_peripheral_writes = 0;

	pin_level = 0;
	pin_transitions = 0;
	pin_high_count = 0;
	pin_low_count = 0;
	timer_raises = 0;
	crystal_ticks = 0;

	// A plausible CX boot state: the OS owns the first PL190 vector slots and
	// its usual sources are unmasked (including timer IRQs 17 and 18, which it
	// dispatches through the default vector), and every GPIO line is an input
	// with the output latch low, exactly as the emulator resets them.
	auto seed = [](uint32_t address, uint32_t value)
	{
		RegisterPair *pair = store(address);
		if(pair != nullptr)
			pair->value = value;
	};

	// Both sections a supported audio line can live in (section 0 for USB D+,
	// section 2 for dock pin 18), so either line can be driven from the same
	// seeded boot state.
	seed(GpioSectionBase + GpioDirection, 0xFF);
	seed(GpioSectionBase + GpioOutput, 0x00);
	seed(GpioSectionBase + GpioInput, 0x1F);
	seed(UsbDataPlusSectionBase + GpioDirection, 0xFF);
	seed(UsbDataPlusSectionBase + GpioOutput, 0x00);
	seed(UsbDataPlusSectionBase + GpioInput, 0x1F);
	seed(VicBase + VicVectorAddr0 + 4 * 0, 0x10001000u); // fast timer
	seed(VicBase + VicVectorCtrl0 + 4 * 0, VicVectorCtrlEnable | 17);
	seed(VicBase + VicVectorAddr0 + 4 * 1, 0x10001100u); // keypad
	seed(VicBase + VicVectorCtrl0 + 4 * 1, VicVectorCtrlEnable | 16);
	seed(VicBase + VicVectorAddr0 + 4 * 2, 0x10001200u); // LCD
	seed(VicBase + VicVectorCtrl0 + 4 * 2, VicVectorCtrlEnable | 21);
	vic_irq_mask = (1u << 16) | (1u << 17) | (1u << 18) | (1u << 21);
}

uint32_t read(uint32_t address)
{
	if(address == TimerBase + TimerValue)
		return timer_value;
	if(address == TimerBase + TimerLoad || address == TimerBase + TimerBackgroundLoad)
		return timer_load;
	if(address == TimerBase + TimerControl)
		return timer_control;
	if(address == TimerBase + TimerIntStatus)
		return timer_interrupt;
	if(address == TimerBase + TimerMaskedStatus)
		return timer_interrupt & (timer_control >> 5);

	if(address == VicBase + VicIntEnable || address == VicBase + VicIntDisable)
		return vic_irq_mask; // two views of one enable mask
	if(address == VicBase + VicIntSelect)
		return vic_fiq_select;

	const RegisterPair *pair = find(address);
	return pair != nullptr ? pair->value : 0;
}

void write(uint32_t address, uint32_t value)
{
	// The PL190: +0x10 sets enable bits, +0x14 clears them, +0x0C routes
	// sources to FIQ, +0x30 is the end-of-interrupt write. Everything else in
	// the controller's window belongs to the classic machine's map and has no
	// register here; a write to one is the bug this simulation exists to catch.
	if(address == VicBase + VicIntEnable)
	{
		vic_irq_mask |= value;
		return;
	}
	if(address == VicBase + VicIntDisable)
	{
		vic_irq_mask &= ~value;
		return;
	}
	if(address == VicBase + VicIntSelect)
	{
		vic_fiq_select = value;
		return;
	}
	if(address == VicBase + VicIrqVector)
	{
		++eoi_writes;
		return;
	}
	if(isVic(address))
	{
		const uint32_t offset = address - VicBase;
		const bool is_slot = (offset >= VicVectorAddr0 && offset < VicVectorAddr0 + 4 * VicVectorSlots)
			|| (offset >= VicVectorCtrl0 && offset < VicVectorCtrl0 + 4 * VicVectorSlots);
		if(!is_slot && offset != 0x004 && offset != 0x01C && offset != VicDefaultVector)
		{
			++bad_vic_writes;
			return;
		}
		RegisterPair *pair = store(address);
		if(pair != nullptr)
			pair->value = value;
		return;
	}

	// The timer: only the four registers the backend programs exist for timer 0.
	if(isTimer0(address))
	{
		switch(address - TimerBase)
		{
		case TimerLoad:
			timer_load = value;
			timer_reload_pending = true; // writing load also arms a reload
			return;
		case TimerBackgroundLoad:
			timer_load = value;
			return;
		case TimerControl:
			timer_control = value;
			return;
		case TimerIntClear:
			timer_interrupt = 0;
			return;
		case TimerValue:
			return; // ignored, as on the hardware
		default:
			++bad_peripheral_writes;
			return;
		}
	}
	if(address >= TimerBase && address < TimerBase + 0x40)
	{
		++bad_peripheral_writes; // the other timer of the block is not ours
		return;
	}

	// The GPIO section that owns the selected audio line. The port registers are
	// byte wide and shared by the section's eight lines, so a write that changes
	// a bit other than the audio line's would disturb someone else's pin.
	if(isGpioSection(address))
	{
		const uint32_t mask = bitMask();
		const uint32_t offset = address & 0x3Fu;
		if(offset == GpioDirection || offset == GpioOutput)
		{
			RegisterPair *pair = store(address);
			const uint32_t current = pair != nullptr ? pair->value : 0;
			if(((current ^ value) & ~mask & 0xFFu) != 0)
				++bad_peripheral_writes;
			if(pair != nullptr)
				pair->value = value & 0xFFu;
			if(offset == GpioOutput)
				observePin((value >> bitIndex()) & 1u);
			return;
		}
		if(offset == GpioIntMaskSet || offset == GpioIntMaskClear
			|| offset == GpioInvert || offset == GpioStickySelect || offset == 0x24)
		{
			RegisterPair *pair = store(address);
			if(pair != nullptr)
				pair->value = value & 0xFFu;
			return;
		}
		++bad_peripheral_writes; // status registers are not written from here
		return;
	}

	++bad_peripheral_writes; // a stray address: nothing the backend owns
}

void advanceTimerTicks(uint32_t ticks)
{
	for(uint32_t i = 0; i < ticks; ++i)
	{
		++crystal_ticks;

		// The CX counter, in the emulator's terms: count down only while the
		// prescale allows, reload from the load register in periodic mode, and
		// raise the interrupt on the transition to zero.
		++timer_prescale;
		if((timer_control & 0x80u) != 0
			&& (timer_prescale & ((1u << (timer_control & 0xCu)) - 1u)) == 0)
		{
			if(timer_reload_pending)
			{
				timer_reload_pending = false;
				timer_value = timer_load;
			}
			else
			{
				const uint32_t old_value = timer_value;
				if(timer_value == 0)
				{
					if((timer_control & 0x01u) == 0) // not one-shot
					{
						timer_value = 0xFFFFFFFFu;
						if((timer_control & 0x40u) != 0)
							timer_value = timer_load;
					}
				}
				else
					--timer_value;

				if(old_value != 0 && timer_value == 0)
				{
					timer_interrupt = 1;
					++timer_raises;
				}
			}
		}

		// The timer's IRQ is level triggered: while it is asserted, unmasked and
		// not routed to FIQ, the OS dispatcher serves it through the armed
		// vector slot -- and only then.
		if(timerIrqAsserted() && isrSlotClaimed()
			&& (vic_irq_mask & (1u << TimerIrqNumber)) != 0
			&& (vic_fiq_select & (1u << TimerIrqNumber)) == 0)
			gpioIsrBody();
	}
}

void advanceBitTicks(uint32_t bits)
{
	advanceTimerTicks(bits * TicksPerBit);
}

uint32_t pinLevel() { return pin_level; }
uint32_t pinTransitions() { return pin_transitions; }
uint32_t pinHighCount() { return pin_high_count; }
uint32_t pinLowCount() { return pin_low_count; }
uint32_t bitsEmitted() { return static_cast<uint32_t>(bits_emitted); }
uint32_t timerIrqs() { return timer_raises; }

int claimedSlot()
{
	for(uint32_t slot = 0; slot < VicVectorSlots; ++slot)
	{
		const RegisterPair *ctrl = find(VicBase + VicVectorCtrl0 + 4 * slot);
		const RegisterPair *addr = find(VicBase + VicVectorAddr0 + 4 * slot);
		if(ctrl != nullptr && (ctrl->value & VicVectorCtrlEnable) != 0
			&& (ctrl->value & 0x1Fu) == TimerIrqNumber
			&& addr != nullptr && addr->value == handlerAddress())
			return static_cast<int>(slot);
	}
	return -1;
}

bool isrSlotClaimed() { return claimedSlot() >= 0; }

bool isrSlotReleased()
{
	for(uint32_t slot = 0; slot < VicVectorSlots; ++slot)
	{
		const RegisterPair *ctrl = find(VicBase + VicVectorCtrl0 + 4 * slot);
		if(ctrl != nullptr && (ctrl->value & VicVectorCtrlEnable) != 0
			&& (ctrl->value & 0x1Fu) == TimerIrqNumber)
			return false;
	}
	return true;
}

uint32_t slotControl(uint32_t slot)
{
	const RegisterPair *pair = find(VicBase + VicVectorCtrl0 + 4 * slot);
	return pair != nullptr ? pair->value : 0;
}

uint32_t vicEnabledMask() { return vic_irq_mask; }
uint32_t vicFiqSelect() { return vic_fiq_select; }
uint32_t endOfInterruptWrites() { return eoi_writes; }
uint32_t badVicWrites() { return bad_vic_writes; }
uint32_t badPeripheralWrites() { return bad_peripheral_writes; }
uint32_t pcmFramesConsumed() { return static_cast<uint32_t>(frames_consumed); }
}
#endif
} // namespace GameAudioGpio
