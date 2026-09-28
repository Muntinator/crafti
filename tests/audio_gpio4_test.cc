// Host tests for the GPIO4 output backend.
//
// The backend writes real TI-Nspire registers, so it cannot run on a host as
// written. What it *can* do is run the identical code against the simulated
// register file in audio_nspire_gpio4.cpp: the interrupt body, the register
// setup and the teardown are the same instructions either way. Only the
// electrical behaviour of dock pin 6 is left unverified.

#include "audio_gpio4_hw.h"
#include "audio_manager.h"
#include "audio_nspire_gpio4.h"

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
	uint32_t read16(const uint8_t *data)
	{
		return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8);
	}

	uint32_t read32(const uint8_t *data)
	{
		return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)
			| (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
	}
}

static void testRegisterSetup()
{
	using namespace Gpio4Hw;

	GameAudioGpio4::Sim::reset();
	GameAudio::initialize();

	CHECK(GameAudioGpio4::supported());
	CHECK(!GameAudioGpio4::active());
	CHECK(GameAudioGpio4::enable());
	CHECK(GameAudioGpio4::active());

	// Timer: periodic, 32-bit, interrupt enabled, loaded with the carrier period.
	const uint32_t control = GameAudioGpio4::Sim::read(TimerBase + Timer1Control);
	CHECK((control & TimerControlEnable) != 0);
	CHECK((control & TimerControlPeriodic) != 0);
	CHECK((control & TimerControlInterruptEnable) != 0);
	CHECK((control & TimerControl32Bit) != 0);
	CHECK(GameAudioGpio4::Sim::read(TimerBase + Timer1Load) == TimerReload);
	CHECK(GameAudioGpio4::Sim::read(TimerBase + TimerClockSelect) == TimerClockSelect10MHz);

	// Interrupts: every other source masked, only the fast timer unmasked.
	CHECK(GameAudioGpio4::Sim::vicEnabledMask() == (1u << FastTimerIrqNumber));

	// GPIO4 is an output and parked low.
	CHECK((GameAudioGpio4::Sim::read(Gpio4DirectionAddress) & Gpio4Mask) == 0);
	CHECK((GameAudioGpio4::Sim::read(Gpio4OutputAddress) & Gpio4Mask) == 0);

	// The IRQ handler branch was installed somewhere reachable.
	CHECK(GameAudioGpio4::Sim::vectorInstalled());
	CHECK(GameAudioGpio4::vectorTable() != GameAudioGpio4::VectorNone);

	CHECK(GameAudioGpio4::carrierHz() == CarrierHz);
	CHECK(CarrierHz == MixerRateHz * OversamplingRatio);
}

static void testClockFollowsCarrierTicks()
{
	// The whole point of the backend: sample consumption is paced by the timer,
	// not by how often, or from where, the buffer is pumped.
	GameAudioGpio4::Sim::reset();
	CHECK(GameAudioGpio4::enable());

	GameAudio::play(GameAudio::EventMenuSelect);
	GameAudioGpio4::pump();

	// Priming the ring must not consume anything by itself.
	CHECK(GameAudioGpio4::Sim::pcmFramesConsumed() == 0);

	GameAudioGpio4::Sim::advanceCarrierTicks(100);
	const uint32_t before = GameAudioGpio4::Sim::pcmFramesConsumed();
	CHECK(before == 100 / Gpio4Hw::OversamplingRatio);

	GameAudioGpio4::Sim::advanceCarrierTicks(1000);
	const uint32_t after1000 = GameAudioGpio4::Sim::pcmFramesConsumed();
	CHECK(after1000 - before == 1000 / Gpio4Hw::OversamplingRatio);

	// Same number of ticks split into many calls must give the same result.
	for(int i = 0; i < 20; ++i)
		GameAudioGpio4::Sim::advanceCarrierTicks(50);
	const uint32_t afterSplit = GameAudioGpio4::Sim::pcmFramesConsumed();
	CHECK(afterSplit - before == (1000 + 20 * 50) / Gpio4Hw::OversamplingRatio);

	GameAudioGpio4::disable();
}

static void testModulatorDrivesThePin()
{
	GameAudioGpio4::Sim::reset();
	CHECK(GameAudioGpio4::enable());

	GameAudio::play(GameAudio::EventMenuSelect);
	GameAudioGpio4::pump();

	CHECK(GameAudioGpio4::Sim::pinTransitions() == 0);
	GameAudioGpio4::Sim::advanceCarrierTicks(2000);

	// A 1-bit output can only be meaningful if the pin actually toggles, and a
	// first-order modulator must use both levels rather than sitting at one.
	CHECK(GameAudioGpio4::Sim::pinTransitions() > 100);
	CHECK(GameAudioGpio4::Sim::pinHighCount() > 0);
	CHECK(GameAudioGpio4::Sim::pinLowCount() > 0);

	GameAudioGpio4::disable();
}

