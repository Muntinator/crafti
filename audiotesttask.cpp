#include "audiotesttask.h"

#include "controls.h"

#include <stdio.h>

#include "audio_manager.h"
#include "audio_nspire_gpio.h"
#include "audio_nspire_tx.h"
#include "audio_output.h"
#include "font.h"
#include "menuui.h"
#include "starttask.h"
#include "texturetools.h"

#ifndef _TINSPIRE
#include <SDL/SDL.h>
#endif

AudioTestTask audio_test_task;

namespace
{
	/** The button column's geometry, in calculator pixels (the CX screen is 320x240). */
	constexpr int RowPitch = 13;   // 12 pixels of button, 1 of gap
	constexpr int RowHeight = 12;
	constexpr int FirstRowY = 26;
	constexpr int StatusGap = 6;   // between the last row and the status block
	constexpr int StatusLines = 4; // pack, output, backend status, hint
	constexpr int FontLineAdvance = 8 + 2;

	// Every extra row (the USB D+ sweep is the thirteenth) has to come out of the
	// pitch, because the column and the status block below it share one screen.
	// This is the check that the pitch is still tight enough.
	static_assert(FirstRowY + (AudioTestTask::ITEM_MAX - 1) * RowPitch + RowHeight + StatusGap
		+ StatusLines * FontLineAdvance <= 240,
		"the audio test column and its status block must fit a 240-pixel screen");

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
		"GPIO 22: sweep",
		"GPIO 22: buzzer tone",
		"USB D+: sweep",
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
	// The sound test drives the music by hand (its own start/stop buttons), so
	// the music manager leaves the tracks alone while this screen is open.
	GameAudio::setMusicDesired(false);
	selected_item = 0;
	status_timeout = 0;
	setStatus(GameAudio::packAvailable() ? "Audio pack loaded" : "No audio pack (tones only)");

	// A resting pointer is not a move, and the press that opened the screen must
	// not also take a row on the first frame. Done on both machines: on the
	// calculator the touchpad's own press is what opened it.
	Pointer::seed();

	Task::makeCurrent();
}

