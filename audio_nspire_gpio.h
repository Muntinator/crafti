#ifndef AUDIO_NSPIRE_GPIO_H
#define AUDIO_NSPIRE_GPIO_H

#include <stdint.h>

/**
 * 1-bit audio output on GPIO 22 (dock connector pin 18).
 *
 * This is the fallback for a calculator whose dock pin 4 (UART Tx) is broken:
 * the same first-order sigma-delta stream as the UART backend, but bit-banged
 * on a general-purpose line instead of shifted out by the PL011. GPIO 22 is
 * the one unclaimed output on the CX dock (see audio_gpio_hw.h), with dock
 * pin 5 (GND) as the return.
 *
 * The UART's transmitter paced its own bits; a GPIO line has no clock, so a
 * spare CX timer (block 0x900C0000, IRQ 18 -- the block Ndless does not use
 * for msleep) paces them instead: one interrupt per output bit at 16384 bit/s,
 * a phase accumulator turning that into exactly the 8 kHz mixer rate, and the
 * same symmetric sigma-delta modulator as the UART backend. The far end is the
 * same RC low-pass filter into an amplifier.
 *
 * For a piezoelectric buzzer wired straight to the pin there is no filter and
 * no amplifier: a buzzer is a full-swing tone device, so setBuzzerDrive()
 * switches the modulator to the classic direct drive -- the pin is driven with
 * a square wave whose polarity follows the sample (one-bit hard limiting),
 * silence holds the pin low with no switching at all, and no RC stage is
 * needed. Both drives share the timer, the vector slot and the PCM ring.
 *
 * Like the UART backend this needs an interrupt service routine, so it is
 * strictly opt-in: enable() claims a free PL190 vector slot and disable()
 * gives it back, and every register it touches is snapshotted and restored.
 * The identical code runs on a development host against a simulated register
 * file, so the register sequence, the modulator and the emitted bitstream can
 * be verified without a calculator. Only the electrical result on pin 18 is
 * unverified.
 */
namespace GameAudioGpio
{
	/** True when this build/platform can drive the GPIO block at all. */
	bool supported();

	/** Installs the timer interrupt and starts the 1-bit stream. Opt-in. */
	bool enable();

	/** Stops the stream and restores every touched register. */
	void disable();

	bool active();
	/** Game loop: mixes into the PCM ring. Never called from the interrupt. */
	void pump();

	const char *status();
	const char *lastError();

	/** The claimed PL190 vector slot, or -1 when the output is idle. */
	int vectorSlot();
	uint32_t bitRateHz();
	uint32_t ringUnderruns();

	/**
	 * Selects how the pin is driven. Off (default) is the sigma-delta stream for
	 * an RC filter stage; on is the full-swing square wave for a piezoelectric
	 * buzzer wired directly to the pin. Takes effect at the next output bit.
	 */
	void setBuzzerDrive(bool on);
	bool buzzerDrive();

	/** Interrupt driven diagnostic: plays a generated sweep through the modulator. */
	int testSweep(uint32_t duration_ms);

	/** Interrupt driven diagnostic: a fixed square-wave beep in buzzer drive. */
	int testBuzzerTone(uint32_t frequency_hz, uint32_t duration_ms);

#ifndef _TINSPIRE
	/** Host-side simulation of the same register file, timer and pin. */
	namespace Sim
	{
		void reset();
		uint32_t read(uint32_t address);
		void write(uint32_t address, uint32_t value);
		/** Runs the simulated timer for `ticks` ticks of the 32.768 kHz crystal. */
		void advanceTimerTicks(uint32_t ticks);
		/** Convenience: `bits` output bit periods (two crystal ticks each). */
		void advanceBitTicks(uint32_t bits);

		/** The level the service routine last drove GPIO 22 to. */
		uint32_t pinLevel();
		/** Number of times the pin level changed. */
		uint32_t pinTransitions();
		uint32_t pinHighCount();
		uint32_t pinLowCount();
		/** Bits the timer interrupt has shifted out of the modulator. */
		uint32_t bitsEmitted();
		/** Timer interrupts raised by the simulated counter. */
		uint32_t timerIrqs();

		/** Index of the vector slot armed for the timer, or -1. */
		int claimedSlot();
		/** True when a vector slot is armed for the service routine. */
		bool isrSlotClaimed();
		/** True when no vector slot is armed for the timer any more. */
		bool isrSlotReleased();
		/** Control register of one PL190 vector slot (to check OS slots). */
		uint32_t slotControl(uint32_t slot);
		uint32_t vicEnabledMask();
		/** The IRQ/FIQ routing register: nonzero means sources were rerouted. */
		uint32_t vicFiqSelect();
		/** End-of-interrupt writes to VICVECTADDR seen. */
		uint32_t endOfInterruptWrites();
		/** Writes to controller offsets that only exist on the classic machine. */
		uint32_t badVicWrites();
		/**
		 * Writes that would leave the backend's own window: stray addresses, the
		 * wrong timer, or a GPIO port write that changes another line's bits.
		 */
		uint32_t badPeripheralWrites();
		uint32_t pcmFramesConsumed();
	}
#endif
}

#endif // AUDIO_NSPIRE_GPIO_H
