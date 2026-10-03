#ifndef AUDIO_GPIO_HW_H
#define AUDIO_GPIO_HW_H

#include <stdint.h>

#include "audio_tx_hw.h"

/**
 * TI-Nspire CX hardware definitions used by the GPIO 22 (dock pin 18) audio
 * output backend.
 *
 * Why not the dock's UART Tx (pin 4): on this calculator that pin is broken, so
 * the audio stream has to move to a general-purpose line. GPIO 22 is the
 * safest output on the dock: the CX pins with known jobs are GPIO 5 (active-low
 * USB VBUS control), 6 (charging), 19 (WLAN cradle detect), 20 (USB micro-B
 * attached), 23 (LCD_OFF) and 24 (keypad present); 22 is unclaimed, and pin 5
 * of the dock is a nearby ground for the return path.
 *
 * Verified against the Hackspire wiki (GPIO Pins, Interrupts, Memory-mapped IO
 * ports on CX) and against Firebird's `core/misc.c` emulation (gpio_read /
 * gpio_write, timer_cx_*), not guessed:
 *
 *  - GPIO lines live at 0x90000000 in sections of 0x40 bytes; the line number
 *    is `section * 8 + bit`. Per section: +0x00 masked int status (R), +0x04
 *    raw/sticky int status (R, W clears), +0x08 int mask set (R/W), +0x0C int
 *    mask clear (W), +0x10 direction (0 = output), +0x14 output bit, +0x18
 *    input bit (reads the driven level back), +0x1C invert, +0x20 sticky
 *    select. The port registers are byte wide and hold all eight lines of the
 *    section, so changing one pin is a read-modify-write.
 *  - The CX has SP804-style dual timers in blocks selected by `(addr >> 16) % 5`
 *    and a timer inside the block by `addr >> 5 & 1`. Per timer: +0x000 load
 *    (writing it also arms a reload), +0x004 current value, +0x008 control,
 *    +0x00C write clears the interrupt, +0x010 interrupt status (R), +0x014
 *    status masked by the control's interrupt-enable bit (R), +0x018 load
 *    without arming. Control: bit 7 enable, bit 6 periodic (reload to load when
 *    the counter wraps), bit 5 interrupt enable, bit 1 32-bit counter, bit 0
 *    one-shot, bits 3:2 prescale. The counter runs from the 32.768 kHz crystal
 *    (Ndless's msleep/idle code programs the sibling block the same way).
 *  - Which block feeds which IRQ: `INT_TIMER0 + which` is 17/18/19 for
 *    which = 0/1/2, so block 0x900C0000 (which 1) is IRQ 18. Block 0x90010000
 *    is the "fast timer" (IRQ 17) that even the emulator refuses to run, and
 *    Ndless owns block 0x900D0000 (IRQ 19) for msleep/idle, so 0x900C0000 is
 *    the block a program can take over without fighting the OS.
 *  - The interrupt controller is the same ARM PL190 the UART backend uses
 *    (audio_tx_hw.h): 16 vector slots at +0x100/+0x200, enable at +0x10,
 *    clear at +0x14, end of interrupt at +0x30. Its definitions are shared
 *    from audio_tx_hw.h below rather than duplicated.
 *
 * Everything here is data only: no memory is touched by including this header.
 */
namespace GpioAudioHw
{
	// --- GPIO controller ----------------------------------------------------
	constexpr uint32_t GpioBase = 0x90000000;
	constexpr uint32_t GpioSectionStride = 0x40;
	constexpr uint32_t GpioIntStatus = 0x00;
	constexpr uint32_t GpioRawStatus = 0x04;
	constexpr uint32_t GpioIntMaskSet = 0x08;
	constexpr uint32_t GpioIntMaskClear = 0x0C;
	constexpr uint32_t GpioDirection = 0x10; // 0 = output, 1 = input
	constexpr uint32_t GpioOutput = 0x14;
	constexpr uint32_t GpioInput = 0x18;
	constexpr uint32_t GpioInvert = 0x1C;
	constexpr uint32_t GpioStickySelect = 0x20;

