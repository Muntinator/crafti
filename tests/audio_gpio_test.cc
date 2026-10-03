// Host tests for the GPIO 22 output backend.
//
// The backend writes real TI-Nspire registers, so it cannot run on a host as
// written. What it *can* do is run the identical code against the simulated
// register file in audio_nspire_gpio.cpp: the interrupt body, the register
// setup and the teardown are the same instructions either way. Only the
// electrical behaviour of dock pin 18 is left unverified.
//
// Build and run with `make -C tests`.

#include "audio_gpio_hw.h"
#include "audio_manager.h"
#include "audio_nspire_gpio.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

static void testRegisterSetup()
{
	using namespace GpioAudioHw;

	GameAudioGpio::Sim::reset();
	GameAudio::initialize();

	CHECK(GameAudioGpio::supported());
	CHECK(!GameAudioGpio::active());
	CHECK(GameAudioGpio::enable());
	CHECK(GameAudioGpio::active());

	// GPIO 22 is section 2, bit 6: a compile-time fact, but this test is the
	// one place that pins it for a reader.
	CHECK(22u == GpioSection * 8 + GpioBit);
	CHECK(GpioSectionBase == 0x90000080u);

	// The line is an output now, and every other line of the section kept the
	// direction it booted with.
	CHECK((GameAudioGpio::Sim::read(GpioSectionBase + GpioDirection) & GpioBitMask) == 0);
	CHECK((GameAudioGpio::Sim::read(GpioSectionBase + GpioDirection) & ~GpioBitMask) == (0xFFu & ~GpioBitMask));
	// ...and the output starts at a known low level, not a power-up glitch.
	CHECK(GameAudioGpio::Sim::pinLevel() == 0);

	// The timer runs periodic at one interrupt per bit: enable, periodic,
	// interrupt enable, 32-bit counter, no prescale, load = ticks per bit - 1.
	CHECK(BitRateHz == 16384);
	CHECK(TicksPerBit == 2);
	CHECK(TimerReload == 1);
	CHECK(GameAudioGpio::Sim::read(TimerBase + TimerControl) == TimerControlValue);
	CHECK(GameAudioGpio::Sim::read(TimerBase + TimerLoad) == TimerReload);
	CHECK(GameAudioGpio::bitRateHz() == BitRateHz);

	// The timer's own IRQ, not the UART's, and only that bit is added to the
	// OS's mask.
	CHECK(TimerIrqNumber == 18);
	const uint32_t os_sources = (1u << 16) | (1u << 17) | (1u << 18) | (1u << 21);
	CHECK(GameAudioGpio::Sim::vicEnabledMask() == os_sources);

	// The service routine sits in the highest free PL190 vector slot, i.e. at
	// the lowest dispatch priority, and the OS keeps the slots it owns.
	CHECK(GameAudioGpio::Sim::isrSlotClaimed());
	CHECK(GameAudioGpio::Sim::claimedSlot() == 15);
	CHECK(GameAudioGpio::vectorSlot() == GameAudioGpio::Sim::claimedSlot());
	CHECK(GameAudioGpio::Sim::slotControl(0) == (VicVectorCtrlEnable | 17));
	CHECK(GameAudioGpio::Sim::slotControl(1) == (VicVectorCtrlEnable | 16));
	CHECK(GameAudioGpio::Sim::slotControl(2) == (VicVectorCtrlEnable | 21));

	// The enable path must stay on the CX's PL190 map: nothing written to the
	// FIQ routing register, nothing to classic-only offsets, and nothing
	// outside the backend's own window.
	CHECK(GameAudioGpio::Sim::vicFiqSelect() == 0);
	CHECK(GameAudioGpio::Sim::badVicWrites() == 0);
	CHECK(GameAudioGpio::Sim::badPeripheralWrites() == 0);

	GameAudioGpio::disable();
}

static void testSilenceIsAlternatingBits()
{
	// Digital silence must produce a perfectly alternating bit stream: a first
	// order sigma-delta whose two feedback steps are the same size holds its
	// accumulator at the centre of its range. An asymmetric modulator drifts
	// for about a second and then slips -- an audible thump at the reset -- and
	// over two seconds of silence that shows up as a lost transition and an
	// unbalanced high/low count, so this pins the symmetry.
	GameAudioGpio::Sim::reset();
	CHECK(GameAudioGpio::enable());

	const uint32_t transitions_before = GameAudioGpio::Sim::pinTransitions();
	const uint32_t high_before = GameAudioGpio::Sim::pinHighCount();
	const uint32_t low_before = GameAudioGpio::Sim::pinLowCount();

	// No voices are playing, so the ring stays empty and every frame is
	// silence. One second of it: 16384 bits, 8000 mixer samples.
	GameAudioGpio::Sim::advanceBitTicks(16384);

	CHECK(GameAudioGpio::Sim::bitsEmitted() == 16384);
	CHECK(GameAudioGpio::Sim::timerIrqs() == 16384);
	// Every bit flips the pin: strict alternation, no drift, no idle tone.
	CHECK(GameAudioGpio::Sim::pinTransitions() - transitions_before == 16384);
	CHECK(GameAudioGpio::Sim::pinHighCount() - high_before == 8192);
	CHECK(GameAudioGpio::Sim::pinLowCount() - low_before == 8192);
	// Silence still costs exactly one mixer sample per sample period.
	CHECK(GameAudioGpio::ringUnderruns() == 8000);
	CHECK(GameAudioGpio::Sim::pcmFramesConsumed() == 0);

	CHECK(GameAudioGpio::Sim::badPeripheralWrites() == 0);
	GameAudioGpio::disable();
}

