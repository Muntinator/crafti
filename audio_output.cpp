#include "audio_output.h"

#include "audio_manager.h"
#include "audio_nspire_gpio4.h"

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
		// The calculator has no built-in output. GPIO4 stays off until the user
		// enables it in the audio settings or runs the audio test.
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
		disableGpio4();

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

	bool enableGpio4()
	{
#ifdef _TINSPIRE
		if(active_backend == BackendGpio4)
			return true;
		if(GameAudioGpio4::enable())
		{
			active_backend = BackendGpio4;
			return true;
		}
#endif
		return false;
	}

	void disableGpio4()
	{
#ifdef _TINSPIRE
		if(active_backend == BackendGpio4)
		{
			GameAudioGpio4::disable();
			active_backend = BackendNone;
		}
#endif
	}

	bool gpio4Active()
	{
		return active_backend == BackendGpio4;
	}

	Backend backend() { return active_backend; }

	const char *backendName()
	{
		switch(active_backend)
		{
		case BackendSdl:
			return "SDL";
		case BackendGpio4:
			return "GPIO4";
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
		case BackendGpio4:
			return GameAudioGpio4::status();
		default:
#ifdef _TINSPIRE
			return GameAudioGpio4::active() ? GameAudioGpio4::status() : "Calculator audio off (GPIO4 disabled)";
#else
			return "No desktop audio device";
#endif
		}
	}

	void pump()
	{
#ifdef _TINSPIRE
		if(active_backend == BackendGpio4)
			GameAudioGpio4::pump();
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
