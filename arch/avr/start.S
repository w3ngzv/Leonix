/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Reset entry and interrupt vector table for the ATmega32U4.
 *
 * Caterina jumps here with interrupts disabled and IVSEL clear, but on
 * the timeout path it leaves the USB controller running.  See
 * Documentation/booting.rst, section 3, for why the USB registers are
 * written unconditionally.
 */

#include <avr/io.h>

#define NR_VECTORS	43

	.section .vectors, "ax", @progbits
	.global	__vectors
__vectors:
	jmp	__reset
	.irp	n, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42
	.weak	__vector_\n
	.set	__vector_\n, __bad_interrupt
	jmp	__vector_\n
	.endr
	.if	(. - __vectors) != NR_VECTORS * 4
	.error	"vector table size does not match NR_VECTORS"
	.endif

	.text
	.global	__reset
__reset:
	clr	r1
	out	_SFR_IO_ADDR(SREG), r1
	ldi	r28, lo8(RAMEND)
	ldi	r29, hi8(RAMEND)
	out	_SFR_IO_ADDR(SPH), r29
	out	_SFR_IO_ADDR(SPL), r28

	/* Return the USB controller and the PLL to their reset values. */
	ldi	r24, 1 << FRZCLK
	sts	_SFR_MEM_ADDR(USBCON), r24
	sts	_SFR_MEM_ADDR(USBINT), r1
	sts	_SFR_MEM_ADDR(UHWCON), r1
	out	_SFR_IO_ADDR(PLLCSR), r1
	ldi	r24, 1 << PDIV2
	out	_SFR_IO_ADDR(PLLFRQ), r24

	/* Copy .data from flash.  The image is below 64 KiB, so lpm reaches it. */
	ldi	r26, lo8(__data_start)
	ldi	r27, hi8(__data_start)
	ldi	r30, lo8(__data_load_start)
	ldi	r31, hi8(__data_load_start)
	rjmp	2f
1:	lpm	r0, Z+
	st	X+, r0
2:	cpi	r26, lo8(__data_end)
	ldi	r24, hi8(__data_end)
	cpc	r27, r24
	brne	1b

	/* Zero .bss. */
	ldi	r26, lo8(__bss_start)
	ldi	r27, hi8(__bss_start)
	rjmp	4f
3:	st	X+, r1
4:	cpi	r26, lo8(__bss_end)
	ldi	r24, hi8(__bss_end)
	cpc	r27, r24
	brne	3b

	call	main
	cli
5:	rjmp	5b

/*
 * Any vector without a handler ends up here.  The CPU has already
 * cleared the I flag on entry, so spinning is enough to stop.
 */
	.global	__bad_interrupt
__bad_interrupt:
	rjmp	__bad_interrupt
