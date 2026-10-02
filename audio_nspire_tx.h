#ifndef AUDIO_NSPIRE_TX_H
#define AUDIO_NSPIRE_TX_H

#include <stdint.h>

/**
 * 1-bit audio output on the original TI-Nspire CX dock connector pin 4 (UART
 * Tx).
 *
 * Pin 4 is the one pin on the dock that is a dedicated output and is not shared
 * with anything else: pin 6 (GPIO4) doubles as USB Data+ when a Navigator cradle
 * is attached, and pins 3/17/18 are inputs or general-purpose lines. The PL011
 * transmitter also paces the bits itself, so the sample clock comes from the
 * UART's baud generator instead of a CPU timer the program has to hijack.
 *
 * The mechanism is the classic calculator "beeper", moved to the UART: a
 * first-order sigma-delta modulator turns each 16-bit mixer sample into eight
 * output bits, which are packed into one UART byte. At 80 kBd a ten-bit frame
 * takes 125 us, so exactly one byte is shifted out per 8 kHz mixer sample and
 * the UART hardware holds the timing. The transmitter's FIFO-empty interrupt
 * (IRQ 1) refills the FIFO from a ring of 16-bit samples, so no timer, no
 * bit-banging and no per-sample interrupt is needed.
 *
 * Like the timer backend it replaces, this needs the interrupt vector, so it is
 * strictly opt-in: enable() snapshots and disable() restores every register it
 * touches. The identical code runs on a development host against a simulated
 * register file, so the register sequence, the modulator and the transmitted
 * bitstream can be verified without a calculator. Only the electrical result on
 * pin 4 is unverified.
 */
namespace GameAudioTx
{
	enum VectorTable
	{
		VectorNone = 0,
		VectorLow,
		VectorHigh
	};

	/** True when this build/platform can drive the UART at all. */
	bool supported();

	/** Installs the transmitter interrupt and starts the 1-bit stream. Opt-in. */
	bool enable();

	/** Stops the stream and restores every touched register. */
	void disable();

	bool active();
	/** Game loop: mixes into the PCM ring. Never called from the interrupt. */
	void pump();

	const char *status();
	const char *lastError();

	VectorTable vectorTable();
	uint32_t carrierHz();
	uint32_t ringUnderruns();

	/** Blocking square-wave diagnostic that never touches interrupts. */
	int testPolled(uint32_t frequency_hz, uint32_t duration_ms);

	/** Interrupt driven diagnostic: plays a generated sweep through the modulator. */
	int testSweep(uint32_t duration_ms);

#ifndef _TINSPIRE
	/** Host-side simulation of the same register file, FIFO and wire. */
	namespace Sim
	{
		void reset();
		uint32_t read(uint32_t address);
		void write(uint32_t address, uint32_t value);
		/** Runs the simulated UART for `ticks` byte-periods (125 us each). */
		void advanceCarrierTicks(uint32_t ticks);
		/** Number of times the transmitted line level changed. */
		uint32_t lineTransitions();
		uint32_t lineHighCount();
		uint32_t lineLowCount();
		uint32_t carrierTickCount();
		/** Bytes the simulated FIFO has handed to the wire. */
		uint32_t bytesTransmitted();
		/** True when the simulated handler slot holds the installed hook stub. */
		bool vectorInstalled();
		/** True when the handler slot was put back the way it was. */
		bool vectorRestored();
		uint32_t vicEnabledMask();
		uint32_t pcmFramesConsumed();
		/** Decodes the transmitted byte stream back to PCM and writes a RIFF WAVE. */
		bool writeWav(const char *path, uint32_t milliseconds);
	}
#endif
}

#endif // AUDIO_NSPIRE_TX_H