static void testSamplesAdvanceAtTheMixerRate()
{
	// The whole point of the phase accumulator: the pin runs at its own bit
	// rate while mixer samples are consumed at exactly 8 kHz. 205 bit periods
	// hold floor(205 * 8000 / 16384) = 100 whole mixer samples -- two and a
	// little bits per sample, with no drift over time.
	GameAudioGpio::Sim::reset();
	GameAudio::initialize();
	CHECK(GameAudioGpio::enable());

	GameAudio::play(GameAudio::EventMenuSelect);
	GameAudioGpio::pump();
	CHECK(GameAudioGpio::Sim::pcmFramesConsumed() == 0);

	GameAudioGpio::Sim::advanceBitTicks(205);
	CHECK(GameAudioGpio::Sim::bitsEmitted() == 205);
	CHECK(GameAudioGpio::Sim::pcmFramesConsumed() == 100);
	CHECK(GameAudioGpio::ringUnderruns() == 0);

	GameAudioGpio::disable();
}

static void testModulatorDrivesThePin()
{
	GameAudioGpio::Sim::reset();
	CHECK(GameAudioGpio::enable());

	GameAudio::play(GameAudio::EventMenuSelect);
	GameAudioGpio::pump();

	const uint32_t transitions_before = GameAudioGpio::Sim::pinTransitions();
	const uint32_t high_before = GameAudioGpio::Sim::pinHighCount();
	const uint32_t low_before = GameAudioGpio::Sim::pinLowCount();

	GameAudioGpio::Sim::advanceBitTicks(205);

	// A 1-bit output can only be meaningful if the pin actually toggles, and a
	// first-order modulator must use both levels rather than sitting at one.
	CHECK(GameAudioGpio::Sim::pinTransitions() - transitions_before > 20);
	CHECK(GameAudioGpio::Sim::pinHighCount() - high_before > 0);
	CHECK(GameAudioGpio::Sim::pinLowCount() - low_before > 0);

	// Two hundred modulator bits are two hundred read-modify-writes of a shared
	// port register, and not one of them may touch another line's bits.
	CHECK(GameAudioGpio::Sim::badPeripheralWrites() == 0);
	CHECK(GameAudioGpio::Sim::vicFiqSelect() == 0);
	CHECK(GameAudioGpio::Sim::badVicWrites() == 0);

	GameAudioGpio::disable();
}

static void testTeardownRestoresEverything()
{
	using namespace GpioAudioHw;

	GameAudioGpio::Sim::reset();

	// Remember the boot state the simulation seeds.
	const uint32_t direction = GameAudioGpio::Sim::read(GpioSectionBase + GpioDirection);
	const uint32_t output = GameAudioGpio::Sim::read(GpioSectionBase + GpioOutput);
	const uint32_t load = GameAudioGpio::Sim::read(TimerBase + TimerLoad);
	const uint32_t control = GameAudioGpio::Sim::read(TimerBase + TimerControl);
	const uint32_t vic = GameAudioGpio::Sim::vicEnabledMask();

	CHECK(GameAudioGpio::enable());
	CHECK(GameAudioGpio::Sim::isrSlotClaimed());
	GameAudioGpio::disable();

	CHECK(!GameAudioGpio::active());
	CHECK(GameAudioGpio::Sim::read(GpioSectionBase + GpioDirection) == direction);
	CHECK(GameAudioGpio::Sim::read(GpioSectionBase + GpioOutput) == output);
	CHECK(GameAudioGpio::Sim::read(TimerBase + TimerLoad) == load);
	CHECK(GameAudioGpio::Sim::read(TimerBase + TimerControl) == control);
	// The OS had the timer's IRQ unmasked when we found it, and only the bit
	// this backend added is ever taken back.
	CHECK(GameAudioGpio::Sim::vicEnabledMask() == vic);
	CHECK(GameAudioGpio::Sim::isrSlotReleased()); // the vector slot was given back
	CHECK(GameAudioGpio::vectorSlot() == -1);
	CHECK(GameAudioGpio::Sim::vicFiqSelect() == 0);
	CHECK(GameAudioGpio::Sim::badVicWrites() == 0);
	CHECK(GameAudioGpio::Sim::badPeripheralWrites() == 0);

	// Disabling twice is harmless.
	GameAudioGpio::disable();

	// The other direction: the OS mask without the timer's bit must also come
	// back as it was, bit not added and not left behind.
	GameAudioGpio::Sim::write(VicBase + VicIntDisable, 1u << TimerIrqNumber);
	const uint32_t vic_without = GameAudioGpio::Sim::vicEnabledMask();
	CHECK((vic_without & (1u << TimerIrqNumber)) == 0);
	CHECK(GameAudioGpio::enable());
	CHECK((GameAudioGpio::Sim::vicEnabledMask() & (1u << TimerIrqNumber)) != 0);
	GameAudioGpio::disable();
	CHECK(GameAudioGpio::Sim::vicEnabledMask() == vic_without);
}