	/** The audio line: dock pin 18, the one unclaimed output on the CX dock. */
	constexpr uint32_t AudioGpioNumber = 22;
	constexpr uint32_t GpioSection = AudioGpioNumber / 8;
	constexpr uint32_t GpioBit = AudioGpioNumber % 8;
	constexpr uint32_t GpioBitMask = 1u << GpioBit;
	/** Base of the section that owns the audio line (GPIO 16..23). */
	constexpr uint32_t GpioSectionBase = GpioBase + GpioSection * GpioSectionStride;

	// --- SP804-style dual timer, block 0x900C0000 (IRQ 18) -------------------
	constexpr uint32_t TimerBase = 0x900C0000;
	constexpr uint32_t TimerLoad = 0x000;        // write also arms a reload
	constexpr uint32_t TimerValue = 0x004;
	constexpr uint32_t TimerControl = 0x008;
	constexpr uint32_t TimerIntClear = 0x00C;    // any write clears the interrupt
	constexpr uint32_t TimerIntStatus = 0x010;
	constexpr uint32_t TimerMaskedStatus = 0x014;
	constexpr uint32_t TimerBackgroundLoad = 0x018;

	constexpr uint32_t TimerControlEnable = 1u << 7;
	constexpr uint32_t TimerControlPeriodic = 1u << 6; // reload to load on wrap
	constexpr uint32_t TimerControlIntEnable = 1u << 5;
	constexpr uint32_t TimerControl32Bit = 1u << 1;
	constexpr uint32_t TimerControlOneShot = 1u << 0;

	/**
	 * Free-running periodic 32-bit timer with the interrupt enabled and no
	 * prescale: one interrupt every `TimerReload + 1` ticks of the crystal.
	 */
	constexpr uint32_t TimerControlValue =
		TimerControlEnable | TimerControlPeriodic | TimerControlIntEnable | TimerControl32Bit;

	constexpr uint32_t TimerIrqNumber = 18;

	// --- Timing -------------------------------------------------------------
	/** The crystal the CX timers count, per Ndless's sleep code. */
	constexpr uint32_t TimerClockHz = 32768;
	/** Output bit rate: one bit every two crystal ticks, two and a bit bits per
	 *  mixer sample. A sigma-delta bit stream is what an RC low-pass filter
	 *  turns back into audio, so the rate only sets the noise floor. */
	constexpr uint32_t TicksPerBit = 2;
	constexpr uint32_t BitRateHz = TimerClockHz / TicksPerBit;
	constexpr uint32_t TimerReload = TicksPerBit - 1;
	/** The mixer runs at the same rate on every backend. */
	constexpr uint32_t SampleRateHz = 8000;

	static_assert(AudioGpioNumber == GpioSection * 8 + GpioBit, "the line splits into section and bit");
	static_assert(GpioSection == 2 && GpioBit == 6, "GPIO 22 is section 2, bit 6");
	static_assert(GpioSectionBase == 0x90000080, "section 2 of the GPIO map");
	static_assert(TicksPerBit == TimerReload + 1, "the counter wraps after load + 1 ticks");
	static_assert(BitRateHz * TicksPerBit == TimerClockHz, "the bit rate divides the crystal");
	static_assert(SampleRateHz == UartTxHw::MixerRateHz, "every backend serves the same mixer");
	static_assert(BitRateHz > SampleRateHz, "at least one bit per mixer sample");

	/** Captured before the backend writes anything, restored on shutdown. */
	struct RegisterSnapshot
	{
		uint32_t gpio_direction;
		uint32_t gpio_output;
		uint32_t timer_load;
		uint32_t timer_control;
		bool valid;
	};

	// --- PL190 interrupt controller (CX) ------------------------------------
	// Shared with the UART backend's header; see its comment for the map.
	using UartTxHw::VicBase;
	using UartTxHw::VicIntSelect;
	using UartTxHw::VicIntEnable;
	using UartTxHw::VicIntDisable;
	using UartTxHw::VicIrqVector;
	using UartTxHw::VicDefaultVector;
	using UartTxHw::VicVectorAddr0;
	using UartTxHw::VicVectorCtrl0;
	using UartTxHw::VicVectorCtrlEnable;
	using UartTxHw::VicVectorSlots;
}

#endif // AUDIO_GPIO_HW_H
