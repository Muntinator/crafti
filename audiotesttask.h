#ifndef AUDIOTESTTASK_H
#define AUDIOTESTTASK_H

#include "task.h"

/**
 * Interactive audio diagnostic.
 *
 * Exercises the whole feature set (UI, footsteps, block material, mobs,
 * weather, music) and both UART output paths: a blocking square wave that only
 * touches the dock pin, and the interrupt driven sigma-delta sweep that
 * gameplay audio uses.
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

	Task *return_task = nullptr;
	int selected_item = 0;
	unsigned int status_timeout = 0;
	char status[64] = {0};
};

extern AudioTestTask audio_test_task;

#endif // AUDIOTESTTASK_H