static void testWaveformRoundTrip()
{
	// Push a known 440 Hz square wave through the modulator, decode the pin
	// stream the way an attached stage would, and confirm the tone survived.
	GameAudioGpio4::Sim::reset();
	CHECK(GameAudioGpio4::enable());

	const char *path = "build/gpio4_waveform.wav";
	CHECK(GameAudioGpio4::Sim::writeWav(path, 500));

	FILE *file = fopen(path, "rb");
	CHECK(file != nullptr);
	if(file == nullptr)
	{
		GameAudioGpio4::disable();
		return;
	}

	uint8_t header[44];
	const size_t got = fread(header, 1, sizeof(header), file);
	CHECK(got == sizeof(header));
	CHECK(memcmp(header, "RIFF", 4) == 0);
	CHECK(memcmp(header + 8, "WAVE", 4) == 0);
	CHECK(read16(header + 22) == 1); // mono
	CHECK(read32(header + 24) == Gpio4Hw::MixerRateHz);

	const uint32_t data_bytes = read32(header + 40);
	CHECK(data_bytes == 500u * Gpio4Hw::MixerRateHz / 1000u * 2);

	int crossings = 0;
	int previous = 0;
	int peak = 0;
	for(uint32_t i = 0; i + 1 < data_bytes; i += 2)
	{
		uint8_t raw[2];
		if(fread(raw, 1, 2, file) != 2)
			break;
		const int sample = static_cast<int16_t>(read16(raw));
		if(sample > peak)
			peak = sample;
		if(sample < -peak)
			peak = -sample;
		const int sign = sample >= 0 ? 1 : -1;
		if(previous != 0 && sign != previous)
			++crossings;
		previous = sign;
	}
	fclose(file);

	// 440 Hz for half a second is about 440 zero crossings.
	CHECK(crossings > 350 && crossings < 530);
	CHECK(peak > 3000); // the reconstructed signal is not a flat line

	GameAudioGpio4::disable();
}

static void testPolledToneOnlyTouchesThePin()
{
	GameAudioGpio4::Sim::reset();

	const uint32_t direction_before = GameAudioGpio4::Sim::read(Gpio4Hw::Gpio4DirectionAddress);
	const uint32_t transitions_before = GameAudioGpio4::Sim::pinTransitions();
	const uint32_t mask_before = GameAudioGpio4::Sim::vicEnabledMask();

	const int periods = GameAudioGpio4::testPolled(1000, 50);
	CHECK(periods == 50);
	CHECK(GameAudioGpio4::Sim::pinTransitions() > transitions_before);

	// No timer, no interrupts, and every register back where it started.
	CHECK(GameAudioGpio4::Sim::vicEnabledMask() == mask_before);
	CHECK(GameAudioGpio4::Sim::read(Gpio4Hw::TimerBase + Gpio4Hw::Timer1Control) == 0);
	CHECK(GameAudioGpio4::Sim::read(Gpio4Hw::Gpio4DirectionAddress) == direction_before);
	CHECK(!GameAudioGpio4::active());

	// Bounds are clamped rather than trusted.
	CHECK(GameAudioGpio4::testPolled(0, 100) == -1);
	CHECK(GameAudioGpio4::testPolled(1000, 0) == -1);
}

static void testTeardownRestoresEverything()
{
	GameAudioGpio4::Sim::reset();

	// Remember the boot state the simulation seeds.
	const uint32_t gpio_direction = GameAudioGpio4::Sim::read(Gpio4Hw::Gpio4DirectionAddress);
	const uint32_t gpio_output = GameAudioGpio4::Sim::read(Gpio4Hw::Gpio4OutputAddress);
	const uint32_t vic_mask = GameAudioGpio4::Sim::vicEnabledMask();
	const uint32_t timer_control = GameAudioGpio4::Sim::read(Gpio4Hw::TimerBase + Gpio4Hw::Timer1Control);
	const uint32_t timer_load = GameAudioGpio4::Sim::read(Gpio4Hw::TimerBase + Gpio4Hw::Timer1Load);

	CHECK(GameAudioGpio4::enable());
	CHECK(GameAudioGpio4::Sim::vectorInstalled());
	GameAudioGpio4::disable();

	CHECK(!GameAudioGpio4::active());
	CHECK(GameAudioGpio4::Sim::read(Gpio4Hw::Gpio4DirectionAddress) == gpio_direction);
	CHECK(GameAudioGpio4::Sim::read(Gpio4Hw::Gpio4OutputAddress) == gpio_output);
	CHECK(GameAudioGpio4::Sim::vicEnabledMask() == vic_mask);
	CHECK(GameAudioGpio4::Sim::read(Gpio4Hw::TimerBase + Gpio4Hw::Timer1Control) == timer_control);
	CHECK(GameAudioGpio4::Sim::read(Gpio4Hw::TimerBase + Gpio4Hw::Timer1Load) == timer_load);
	CHECK(!GameAudioGpio4::Sim::vectorInstalled()); // the handler branch was removed

	// Disabling twice is harmless.
	GameAudioGpio4::disable();
}

static void testTimerSweepRuns()
{
	GameAudioGpio4::Sim::reset();

	const int result = GameAudioGpio4::testTimer(100);
	CHECK(result >= 0);
	CHECK(!GameAudioGpio4::active()); // the sweep always tears down after itself
	CHECK(GameAudioGpio4::Sim::pinTransitions() > 0);
}

int main()
{
	printf("audio_gpio4_test\n");

	testRegisterSetup();
	testClockFollowsCarrierTicks();
	testModulatorDrivesThePin();
	testWaveformRoundTrip();
	testPolledToneOnlyTouchesThePin();
	testTeardownRestoresEverything();
	testTimerSweepRuns();

	printf("%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
