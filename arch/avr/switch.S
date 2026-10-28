/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Context switch on the Timer1 compare match A interrupt, the TWI and
 * USB interrupts and arch_yield().
 *
 * The interrupt has already pushed the PC.  The frame below it is r0,
 * SREG, r1, r2 ... r31, from high address to low, and arch_task_frame()
 * in arch/avr/process.c builds the same frame for a task that has not run
 * yet.  The two must be changed together.
 *
 * 在 Timer1 比较匹配 A 中断、TWI 与 USB 中断以及 arch_yield() 中切换上下文.
 *
 * 中断已经压入 PC. PC 之下的栈帧自高地址向低地址依次为 r0、SREG、r1、r2 ...
 * r31; arch/avr/process.c 中的 arch_task_frame() 为尚未运行的任务构造同样的栈
 * 帧. 两者必须一起修改.
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

	/* current->sp = SP; 保存 SP */
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
	 *
	 * SP = current->sp. 两个半字分开写入, 这只因每处使用时 I 标志都已清除才
	 * 是安全的.
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

#ifdef CONFIG_I2C
	/*
	 * TWI interrupt.  twi_interrupt() returns non-zero when it has woken
	 * a task; switching right away lets the I2C caller run on before
	 * the next tick instead of up to 1 ms later.  The frame is the
	 * tick's, so either path can resume a task the other saved.
	 * Without CONFIG_I2C the weak default in start.S stays in place.
	 *
	 * TWI 中断. twi_interrupt() 唤醒了任务时返回非零值; 立即切换使 I2C 调用
	 * 者在下一个节拍之前继续运行, 而不必等待至多 1 ms. 栈帧与节拍中断的相
	 * 同, 因此两条路径都能恢复另一条路径保存的任务. 没有 CONFIG_I2C 时保留
	 * start.S 中的弱默认处理.
	 */
	.global	__vector_36
__vector_36:
	save_context
	store_sp
	call	twi_interrupt
	tst	r24
	breq	1f
	call	schedule
1:	load_sp
	restore_context
	reti
#endif

	/*
	 * USB general and endpoint interrupts, the same way as TWI.  The
	 * endpoint handler wakes usb_task(), see arch/avr/usb.c.
	 *
	 * USB general 与 endpoint 中断, 处理方式与 TWI 相同. endpoint 处理函数
	 * 唤醒 usb_task(), 见 arch/avr/usb.c.
	 */
	.global	__vector_10
__vector_10:
	save_context
	store_sp
	call	usb_general_interrupt
	load_sp
	restore_context
	reti

	.global	__vector_11
__vector_11:
	save_context
	store_sp
	call	usb_endpoint_interrupt
	tst	r24
	breq	1f
	call	schedule
1:	load_sp
	restore_context
	reti

	/*
	 * A task giving up the CPU before its tick is over.  The call has
	 * already pushed the return address where the interrupt would have
	 * pushed the PC, so the frame built here is the same as the tick's.
	 * cli comes first so the saved SREG has I clear like every other
	 * frame; reti sets it again on the way out, in whichever task that
	 * turns out to be.
	 *
	 * 任务在节拍结束之前让出 CPU. call 已把返回地址压在中断压入 PC 的位置,
	 * 因此此处构造的栈帧与节拍中断的相同. 先执行 cli, 使保存的 SREG 与其他
	 * 每个栈帧一样 I 为 0; reti 在返回时重新置位 I, 无论返回到哪个任务.
	 */
	.global	arch_yield
arch_yield:
	cli
	save_context
	store_sp
	call	schedule
	load_sp
	restore_context
	reti

	/*
	 * Enter current from the frame arch_task_frame() built.  reti enables
	 * interrupts.
	 *
	 * 从 arch_task_frame() 构造的栈帧进入 current. reti 打开中断.
	 */
	.global	arch_start_first_task
arch_start_first_task:
	load_sp
	restore_context
	reti
