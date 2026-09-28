#ifndef AUDIO_NSPIRE_GPIO4_H
#define AUDIO_NSPIRE_GPIO4_H

#include <stdint.h>

/**
 * 1-bit audio output on the original TI-Nspire CX dock connector pin 6 (GPIO4).
 *
 * The mechanism is the classic calculator "beeper": GPIO4 is driven as a digital
 * output and a first-order sigma-delta modulator turns 16-bit mixer samples into
 * a 1-bit pulse stream at the carrier rate. The fast timer's interrupt supplies
 * the sample clock, so playback timing is set by hardware and cannot drift with
 * the frame rate.
 *
 * Because that requires the timer interrupt vector, this backend is strictly
 * opt-in; enable() snapshots and disable() restores every register it touches.
 *
 * The exact same code runs on a development host against a simulated register
 * file, so the register sequence, the modulator and the pin waveform can be
 * verified without a calculator. Only the electrical result is unverified.
 */
namespace GameAudioGpio4
{
	enum VectorTable
	{
		VectorNone = 0,
		VectorLow,
		VectorHigh
	};

	/** True when this build/platform can drive GPIO4 at all. */
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

	VectorTable vectorTable();
	uint32_t carrierHz();
	uint32_t ringUnderruns();

	/** Blocking square-wave diagnostic that never touches timers or interrupts. */
	int testPolled(uint32_t frequency_hz, uint32_t duration_ms);

	/** Timer driven diagnostic: plays a generated sweep through the modulator. */
	int testTimer(uint32_t duration_ms);

#ifndef _TINSPIRE
	/** Host-side simulation of the same register file, timer and pin. */
	namespace Sim
	{
		void reset();
		uint32_t read(uint32_t address);
		void write(uint32_t address, uint32_t value);
		/** Runs the simulated carrier interrupt `ticks` times. */
		void advanceCarrierTicks(uint32_t ticks);
		/** Number of times the simulated GPIO4 level changed. */
		uint32_t pinTransitions();
		/** Highest and lowest pin levels observed. */
		uint32_t pinHighCount();
		uint32_t pinLowCount();
		uint32_t carrierTickCount();
		uint32_t timerReloadValue();
		/** True when the simulated handler slot holds the installed hook stub. */
		bool vectorInstalled();
		/** True when the handler slot was put back the way it was. */
		bool vectorRestored();
		uint32_t vicEnabledMask();
		uint32_t pcmFramesConsumed();
		/** Decodes the pin stream back to PCM and writes a RIFF WAVE file. */
		bool writeWav(const char *path, uint32_t milliseconds);
	}
#endif
}

#endif // AUDIO_NSPIRE_GPIO4_H
