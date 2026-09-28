#ifndef AUDIO_GPIO4_HW_H
#define AUDIO_GPIO4_HW_H

#include <stdint.h>

/**
 * TI-Nspire CX hardware definitions used by the GPIO4 audio output backend.
 *
 * Verified against the Hackspire wiki (GPIO Pins, Memory-mapped IO ports on CX,
 * Timers, Interrupts) rather than guessed:
 *
 *  - The Ndless SDK exposes no GPIO, timer, interrupt or audio API.  Checked
 *    include/libndls.h, include/nucleus.h, include/os.h and include/hook.h:
 *    they offer msleep/idle/set_cpu_speed, filesystem and syscall helpers and
 *    function hooks, but nothing that reaches a pin or a DAC.  libndls itself
 *    reaches hardware by dereferencing fixed addresses (IO_LCD_CONTROL,
 *    REAL_SCREEN_BASE_ADDRESS), which is the same technique used here.
 *  - GPIO is split into 8-bit sections of 0x40 bytes starting at 0x90000000;
 *    GPIO number = section * 8 + bit.  GPIO4 is section 0, bit 4, which
 *    Hackspire's GPIO map identifies as dock connector pin 6.
 *  - The fast timer at 0x90010000 is an SP804 dual timer with an extra clock
 *    selection register at +0x80 (bit 0 selects a ~10 MHz clock, bit 1 a 32 kHz
 *    clock, neither selects ~33 MHz).  Only HW-AA has been characterised, so the
 *    absolute pitch is approximate and is documented as such.
 *  - Interrupts come from a PL190 at 0xDC000000; the fast timer is IRQ 17.
 *
 * Everything below is data only: no memory is touched by including this header.
 */
namespace Gpio4Hw
{
	// --- GPIO controller -----------------------------------------------------
	constexpr uint32_t GpioBase = 0x90000000;
	constexpr uint32_t GpioSectionStride = 0x40;
	constexpr uint32_t GpioIrqStatusOffset = 0x04; // write 1 to clear sticky status
	constexpr uint32_t GpioIrqEnableOffset = 0x08; // write 1 to unmask
	constexpr uint32_t GpioIrqDisableOffset = 0x0C; // write 1 to mask
	constexpr uint32_t GpioDirectionOffset = 0x10; // 0 = output, 1 = input
	constexpr uint32_t GpioOutputOffset = 0x14;
	constexpr uint32_t GpioInputOffset = 0x18;

	// Dock connector physical pin 6.
	constexpr uint32_t Gpio4Section = 0;
	constexpr uint32_t Gpio4Bit = 4;
	constexpr uint32_t Gpio4Mask = 1u << Gpio4Bit;
	constexpr uint32_t Gpio4DirectionAddress = GpioBase + Gpio4Section * GpioSectionStride + GpioDirectionOffset;
	constexpr uint32_t Gpio4OutputAddress = GpioBase + Gpio4Section * GpioSectionStride + GpioOutputOffset;
	constexpr uint32_t Gpio4InputAddress = GpioBase + Gpio4Section * GpioSectionStride + GpioInputOffset;

	// --- Fast timer (SP804) --------------------------------------------------
	constexpr uint32_t TimerBase = 0x90010000;
	constexpr uint32_t Timer1Load = 0x00;
	constexpr uint32_t Timer1Value = 0x04;
	constexpr uint32_t Timer1Control = 0x08;
	constexpr uint32_t Timer1InterruptClear = 0x0C;
	constexpr uint32_t Timer1RawStatus = 0x10;
	constexpr uint32_t Timer1MaskedStatus = 0x14;
	constexpr uint32_t TimerClockSelect = 0x80; // Hackspire extension on the fast timer

	constexpr uint32_t TimerControlEnable = 1u << 0;
	constexpr uint32_t TimerControlPeriodic = 1u << 1;
	constexpr uint32_t TimerControlInterruptEnable = 1u << 2;
	constexpr uint32_t TimerControl32Bit = 1u << 6;

	// Hackspire: bit 0 selects ~10 MHz, bit 1 the 32 kHz clock, neither ~33 MHz.
	// The ~10 MHz setting is the least ambiguous of the three for audio.
	constexpr uint32_t TimerClockSelect10MHz = 0x1;

	// --- PL190 interrupt controller -----------------------------------------
	constexpr uint32_t VicBase = 0xDC000000;
	constexpr uint32_t VicIrqStatus = 0x00;
	constexpr uint32_t VicIrqRawStatus = 0x04;
	constexpr uint32_t VicIntEnable = 0x08; // write 1s to unmask
	constexpr uint32_t VicIntDisable = 0x0C; // write 1s to mask
	constexpr uint32_t VicIrqCurrent = 0x20;
	constexpr uint32_t VicIrqVector = 0x24;
	constexpr uint32_t VicIrqAcknowledge = 0x28;
	constexpr uint32_t VicIrqMaxPriority = 0x2C;
	constexpr uint32_t VicAllIrqs = 0xFFFFFFFFu;

	constexpr uint32_t FastTimerIrqNumber = 17;

	// --- Exception vector table ---------------------------------------------
	// Boot1 ROM is mapped at 0x00000000 on CX, so the OS may instead install its
	// handlers in the ARM high-vector table.  Both are probed at runtime.
	constexpr uint32_t LowIrqVectorAddress = 0x00000018;
	constexpr uint32_t HighIrqVectorAddress = 0xFFFF0018;

	/** Output rate of the 1-bit DAC emulation, derived from the timer clock. */
	constexpr uint32_t AssumedTimerClockHz = 10000000u;
	/** Sigma-delta carrier; ~2000 cycles per interrupt on a 150 MHz CX. */
	constexpr uint32_t CarrierHz = 80000u;
	/** Audio samples per carrier tick (oversampling ratio of the modulator). */
	constexpr uint32_t OversamplingRatio = 10u;
	/** Mixer rate the carrier is locked to, so audio timing never follows FPS. */
	constexpr uint32_t MixerRateHz = CarrierHz / OversamplingRatio;
	constexpr uint32_t TimerReload = (AssumedTimerClockHz / CarrierHz) - 1;
	static_assert(TimerReload >= 1, "carrier rate cannot exceed the timer clock");

	/** Captured before the backend writes anything, restored on shutdown. */
	struct RegisterSnapshot
	{
		uint32_t gpio_direction;
		uint32_t gpio_output;
		uint32_t gpio_irq_enable; // read back from the VIC mask instead: see below
		uint32_t timer_control;
		uint32_t timer_load;
		uint32_t timer_clock_select;
		uint32_t vic_mask;
		uint32_t vector_address;
		uint32_t vector_instruction;
		bool valid;
	};
}

#endif // AUDIO_GPIO4_HW_H
