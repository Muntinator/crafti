#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

/**
 * Output backend selection.
 *
 *  - Desktop builds use SDL 1.2's audio callback (development/testing sink).
 *    Define CRAFTI_NO_SDL to link the engine into plain host tests.
 *  - Calculator builds have no built-in sound device. The only sink is the
 *    dock-connector UART transmitter backend (physical pin 4), which is strictly
 *    opt-in because it takes over the interrupt vector and the UART.
 */
namespace GameAudioOutput
{
	enum Backend
	{
		BackendNone = 0,
		BackendSdl,
		BackendUartTx
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

	/** Human readable backend status for the settings and test screens. */
	const char *status();

	/** Game loop: tops up the ring of backends whose clock is external. */
	void pump();

	void lock();
	void unlock();
}

#endif // AUDIO_OUTPUT_H
