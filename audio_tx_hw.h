#ifndef AUDIO_TX_HW_H
#define AUDIO_TX_HW_H

#include <stdint.h>

/**
 * TI-Nspire CX hardware definitions used by the dock pin 4 (UART Tx) audio
 * output backend.
 *
 * Why pin 4 and not pin 6 (GPIO4): both are digital pins on the same dock, but
 * GPIO4 doubles as USB Data+ when a Navigator cradle is attached, whereas the
 * UART transmitter is a dedicated, always-output pin that nothing else in the
 * OS drives during play. The PL011 also shifts the bits out itself, so the
 * sample clock comes from the UART's own baud generator rather than from a CPU
 * timer the program has to take over.
 *
 * Verified against the Hackspire wiki (Connector J01 - Dock connector, UART,
 * Memory-mapped IO ports on CX, GPIO Pins, Interrupts) rather than guessed:
 *
 *  - The dock connector's serial pins are pin 3 (Rx), pin 4 (Tx) and pin 5
 *    (GND); a TTL serial adapter is wired straight to them at 115200 8N1, so
 *    the transmitter is a real 3.3 V TTL output.
 *  - The CX's serial UART is a PrimeCell PL011 at 0x90020000. Register layout:
 *    0x000 UARTDR (write = transmit FIFO), 0x018 UARTFR (flags), 0x024 UARTIBRD
 *    and 0x028 UARTFBRD (baud divisors), 0x02C UARTLCR_H (line control),
 *    0x030 UARTCR (control), 0x034 UARTIFLS (FIFO levels), 0x038 UARTIMSC,
 *    0x03C UARTRIS, 0x040 UARTMIS, 0x044 UARTICR.
 *  - Baud = UARTCLK / (16 * (IBRD + FBRD/64)). The UART runs off the APB clock,
 *    which Hackspire puts at 22.5 MHz on the CX.
 *  - Power management 0x900B0018 gates peripheral bus access; bit 17 disables
 *    the UART, so it has to be cleared before the UART answers anything.
 *  - The UART's interrupt is IRQ 1 on the interrupt controller at 0xDC000000.
 *
 * The CX's controller is an ARM PrimeCell PL190 (Hackspire says so explicitly:
 * the detailed register docs there are "for TI-Nspire classic. The CX has a
 * PL190 interrupt controller"), so it is NOT the classic map: enable/disable
 * live at +0x10/+0x14, +0x0C is the IRQ/FIQ *routing* register, and +0x30 is
 * the vectored dispatch register the OS's interrupt handler reads to find out
 * which service routine to call. The PL190 also has 16 vector slots
 * (+0x100 + 4*slot for the handler address, +0x200 + 4*slot for control:
 * bit 5 = enable, bits 4:0 = IRQ source). Programming a free slot is the
 * supported way to add an interrupt handler: no code is patched at all.
 *
 * Everything below is data only: no memory is touched by including this header.
 */
namespace UartTxHw
{
	// --- PL011 UART ---------------------------------------------------------
	constexpr uint32_t UartBase = 0x90020000;
	constexpr uint32_t UartDr = 0x000;   // read: RX FIFO, write: TX FIFO
	constexpr uint32_t UartRsr = 0x004;  // receive status / error clear
	constexpr uint32_t UartFr = 0x018;   // flag register
	constexpr uint32_t UartIlpr = 0x020;
	constexpr uint32_t UartIbrd = 0x024; // integer baud divisor
	constexpr uint32_t UartFbrd = 0x028; // fractional baud divisor
	constexpr uint32_t UartLcrH = 0x02C; // line control
	constexpr uint32_t UartCr = 0x030;   // control
	constexpr uint32_t UartIfLs = 0x034; // interrupt FIFO level select
	constexpr uint32_t UartImsc = 0x038; // interrupt mask set/clear
	constexpr uint32_t UartRis = 0x03C;  // raw interrupt status
	constexpr uint32_t UartMis = 0x040;  // masked interrupt status
	constexpr uint32_t UartIcr = 0x044;  // interrupt clear

	// UARTFR bits.
	constexpr uint32_t FlagTxFifoFull = 1u << 5;
	constexpr uint32_t FlagTxFifoEmpty = 1u << 7;
	constexpr uint32_t FlagBusy = 1u << 3;

	// UARTLCR_H: FIFO on, 8 data bits (WLEN = 11), no parity, one stop bit.
	constexpr uint32_t LineFifoEnable = 1u << 4;
	constexpr uint32_t LineEightDataBits = 3u << 5;

	// UARTCR: bring the UART and its transmitter up, leave the receiver alone.
	constexpr uint32_t ControlUartEnable = 1u << 0;
	constexpr uint32_t ControlTxEnable = 1u << 8;

	// Interrupt bits shared by UARTIMSC/UARTRIS/UARTMIS/UARTICR.
	constexpr uint32_t InterruptTx = 1u << 5; // TX FIFO dropped below the trigger
	constexpr uint32_t InterruptRx = 1u << 4;

