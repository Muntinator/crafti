#ifndef AUDIOTESTTASK_H
#define AUDIOTESTTASK_H

#include "task.h"

/**
 * Interactive audio diagnostic.
 *
 * Exercises the whole feature set (UI, footsteps, block material, mobs,
 * weather, music) and all three output paths: the interrupt driven sigma-delta
 * sweep that gameplay audio uses (on the UART pin or on GPIO 22), and a
 * blocking square wave that only touches the dock's UART pin.
 */
class AudioTestTask : public Task
{
public:
	enum Item
	{
		ITEM_UI_CLICK = 0,
		ITEM_FOOTSTEP,
		ITEM_BREAK_BLOCK,
		ITEM_MOB,
		ITEM_THUNDER,
		ITEM_MUSIC,
		ITEM_STOP_MUSIC,
		ITEM_UART_POLLED,
		ITEM_UART_SWEEP,
		ITEM_GPIO_SWEEP,
		ITEM_BACK,
		ITEM_MAX
	};

	AudioTestTask();
	virtual ~AudioTestTask();

	virtual void makeCurrent() override;
	virtual void render() override;
	virtual void logic(GLFix dt) override;

	/** Opens the screen and remembers where Back should return to. */
	void openFrom(Task *from);

	/** Runs one diagnostic and stores a human readable result. */
	void runItem(unsigned int item);

private:
	void setStatus(const char *text);

	/**
	 * The box of one row of the packed button column. Rendered and hit-tested
	 * through this, so the pointer and the drawing cannot disagree about where a
	 * button is.
	 */
	void buttonRect(unsigned int item, int &x, int &y, int &w, int &h) const;

	Task *return_task = nullptr;
	int selected_item = 0;
	unsigned int status_timeout = 0;
	char status[64] = {0};

#ifndef _TINSPIRE
	/** The left button's state last frame, so a click is an edge, not a hold. */
	bool left_mouse_was_down = false;
	/** The pointer's position last frame; only a move takes the focus. */
	int last_mouse_x = -1, last_mouse_y = -1;
#endif
};

extern AudioTestTask audio_test_task;

#endif // AUDIOTESTTASK_H