void AudioTestTask::buttonRect(unsigned int item, int &x, int &y, int &w, int &h) const
{
	const int scale = MenuUI::uiScale();
	w = SCREEN_WIDTH - 16;
	// Thirteen rows plus the four-line status block have to fit a 240-pixel
	// screen, so the buttons are packed at a 13-pixel pitch (12 pixels of
	// button, 1 of gap). The constants are named so the static_assert below is
	// about the same numbers the rows are actually drawn at.
	h = RowHeight * scale;
	x = (SCREEN_WIDTH - w) / 2;
	y = FirstRowY * scale + static_cast<int>(item) * RowPitch * scale;
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

	case ITEM_GPIO_SWEEP:
	case ITEM_USB_SWEEP:
	{
		// The same sweep, out of whichever dock line the row asks for.
		const bool usb = item == ITEM_USB_SWEEP;
		if(!GameAudioGpio::supported())
		{
			setStatus("GPIO output requires an original CX");
			break;
		}
		if(!GameAudioOutput::enableGpio(usb ? GameAudioOutput::GpioLineUsbDataPlus
			: GameAudioOutput::GpioLineDock18, false))
		{
			snprintf(buffer, sizeof(buffer), "Sweep failed: %s",
				GameAudioGpio::lastError() != nullptr ? GameAudioGpio::lastError() : "unavailable");
			setStatus(buffer);
			break;
		}
		else
		{
			const int result = GameAudioGpio::testSweep(2000);
			GameAudioOutput::disableGpio();
			if(result == 0)
				snprintf(buffer, sizeof(buffer), "Sweep done on %s, %u Hz bits",
					usb ? "USB D+" : "GPIO 22", static_cast<unsigned int>(GameAudioGpio::bitRateHz()));
			else if(result > 0)
				snprintf(buffer, sizeof(buffer), "Sweep done, %d underruns", result);
			else
				snprintf(buffer, sizeof(buffer), "Sweep failed: %s",
					GameAudioGpio::lastError() != nullptr ? GameAudioGpio::lastError() : "unavailable");
			setStatus(buffer);
		}
		break;
	}

	case ITEM_GPIO_BUZZER:
	{
		// A fixed beep in the buzzer drive: the square wave a piezoelectric
		// buzzer plays at full swing, near the resonance most of them have.
		const int result = GameAudioGpio::testBuzzerTone(2000, 800);
		if(result == 0)
			snprintf(buffer, sizeof(buffer), "Buzzer tone done (2 kHz square wave)");
		else if(result > 0)
			snprintf(buffer, sizeof(buffer), "Buzzer tone: %d underruns", result);
		else
			snprintf(buffer, sizeof(buffer), "Buzzer tone failed: %s",
				GameAudioGpio::lastError() != nullptr ? GameAudioGpio::lastError() : "unavailable");
		setStatus(buffer);
		break;
	}

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

	// Thirteen rows do not fit a standard 24-pixel button column on a 240-pixel
	// screen, so the buttons are packed a little tighter -- but they are still the
	// vanilla widget sheet's button, in its plain and highlighted states.
	for(unsigned int i = 0; i < ITEM_MAX; ++i)
	{
		int button_x = 0, y = 0, button_w = 0, button_h = 0;
		buttonRect(i, button_x, y, button_w, button_h);
		const bool selected = (static_cast<int>(i) == selected_item);
		MenuUI::drawButton(*screen, button_x, y, button_w, button_h, selected);
		MenuUI::drawButtonLabel(item_labels[i], *screen, button_x, y, button_w, button_h, selected);
	}

	// The status block starts just under the last button, so the two cannot
	// disagree about the row pitch and push each other off the screen.
	int info_x = 0, y = 0, info_w = 0, info_h = 0;
	buttonRect(ITEM_MAX - 1, info_x, y, info_w, info_h);
	y += info_h + StatusGap * MenuUI::uiScale();
	drawString("Audio pack:", MenuUI::TextDisabled, *screen, 8, y);
	drawString(GameAudio::packStatus(), MenuUI::Text, *screen, 100, y);
	y += fontHeight() + 2;

	drawString("Output:", MenuUI::TextDisabled, *screen, 8, y);
	drawString(GameAudioOutput::backendName(), MenuUI::Text, *screen, 100, y);
	y += fontHeight() + 2;

	drawString(GameAudioOutput::gpioActive() ? GameAudioGpio::status() : GameAudioTx::status(),
		MenuUI::Text, *screen, 8, y);
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

	// Vanilla's pointer: hovering a row lights it, a click runs it. On the
	// calculator that is the touchpad's virtual cursor, so the diagnostic can be
	// reached and run without the keypad.
	Pointer::poll();
	const int mouse_x = Pointer::x(), mouse_y = Pointer::y();

	int hovered = -1;
	for(unsigned int i = 0; i < ITEM_MAX; ++i)
	{
		int x = 0, y = 0, w = 0, h = 0;
		buttonRect(i, x, y, w, h);
		if(mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h)
			hovered = static_cast<int>(i);
	}

	if(Pointer::moved() && hovered >= 0)
		selected_item = hovered;

	if(Pointer::clicked() && hovered >= 0)
	{
		selected_item = hovered;
		runItem(static_cast<unsigned int>(hovered));
		return;
	}

	if(key_held_down)
		key_held_down = Controls::cursorUp() || Controls::cursorDown()
			|| Controls::activate() || Controls::menu();
	else if(Controls::cursorUp())
	{
		if(--selected_item < 0)
			selected_item = ITEM_MAX - 1;
		key_held_down = true;
	}
	else if(Controls::cursorDown())
	{
		if(++selected_item >= ITEM_MAX)
			selected_item = 0;
		key_held_down = true;
	}
	else if(Controls::activate())
	{
		runItem(static_cast<unsigned int>(selected_item));
		key_held_down = true;
	}
	else if(Controls::menu())
	{
		(return_task != nullptr ? return_task : &start_task)->makeCurrent();
		key_held_down = true;
	}
}
