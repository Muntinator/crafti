#include "audiotesttask.h"

#include <stdio.h>

#include "audio_manager.h"
#include "audio_nspire_gpio4.h"
#include "audio_output.h"
#include "font.h"
#include "starttask.h"
#include "texturetools.h"

AudioTestTask audio_test_task;

namespace
{
	const char *const item_labels[AudioTestTask::ITEM_MAX] = {
		"UI click",
		"Footstep (grass)",
		"Break block (stone)",
		"Mob (cow)",
		"Weather (thunder)",
		"Start music",
		"Stop music",
		"GPIO4 test: polled tone",
		"GPIO4 test: timer sweep",
		"Back"
	};

	void fillRect(TEXTURE &tex, int x, int y, int w, int h, COLOR c)
	{
		if(x >= static_cast<int>(tex.width) || y >= static_cast<int>(tex.height) || w <= 0 || h <= 0)
			return;

		if(x < 0)
		{
			w += x;
			x = 0;
		}
		if(y < 0)
		{
			h += y;
			y = 0;
		}
		if(x + w > static_cast<int>(tex.width))
			w = tex.width - x;
		if(y + h > static_cast<int>(tex.height))
			h = tex.height - y;

		for(int yy = 0; yy < h; ++yy)
		{
			COLOR *line = tex.bitmap + (y + yy) * tex.width + x;
			for(int xx = 0; xx < w; ++xx)
				line[xx] = c;
		}
	}
}

AudioTestTask::AudioTestTask()
{
}

AudioTestTask::~AudioTestTask()
{
}

void AudioTestTask::openFrom(Task *from)
{
	return_task = from;
	makeCurrent();
}

void AudioTestTask::makeCurrent()
{
	selected_item = 0;
	status_timeout = 0;
	setStatus(GameAudio::packAvailable() ? "Audio pack loaded" : "No audio pack (tones only)");
	Task::makeCurrent();
}

void AudioTestTask::setStatus(const char *text)
{
	snprintf(status, sizeof(status), "%s", text != nullptr ? text : "");
	status_timeout = 180;
}

void AudioTestTask::runItem(unsigned int item)
{
	char buffer[64];

	switch(item)
	{
	case ITEM_UI_CLICK:
		GameAudio::playUiSound(GameAudio::Sound::RandomClick);
		setStatus(GameAudio::packAvailable() ? "UI click (pack sample)" : "UI click (procedural tone)");
		break;

	case ITEM_FOOTSTEP:
		GameAudio::footstep(GameAudio::MaterialGrass);
		setStatus("Footstep: grass material");
		break;

	case ITEM_BREAK_BLOCK:
		GameAudio::digBlock(GameAudio::MaterialStone);
		setStatus("Block break: stone material");
		break;

	case ITEM_MOB:
		GameAudio::mobSound(GameAudio::MobCow, false);
		setStatus("Mob: cow idle");
		break;

	case ITEM_THUNDER:
		GameAudio::weatherThunder();
		setStatus("Weather: thunder cue");
		break;

	case ITEM_MUSIC:
		if(GameAudio::startMusic())
			snprintf(buffer, sizeof(buffer), "Music track %u/%u",
				GameAudio::currentMusicTrack(), GameAudio::musicTrackCount());
		else
			snprintf(buffer, sizeof(buffer), "Music unavailable: %s", GameAudio::packStatus());
		setStatus(buffer);
		break;

	case ITEM_STOP_MUSIC:
		GameAudio::stopMusic();
		setStatus("Music stopped");
		break;

	case ITEM_GPIO4_POLLED:
	{
		// Only the pin is driven: no timer, no interrupts, fully reversible.
		const int periods = GameAudioGpio4::testPolled(1000, 800);
		if(periods > 0)
			snprintf(buffer, sizeof(buffer), "Polled 1 kHz tone, %d periods", periods);
		else
			snprintf(buffer, sizeof(buffer), "Polled test failed: %s", GameAudioGpio4::lastError());
		setStatus(buffer);
		break;
	}

	case ITEM_GPIO4_TIMER:
		if(!GameAudioGpio4::supported())
		{
			setStatus("GPIO4 output requires an original CX");
			break;
		}
		if(!GameAudioOutput::enableGpio4())
		{
			snprintf(buffer, sizeof(buffer), "Timer test failed: %s",
				GameAudioGpio4::lastError() != nullptr ? GameAudioGpio4::lastError() : "unavailable");
			setStatus(buffer);
			break;
		}
		else
		{
			const int result = GameAudioGpio4::testTimer(2000);
			GameAudioOutput::disableGpio4();
			if(result == 0)
				snprintf(buffer, sizeof(buffer), "Timer sweep done, %u Hz carrier",
					static_cast<unsigned int>(GameAudioGpio4::carrierHz()));
			else if(result > 0)
				snprintf(buffer, sizeof(buffer), "Timer sweep done, %d underruns", result);
			else
				snprintf(buffer, sizeof(buffer), "Timer sweep failed: %s",
					GameAudioGpio4::lastError() != nullptr ? GameAudioGpio4::lastError() : "unavailable");
			setStatus(buffer);
		}
		break;

	case ITEM_BACK:
	default:
		(return_task != nullptr ? return_task : &start_task)->makeCurrent();
		break;
	}
}

