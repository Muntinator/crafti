// Host tests for the UART Tx output backend.
//
// The backend writes real TI-Nspire registers, so it cannot run on a host as
// written. What it *can* do is run the identical code against the simulated
// register file in audio_nspire_tx.cpp: the interrupt body, the register setup
// and the teardown are the same instructions either way. Only the electrical
// behaviour of dock pin 4 is left unverified.

#include "audio_manager.h"
#include "audio_nspire_tx.h"
#include "audio_tx_hw.h"

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
	using namespace UartTxHw;

	GameAudioTx::Sim::reset();
	GameAudio::initialize();

	CHECK(GameAudioTx::supported());
	CHECK(!GameAudioTx::active());
	CHECK(GameAudioTx::enable());
	CHECK(GameAudioTx::active());

	// The baud divisors are 22.5 MHz / (16 * 80000 baud), to the last 1/64th.
	CHECK(Ibrd == 17);
	CHECK(Fbrd == 37);
	CHECK(GameAudioTx::Sim::read(UartBase + UartIbrd) == Ibrd);
	CHECK(GameAudioTx::Sim::read(UartBase + UartFbrd) == Fbrd);

	// 8N1 with the FIFO on, transmitter up, receiver left alone.
	const uint32_t lcr = GameAudioTx::Sim::read(UartBase + UartLcrH);
	CHECK((lcr & LineFifoEnable) != 0);
	CHECK((lcr & LineEightDataBits) == LineEightDataBits);
	const uint32_t cr = GameAudioTx::Sim::read(UartBase + UartCr);
	CHECK((cr & ControlUartEnable) != 0);
	CHECK((cr & ControlTxEnable) != 0);

	// The power gate is open and only the TX interrupt is unmasked.
	CHECK((GameAudioTx::Sim::read(PowerBase + PowerPeripheralDisable) & PowerUartBusDisable) == 0);
	CHECK(GameAudioTx::Sim::read(UartBase + UartImsc) == InterruptTx);

	// Interrupts: every other source masked, only IRQ 1 unmasked.
	CHECK(GameAudioTx::Sim::vicEnabledMask() == (1u << UartIrqNumber));

	// The IRQ handler branch was installed somewhere reachable.
	CHECK(GameAudioTx::Sim::vectorInstalled());
	CHECK(GameAudioTx::vectorTable() != GameAudioTx::VectorNone);

	CHECK(GameAudioTx::carrierHz() == CarrierHz);
	CHECK(CarrierHz == MixerRateHz * OversamplingRatio);
	CHECK(MixerRateHz == 8000);

	GameAudioTx::disable();
}

static void testBytesDriveTheClock()
{
	// The whole point of the backend: the bytes on the wire, and therefore the
	// samples consumed, are paced by the UART's own baud clock rather than by how
	// often, or from where, the ring is pumped.
	GameAudioTx::Sim::reset();
	CHECK(GameAudioTx::enable());

	GameAudio::play(GameAudio::EventMenuSelect);
	GameAudioTx::pump();

	// Enable primed the FIFO; none of that priming came from the ring.
	CHECK(GameAudioTx::Sim::pcmFramesConsumed() == 0);
	// And nothing has reached the wire until time passes.
	CHECK(GameAudioTx::Sim::bytesTransmitted() == 0);

	GameAudioTx::Sim::advanceCarrierTicks(100);
	CHECK(GameAudioTx::Sim::bytesTransmitted() == 100);

	// The FIFO is refilled in bursts of eight, so any hundred-byte window holds
	// twelve or thirteen refills depending on the phase it starts in.
	const uint32_t before = GameAudioTx::Sim::pcmFramesConsumed();
	CHECK(before >= 90 && before <= 110);

	GameAudioTx::Sim::advanceCarrierTicks(100);
	const uint32_t after = GameAudioTx::Sim::pcmFramesConsumed();
	CHECK(after - before >= 90 && after - before <= 110);

	GameAudioTx::disable();
}

