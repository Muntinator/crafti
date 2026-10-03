#include "audio_output.h"

#include "audio_manager.h"
#include "audio_nspire_gpio.h"
#include "audio_nspire_tx.h"

#ifdef _TINSPIRE
#include <libndls.h>
#endif

#if !defined(_TINSPIRE) && !defined(CRAFTI_NO_SDL)
#include <SDL/SDL.h>
#define CRAFTI_HAS_SDL 1
#endif

#include <stdint.h>
#include <string.h>

namespace
{
	GameAudioOutput::Backend active_backend = GameAudioOutput::BackendNone;

#ifdef CRAFTI_HAS_SDL
	bool sdl_open = false;

	void sdlAudioCallback(void *, Uint8 *stream, int length)
	{
		if(stream == nullptr || length <= 0)
			return;

		static int16_t samples[256];
		size_t remaining = static_cast<size_t>(length);
		size_t written = 0;

		while(remaining >= sizeof(int16_t))
		{
			size_t frames = remaining / sizeof(int16_t);
			if(frames > sizeof(samples) / sizeof(samples[0]))
				frames = sizeof(samples) / sizeof(samples[0]);

			GameAudio::mixMono(samples, frames);

			const size_t bytes = frames * sizeof(int16_t);
			memcpy(stream + written, samples, bytes);
			written += bytes;
			remaining -= bytes;
		}

		if(remaining != 0)
			stream[written++] = 0;
		if(written < static_cast<size_t>(length))
			memset(stream + written, 0, static_cast<size_t>(length) - written);
	}
#endif
}

namespace GameAudioOutput
{
	bool initialize()
	{
#ifdef _TINSPIRE
		// The calculator has no built-in output. The dock UART stays off until
		// the user enables it in the audio settings or runs the audio test.
		return false;
#elif defined(CRAFTI_HAS_SDL)
		if(sdl_open)
			return true;

		SDL_AudioSpec desired = {};
		desired.freq = GameAudio::MixerSampleRate;
		desired.format = AUDIO_S16SYS;
		desired.channels = 1;
		desired.samples = 256;
		desired.callback = sdlAudioCallback;
		desired.userdata = nullptr;

		if(SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
			return false;
		if(SDL_OpenAudio(&desired, nullptr) < 0)
		{
			SDL_QuitSubSystem(SDL_INIT_AUDIO);
			return false;
		}

		sdl_open = true;
		active_backend = BackendSdl;
		SDL_PauseAudio(0);
		return true;
#else
		return false;
#endif
	}

	void shutdown()
	{
		disableUartTx();
		disableGpio();

#ifdef CRAFTI_HAS_SDL
		if(sdl_open)
		{
			SDL_PauseAudio(1);
			SDL_CloseAudio();
			SDL_QuitSubSystem(SDL_INIT_AUDIO);
			sdl_open = false;
		}
#endif
		active_backend = BackendNone;
	}

	bool enableUartTx()
	{
#ifdef _TINSPIRE
		if(active_backend == BackendUartTx)
			return true;
		disableGpio(); // the two opt-in backends cannot share the mixer
		if(GameAudioTx::enable())
		{
			active_backend = BackendUartTx;
			return true;
		}
#endif
		return false;
	}

	bool enableGpio(bool buzzer)
	{
#ifdef _TINSPIRE
		if(active_backend == BackendGpio && GameAudioGpio::buzzerDrive() == buzzer)
			return true;
		disableUartTx(); // the two opt-in backends cannot share the mixer
		if(active_backend == BackendGpio)
		{
			GameAudioGpio::disable(); // switching drive: rebuilt below
			active_backend = BackendNone;
		}
		GameAudioGpio::setBuzzerDrive(buzzer);
		if(GameAudioGpio::enable())
		{
			active_backend = BackendGpio;
			return true;
		}
#endif
		return false;
	}

	void disableGpio()
	{
#ifdef _TINSPIRE
		if(active_backend == BackendGpio)
		{
			GameAudioGpio::disable();
			active_backend = BackendNone;
		}
#endif
	}

	bool gpioActive()
	{
		return active_backend == BackendGpio;
	}

	void disableUartTx()
	{
#ifdef _TINSPIRE
		if(active_backend == BackendUartTx)
		{
			GameAudioTx::disable();
			active_backend = BackendNone;
		}
#endif
	}

	bool uartTxActive()
	{
		return active_backend == BackendUartTx;
	}

	Backend backend() { return active_backend; }

	const char *backendName()
	{
		switch(active_backend)
		{
		case BackendSdl:
			return "SDL";
		case BackendUartTx:
			return "UART";
		case BackendGpio:
			return GameAudioGpio::buzzerDrive() ? "GPIO buzzer" : "GPIO 22";
		default:
			return "none";
		}
	}

	const char *status()
	{
		switch(active_backend)
		{
		case BackendSdl:
			return "SDL audio callback active";
		case BackendUartTx:
			return GameAudioTx::status();
		case BackendGpio:
			return GameAudioGpio::status();
		default:
#ifdef _TINSPIRE
			if(GameAudioGpio::active())
				return GameAudioGpio::status();
			return GameAudioTx::active() ? GameAudioTx::status() : "Calculator audio off (output disabled)";
#else
			return "No desktop audio device";
#endif
		}
	}

	void pump()
	{
#ifdef _TINSPIRE
		if(active_backend == BackendUartTx)
			GameAudioTx::pump();
		else if(active_backend == BackendGpio)
			GameAudioGpio::pump();
#endif
	}

	void lock()
	{
		// Voice state is owned exclusively by the audio clock and mutated only
		// through the lock-free command queue, so no locking is required.
	}

	void unlock()
	{
	}

	bool available()
	{
		return active_backend != BackendNone;
	}
}
