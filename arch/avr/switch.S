/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Context switch on the Timer1 compare match A interrupt.
 *
 * The interrupt has already pushed the PC.  The frame below it is r0,
 * SREG, r1, r2 ... r31, from high address to low, and task_create()
 * in kernel/sched.c builds the same frame for a task that has not run
 * yet.  The two must be changed together.
 */

#include <avr/io.h>

	.macro	save_context
	push	r0
	in	r0, _SFR_IO_ADDR(SREG)
	push	r0
	push	r1
	clr	r1
	.irp	reg, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
	push	r\reg
	.endr
	.endm

	.macro	restore_context
	.irp	reg, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2
	pop	r\reg
	.endr
	pop	r1
	pop	r0
	out	_SFR_IO_ADDR(SREG), r0
	pop	r0
	.endm

	/* current->sp = SP */
	.macro	store_sp
	lds	r26, current
	lds	r27, current + 1
	in	r0, _SFR_IO_ADDR(SPL)
	st	X+, r0
	in	r0, _SFR_IO_ADDR(SPH)
	st	X, r0
	.endm

	/*
	 * SP = current->sp.  The two halves are written separately, which
	 * is only safe because the I flag is clear at every use.
	 */
	.macro	load_sp
	lds	r26, current
	lds	r27, current + 1
	ld	r0, X+
	out	_SFR_IO_ADDR(SPL), r0
	ld	r0, X
	out	_SFR_IO_ADDR(SPH), r0
	.endm

	.text

	.global	__vector_17
__vector_17:
	save_context
	store_sp
	call	timer_interrupt
	load_sp
	restore_context
	reti

	.global	arch_start_first_task
arch_start_first_task:
	load_sp
	restore_context
	reti