static void testSilenceHasNoIdleTone()
{
	// Digital silence must produce a perfectly alternating bit stream: a first
	// order sigma-delta whose two feedback steps are the same size holds its
	// accumulator at the centre of its range, so every idle byte is 0x55 and the
	// wire has the most transitions a byte-paced 8N1 frame can have. An asymmetric
	// modulator (the high step one smaller than the low step) instead gains half an
	// LSB per bit, drifts for about a second and then slips -- an audible thump at
	// the reset. Over two seconds of silence the drift would show up as a lost
	// transition and an unbalanced high/low count, so this pins the symmetry.
	GameAudioTx::Sim::reset();
	GameAudio::initialize();
	CHECK(GameAudioTx::enable());

	// No voices are playing, so the ring stays empty and every frame is silence.
	GameAudioTx::Sim::advanceCarrierTicks(16000); // 16000 bytes = 2 seconds

	const uint32_t bytes = GameAudioTx::Sim::bytesTransmitted();
	CHECK(bytes == 16000);
	CHECK(GameAudioTx::Sim::lineHighCount() == bytes * 5);
	CHECK(GameAudioTx::Sim::lineLowCount() == bytes * 5);
	CHECK(GameAudioTx::Sim::lineTransitions() == bytes * 10 - 1);

	GameAudioTx::disable();
}

static void testModulatorDrivesTheLine()
{
	GameAudioTx::Sim::reset();
	CHECK(GameAudioTx::enable());

	GameAudio::play(GameAudio::EventMenuSelect);
	GameAudioTx::pump();

	CHECK(GameAudioTx::Sim::lineTransitions() == 0);
	GameAudioTx::Sim::advanceCarrierTicks(2000);

	// A 1-bit output can only be meaningful if the line actually toggles, and a
	// first-order modulator must use both levels rather than sitting at one.
	CHECK(GameAudioTx::Sim::lineTransitions() > 100);
	CHECK(GameAudioTx::Sim::lineHighCount() > 0);
	CHECK(GameAudioTx::Sim::lineLowCount() > 0);

	GameAudioTx::disable();
}

static void testWaveformRoundTrip()
{
	// Push a known 440 Hz square wave through the modulator, decode the framed
	// byte stream the way an attached stage would, and confirm the tone survived.
	GameAudioTx::Sim::reset();
	CHECK(GameAudioTx::enable());

	const char *path = "build/uart_tx_waveform.wav";
	CHECK(GameAudioTx::Sim::writeWav(path, 500));

	FILE *file = fopen(path, "rb");
	CHECK(file != nullptr);
	if(file == nullptr)
	{
		GameAudioTx::disable();
		return;
	}

	uint8_t header[44];
	const size_t got = fread(header, 1, sizeof(header), file);
	CHECK(got == sizeof(header));
	CHECK(memcmp(header, "RIFF", 4) == 0);
	CHECK(memcmp(header + 8, "WAVE", 4) == 0);
	CHECK(read16(header + 22) == 1); // mono
	CHECK(read32(header + 24) == UartTxHw::MixerRateHz);

	const uint32_t data_bytes = read32(header + 40);
	CHECK(data_bytes == 500u * UartTxHw::MixerRateHz / 1000u * 2);

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

	GameAudioTx::disable();
}