	// UARTIFLS: ask for a TX interrupt once the FIFO is half empty (bits 3:5).
	constexpr uint32_t IfLsTxHalf = 2u << 3;
	/** FIFO depth of the CX PL011; half of it is the TX trigger. */
	constexpr uint32_t TxFifoDepth = 16;
	constexpr uint32_t TxTriggerLevel = TxFifoDepth / 2;

	// --- Power management ---------------------------------------------------
	constexpr uint32_t PowerBase = 0x900B0000;
	constexpr uint32_t PowerPeripheralDisable = 0x18;
	constexpr uint32_t PowerUartBusDisable = 1u << 17;

	// --- PL190 interrupt controller (CX) ------------------------------------
	// Register map per the ARM PL190 (DDI 0181) and the CX's boot behaviour.
	constexpr uint32_t VicBase = 0xDC000000;
	constexpr uint32_t VicIrqStatus = 0x00;   // read: active IRQ sources
	constexpr uint32_t VicFiqStatus = 0x04;   // read: active FIQ sources
	constexpr uint32_t VicIrqRawStatus = 0x08; // read: raw source status
	constexpr uint32_t VicIntSelect = 0x0C;   // write: 1 routes the source to FIQ
	constexpr uint32_t VicIntEnable = 0x10;   // write 1s to unmask (IRQ)
	constexpr uint32_t VicIntDisable = 0x14;  // write 1s to mask (IRQ)
	constexpr uint32_t VicIrqVector = 0x30;   // read: current handler, write: end of interrupt
	constexpr uint32_t VicDefaultVector = 0x34;
	constexpr uint32_t VicVectorAddr0 = 0x100; // + 4 * slot: handler address
	constexpr uint32_t VicVectorCtrl0 = 0x200; // + 4 * slot: control
	constexpr uint32_t VicVectorCtrlEnable = 1u << 5;
	constexpr uint32_t VicVectorSlots = 16;

	constexpr uint32_t UartIrqNumber = 1;

	// --- Carrier / baud -----------------------------------------------------
	/**
	 * APB clock the PL011 is divided from. Hackspire lists the CX fast timer's
	 * APB clock as 22.5 MHz; the UART shares it. If the OS resamples the clock
	 * the absolute pitch shifts with it, like the timer backend's did.
	 */
	constexpr uint32_t AssumedUartClockHz = 22500000u;

	/**
	 * One UART byte is a start bit, eight data bits and a stop bit, so a frame
	 * is ten bit-times: the channel carries `CarrierBaud / 10` bytes per second.
	 * 80 kBd gives exactly 8000 bytes per second, one per mixer sample, and the
	 * eight data bits of the byte are the oversampled output for that sample.
	 */
	constexpr uint32_t CarrierBaud = 80000u;
	constexpr uint32_t FrameBits = 10u;
	constexpr uint32_t DataBitsPerByte = 8u;
	constexpr uint32_t BitsPerSample = DataBitsPerByte;

	/** Bytes per second the wire carries, i.e. the sample clock. */
	constexpr uint32_t MixerRateHz = CarrierBaud / FrameBits;
	/** Data-bit rate of the sigma-delta stream, ignoring the framing bits. */
	constexpr uint32_t CarrierHz = (CarrierBaud * DataBitsPerByte) / FrameBits;
	constexpr uint32_t OversamplingRatio = CarrierHz / MixerRateHz;

	/** UARTCLK / (16 * baud), split into the integer and 1/64th registers. */
	constexpr uint32_t DivisorTimes16 = 16u * CarrierBaud;
	constexpr uint32_t Ibrd = AssumedUartClockHz / DivisorTimes16;
	constexpr uint32_t DivisorRemainder = AssumedUartClockHz % DivisorTimes16;
	constexpr uint32_t Fbrd = ((DivisorRemainder * 64u) + (DivisorTimes16 / 2u)) / DivisorTimes16;

	static_assert(BitsPerSample == OversamplingRatio,
		"one byte carries exactly the oversampling ratio's worth of delta bits");
	static_assert(CarrierBaud % FrameBits == 0, "the carrier must divide into whole frames");
	static_assert(Ibrd >= 1, "the baud divisor cannot be zero");
	static_assert(Fbrd < 64, "the fractional divisor is six bits");
	static_assert(Ibrd == 17 && Fbrd == 37,
		"22.5 MHz / (16 * 80000 baud) is IBRD 17, FBRD 37, to the last 1/64th");
	static_assert(MixerRateHz == 8000, "the wire byte rate must equal the mixer rate");

	/** Captured before the backend writes anything, restored on shutdown. */
	struct RegisterSnapshot
	{
		uint32_t uart_cr;
		uint32_t uart_lcr_h;
		uint32_t uart_ibrd;
		uint32_t uart_fbrd;
		uint32_t uart_ifls;
		uint32_t uart_imsc;
		uint32_t power_disable;
		bool valid;
	};
}

#endif // AUDIO_TX_HW_H