static void testEnableFailsWhenNoSlotFree()
{
	// All sixteen PL190 vector slots taken by the OS: enable() must fail
	// cleanly and leave the controller exactly as it found it.
	using namespace GpioAudioHw;

	GameAudioGpio::Sim::reset();
	for(uint32_t slot = 0; slot < VicVectorSlots; ++slot)
		GameAudioGpio::Sim::write(VicBase + VicVectorCtrl0 + 4 * slot, VicVectorCtrlEnable | (slot + 2));

	const uint32_t mask = GameAudioGpio::Sim::vicEnabledMask();
	CHECK(!GameAudioGpio::enable());
	CHECK(!GameAudioGpio::active());
	CHECK(GameAudioGpio::lastError() != nullptr);
	CHECK(GameAudioGpio::Sim::vicEnabledMask() == mask);
	CHECK(GameAudioGpio::Sim::vicFiqSelect() == 0);
	CHECK(GameAudioGpio::Sim::badVicWrites() == 0);
	CHECK(GameAudioGpio::Sim::badPeripheralWrites() == 0);
	CHECK(!GameAudioGpio::Sim::isrSlotClaimed());
}

static void testInterruptBodyRunsAndAcknowledge()
{
	// End-to-end through the same dispatch rules the real PL190 has: the armed
	// slot serves the timer when its interrupt is asserted and unmasked, and the
	// service routine signals end of interrupt on VICVECTADDR.
	using namespace GpioAudioHw;

	GameAudioGpio::Sim::reset();
	CHECK(GameAudioGpio::enable());

	GameAudioGpio::Sim::advanceBitTicks(100);
	CHECK(GameAudioGpio::Sim::bitsEmitted() == 100);
	CHECK(GameAudioGpio::Sim::endOfInterruptWrites() > 0);

	// Mask the timer's bit at the controller: the dispatcher must stop calling
	// the body, so the pin stalls even though the counter keeps raising.
	GameAudioGpio::Sim::write(VicBase + VicIntDisable, 1u << TimerIrqNumber);
	const uint32_t eoi = GameAudioGpio::Sim::endOfInterruptWrites();
	const uint32_t bits_before = GameAudioGpio::Sim::bitsEmitted();
	GameAudioGpio::Sim::advanceBitTicks(50);
	CHECK(GameAudioGpio::Sim::bitsEmitted() == bits_before); // stalled
	CHECK(GameAudioGpio::Sim::endOfInterruptWrites() == eoi); // no service, no ack
	CHECK(GameAudioGpio::Sim::timerIrqs() > 0); // the counter still ran
	CHECK(GameAudioGpio::Sim::read(TimerBase + TimerIntStatus) != 0); // level, still asserted

	// Unmasked again, service resumes from where it stopped: fifty fresh bits
	// plus the one catch-up bit for the interrupt that stayed asserted (level
	// triggered) but unserved through the stall.
	GameAudioGpio::Sim::write(VicBase + VicIntEnable, 1u << TimerIrqNumber);
	GameAudioGpio::Sim::advanceBitTicks(50);
	CHECK(GameAudioGpio::Sim::bitsEmitted() == bits_before + 51);

	// And then exactly one bit per bit period again.
	const uint32_t resumed = GameAudioGpio::Sim::bitsEmitted();
	GameAudioGpio::Sim::advanceBitTicks(50);
	CHECK(GameAudioGpio::Sim::bitsEmitted() == resumed + 50);

	GameAudioGpio::disable();
}

static void testSweepRuns()
{
	GameAudioGpio::Sim::reset();

	const int result = GameAudioGpio::testSweep(100);
	CHECK(result == 0); // the ring drained with no underruns
	CHECK(!GameAudioGpio::active()); // the sweep always tears down after itself
	CHECK(GameAudioGpio::Sim::pinTransitions() > 0);
	CHECK(GameAudioGpio::Sim::badPeripheralWrites() == 0);
	CHECK(GameAudioGpio::Sim::badVicWrites() == 0);
	CHECK(GameAudioGpio::Sim::vicFiqSelect() == 0);
}

int main()
{
	printf("audio_gpio_test\n");

	testRegisterSetup();
	testSilenceIsAlternatingBits();
	testSamplesAdvanceAtTheMixerRate();
	testModulatorDrivesThePin();
	testTeardownRestoresEverything();
	testEnableFailsWhenNoSlotFree();
	testInterruptBodyRunsAndAcknowledge();
	testSweepRuns();

	printf("%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