void AudioTestTask::render()
{
	drawStringCenter("Audio Test", 0xFFFF, *screen, SCREEN_WIDTH / 2, 8);

	const int button_w = 220;
	const int button_h = 18;
	const int button_x = (SCREEN_WIDTH - button_w) / 2;
	int y = 30;

	for(unsigned int i = 0; i < ITEM_MAX; ++i)
	{
		const bool selected = (static_cast<int>(i) == selected_item);
		if(selected)
		{
			fillRect(*screen, button_x, y, button_w, button_h, 0x7BEF);
			drawRectangle(*screen, button_x, y, button_w, button_h, 0xFFFF);
		}
		else
		{
			drawRectangle(*screen, button_x, y, button_w, button_h, 0x8410);
		}

		drawString(item_labels[i], selected ? 0x0000 : 0xFFFF, *screen, button_x + 6, y + 4);
		y += button_h + 3;
	}

	y += 4;
	drawString("Audio pack:", 0x8410, *screen, 8, y);
	drawString(GameAudio::packStatus(), 0xFFFF, *screen, 100, y);
	y += fontHeight() + 2;

	drawString("Output:", 0x8410, *screen, 8, y);
	drawString(GameAudioOutput::backendName(), 0xFFFF, *screen, 100, y);
	y += fontHeight() + 2;

	drawString(GameAudioGpio4::status(), 0xFFFF, *screen, 8, y);
	y += fontHeight() + 2;

	if(status_timeout > 0)
		drawString(status, 0xFFE0, *screen, 8, y);
	else
		drawString("Up/Down to move, 5/Return to run", 0x8410, *screen, 8, y);
}

void AudioTestTask::logic(GLFix /*dt*/)
{
	if(status_timeout > 0)
		--status_timeout;

	if(key_held_down)
		key_held_down = keyPressed(KEY_NSPIRE_ESC) || keyPressed(KEY_NSPIRE_UP) || keyPressed(KEY_NSPIRE_DOWN)
			|| keyPressed(KEY_NSPIRE_2) || keyPressed(KEY_NSPIRE_8) || keyPressed(KEY_NSPIRE_5)
			|| keyPressed(KEY_NSPIRE_ENTER) || keyPressed(KEY_NSPIRE_CLICK);
	else if(keyPressed(KEY_NSPIRE_UP) || keyPressed(KEY_NSPIRE_8))
	{
		if(--selected_item < 0)
			selected_item = ITEM_MAX - 1;
		key_held_down = true;
	}
	else if(keyPressed(KEY_NSPIRE_DOWN) || keyPressed(KEY_NSPIRE_2))
	{
		if(++selected_item >= ITEM_MAX)
			selected_item = 0;
		key_held_down = true;
	}
	else if(keyPressed(KEY_NSPIRE_5) || keyPressed(KEY_NSPIRE_ENTER) || keyPressed(KEY_NSPIRE_CLICK))
	{
		runItem(static_cast<unsigned int>(selected_item));
		key_held_down = true;
	}
	else if(keyPressed(KEY_NSPIRE_ESC))
	{
		(return_task != nullptr ? return_task : &start_task)->makeCurrent();
		key_held_down = true;
	}
}
