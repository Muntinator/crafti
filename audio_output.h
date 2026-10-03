#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

/**
 * Output backend selection.
 *
 *  - Desktop builds use SDL 1.2's audio callback (development/testing sink).
 *    Define CRAFTI_NO_SDL to link the engine into plain host tests.
 *  - Calculator builds have no built-in sound device. The sinks are the
 *    dock-connector UART transmitter backend (physical pin 4) and the GPIO 22
 *    bit-bang backend (dock pin 18, for calculators whose pin 4 is broken).
 *    Both are strictly opt-in because they take over an interrupt vector.
 */
namespace GameAudioOutput
{
	enum Backend
	{
		BackendNone = 0,
		BackendSdl,
		BackendUartTx,
		BackendGpio
	};

	bool initialize();
	void shutdown();
	bool available();
	Backend backend();
	const char *backendName();

	/** Opt-in UART Tx output. Returns false when it cannot be brought up. */
	bool enableUartTx();
	void disableUartTx();
	bool uartTxActive();

	/**
	 * Opt-in GPIO 22 output. The two opt-in backends are mutually exclusive.
	 * `buzzer` selects the full-swing square-wave drive for a piezoelectric
	 * buzzer wired straight to the pin, instead of the sigma-delta stream that
	 * expects an RC filter stage.
	 */
	bool enableGpio(bool buzzer = false);
	void disableGpio();
	bool gpioActive();

	/** Human readable backend status for the settings and test screens. */
	const char *status();

	/** Game loop: tops up the ring of backends whose clock is external. */
	void pump();

	void lock();
	void unlock();
}

#endif // AUDIO_OUTPUT_H