static void testPolledToneOnlyTouchesTheUart()
{
	using namespace UartTxHw;

	GameAudioTx::Sim::reset();

	const uint32_t cr_before = GameAudioTx::Sim::read(UartBase + UartCr);
	const uint32_t lcr_before = GameAudioTx::Sim::read(UartBase + UartLcrH);
	const uint32_t ibrd_before = GameAudioTx::Sim::read(UartBase + UartIbrd);
	const uint32_t fbrd_before = GameAudioTx::Sim::read(UartBase + UartFbrd);
	const uint32_t vic_before = GameAudioTx::Sim::vicEnabledMask();
	const uint32_t transitions_before = GameAudioTx::Sim::lineTransitions();

	const int periods = GameAudioTx::testPolled(1000, 50);
	CHECK(periods == 50);
	CHECK(GameAudioTx::Sim::lineTransitions() > transitions_before);

	// No interrupts installed, and every register back where it started.
	CHECK(GameAudioTx::Sim::vicEnabledMask() == vic_before);
	CHECK(GameAudioTx::Sim::read(UartBase + UartCr) == cr_before);
	CHECK(GameAudioTx::Sim::read(UartBase + UartLcrH) == lcr_before);
	CHECK(GameAudioTx::Sim::read(UartBase + UartIbrd) == ibrd_before);
	CHECK(GameAudioTx::Sim::read(UartBase + UartFbrd) == fbrd_before);
	CHECK(!GameAudioTx::Sim::vectorInstalled());
	CHECK(!GameAudioTx::active());

	// Bounds are clamped rather than trusted.
	CHECK(GameAudioTx::testPolled(0, 100) == -1);
	CHECK(GameAudioTx::testPolled(1000, 0) == -1);
}

static void testTeardownRestoresEverything()
{
	using namespace UartTxHw;

	GameAudioTx::Sim::reset();

	// Remember the boot state the simulation seeds.
	const uint32_t cr = GameAudioTx::Sim::read(UartBase + UartCr);
	const uint32_t lcr = GameAudioTx::Sim::read(UartBase + UartLcrH);
	const uint32_t ibrd = GameAudioTx::Sim::read(UartBase + UartIbrd);
	const uint32_t fbrd = GameAudioTx::Sim::read(UartBase + UartFbrd);
	const uint32_t ifls = GameAudioTx::Sim::read(UartBase + UartIfLs);
	const uint32_t imsc = GameAudioTx::Sim::read(UartBase + UartImsc);
	const uint32_t power = GameAudioTx::Sim::read(PowerBase + PowerPeripheralDisable);
	const uint32_t vic = GameAudioTx::Sim::vicEnabledMask();

	CHECK(GameAudioTx::enable());
	CHECK(GameAudioTx::Sim::vectorInstalled());
	GameAudioTx::disable();

	CHECK(!GameAudioTx::active());
	CHECK(GameAudioTx::Sim::read(UartBase + UartCr) == cr);
	CHECK(GameAudioTx::Sim::read(UartBase + UartLcrH) == lcr);
	CHECK(GameAudioTx::Sim::read(UartBase + UartIbrd) == ibrd);
	CHECK(GameAudioTx::Sim::read(UartBase + UartFbrd) == fbrd);
	CHECK(GameAudioTx::Sim::read(UartBase + UartIfLs) == ifls);
	CHECK(GameAudioTx::Sim::read(UartBase + UartImsc) == imsc);
	CHECK(GameAudioTx::Sim::read(PowerBase + PowerPeripheralDisable) == power);
	CHECK(GameAudioTx::Sim::vicEnabledMask() == vic);
	CHECK(!GameAudioTx::Sim::vectorInstalled()); // the handler branch was removed

	// Disabling twice is harmless.
	GameAudioTx::disable();
}

static void testSweepRuns()
{
	GameAudioTx::Sim::reset();

	const int result = GameAudioTx::testSweep(100);
	CHECK(result >= 0);
	CHECK(!GameAudioTx::active()); // the sweep always tears down after itself
	CHECK(GameAudioTx::Sim::lineTransitions() > 0);
}

int main()
{
	printf("audio_tx_test\n");

	testRegisterSetup();
	testBytesDriveTheClock();
	testSilenceHasNoIdleTone();
	testModulatorDrivesTheLine();
	testWaveformRoundTrip();
	testPolledToneOnlyTouchesTheUart();
	testTeardownRestoresEverything();
	testSweepRuns();

	printf("%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
