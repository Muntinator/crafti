#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

/**
 * Output backend selection.
 *
 *  - Desktop builds use SDL 1.2's audio callback (development/testing sink).
 *    Define CRAFTI_NO_SDL to link the engine into plain host tests.
 *  - Calculator builds have no built-in sound device. The only sink is the
 *    dock-connector GPIO4 backend, which is strictly opt-in because it takes
 *    over the interrupt vector and the fast timer.
 */
namespace GameAudioOutput
{
	enum Backend
	{
		BackendNone = 0,
		BackendSdl,
		BackendGpio4
	};

	bool initialize();
	void shutdown();
	bool available();
	Backend backend();
	const char *backendName();

	/** Opt-in GPIO4 output. Returns false when it cannot be brought up. */
	bool enableGpio4();
	void disableGpio4();
	bool gpio4Active();

	/** Human readable backend status for the settings and test screens. */
	const char *status();

	/** Game loop: tops up the ring of backends whose clock is external. */
	void pump();

	void lock();
	void unlock();
}

#endif // AUDIO_OUTPUT_H
