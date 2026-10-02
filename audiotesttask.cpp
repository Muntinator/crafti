#include "audiotesttask.h"

#include <stdio.h>

#include "audio_manager.h"
#include "audio_nspire_tx.h"
#include "audio_output.h"
#include "font.h"
#include "menuui.h"
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
		"UART test: polled tone",
		"UART test: sweep",
		"Back"
	};

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

	case ITEM_UART_POLLED:
	{
		// Only the pin is driven: no interrupts, fully reversible.
		const int periods = GameAudioTx::testPolled(1000, 800);
		if(periods > 0)
			snprintf(buffer, sizeof(buffer), "Polled 1 kHz tone, %d periods", periods);
		else
			snprintf(buffer, sizeof(buffer), "Polled test failed: %s", GameAudioTx::lastError());
		setStatus(buffer);
		break;
	}

	case ITEM_UART_SWEEP:
		if(!GameAudioTx::supported())
		{
			setStatus("UART output requires an original CX");
			break;
		}
		if(!GameAudioOutput::enableUartTx())
		{
			snprintf(buffer, sizeof(buffer), "Sweep failed: %s",
				GameAudioTx::lastError() != nullptr ? GameAudioTx::lastError() : "unavailable");
			setStatus(buffer);
			break;
		}
		else
		{
			const int result = GameAudioTx::testSweep(2000);
			GameAudioOutput::disableUartTx();
			if(result == 0)
				snprintf(buffer, sizeof(buffer), "Sweep done, %u Hz carrier",
					static_cast<unsigned int>(GameAudioTx::carrierHz()));
			else if(result > 0)
				snprintf(buffer, sizeof(buffer), "Sweep done, %d underruns", result);
			else
				snprintf(buffer, sizeof(buffer), "Sweep failed: %s",
					GameAudioTx::lastError() != nullptr ? GameAudioTx::lastError() : "unavailable");
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
	// The dirt every vanilla menu is drawn on, and a vanilla heading.
	MenuUI::drawMenuBackground(*screen);
	MenuUI::drawHeading("Audio Test", *screen, MenuUI::headingY());

	// Ten rows do not fit a standard 24-pixel button column on a 240-pixel
	// screen, so the buttons are packed a little tighter -- but they are still the
	// vanilla widget sheet's button, in its plain and highlighted states.
	const int button_w = SCREEN_WIDTH - 16;
	const int button_h = 16 * MenuUI::uiScale();
	const int button_x = (SCREEN_WIDTH - button_w) / 2;
	int y = 26 * MenuUI::uiScale();

	for(unsigned int i = 0; i < ITEM_MAX; ++i)
	{
		const bool selected = (static_cast<int>(i) == selected_item);
		MenuUI::drawButton(*screen, button_x, y, button_w, button_h, selected);
		MenuUI::drawButtonLabel(item_labels[i], *screen, button_x, y, button_w, button_h, selected);
		y += button_h + 2 * MenuUI::uiScale();
	}

	y += 4;
	drawString("Audio pack:", MenuUI::TextDisabled, *screen, 8, y);
	drawString(GameAudio::packStatus(), MenuUI::Text, *screen, 100, y);
	y += fontHeight() + 2;

	drawString("Output:", MenuUI::TextDisabled, *screen, 8, y);
	drawString(GameAudioOutput::backendName(), MenuUI::Text, *screen, 100, y);
	y += fontHeight() + 2;

	drawString(GameAudioTx::status(), MenuUI::Text, *screen, 8, y);
	y += fontHeight() + 2;

	if(status_timeout > 0)
		drawString(status, MenuUI::Splash, *screen, 8, y);
	else
		drawString("Up/Down to move, 5/Return to run", MenuUI::TextDisabled, *screen, 8, y);
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
