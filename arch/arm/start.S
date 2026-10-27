/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Reset entry and vector table for the STM32F103.
 *
 * The core loads SP from the first word of the table and starts at the
 * second (RM0008 3.4).  It comes out of reset in Thread mode on MSP
 * with PRIMASK clear (PM0056 2.1.3); the first instruction sets
 * PRIMASK, so that no interrupt is taken before sched_start(), as on
 * the AVR.  setup_arch() then sets the clock before main().
 *
 * STM32F103 的复位入口与向量表.
 *
 * 内核从向量表的第一个字装入 SP, 从第二个字开始执行 (RM0008 3.4). 复位后处于
 * Thread 模式、使用 MSP、PRIMASK 为 0 (PM0056 2.1.3); 第一条指令置位 PRIMASK,
 * 使 sched_start() 之前不响应任何中断, 与 AVR 相同. 随后 setup_arch() 在
 * main() 之前设置时钟.
 */

#include <leonix/panic.h>

	.syntax	unified
	.cpu	cortex-m3
	.thumb

/*
 * IRQs of the medium-density STM32F103, RM0008 table 63 lists 0 to 42;
 * 60 entries cover the other STM32F10xxx devices of that table too
 * 中容量 STM32F103 的 IRQ, RM0008 表 63 列出 0 至 42; 60 项也覆盖了该表中的
 * 其他 STM32F10xxx 器件
 */
#define NR_IRQS		60

	/*
	 * an entry that falls back to bad_interrupt
	 * 未定义时取 bad_interrupt
	 */
	.macro	vector name
	.weak	\name
	.thumb_set \name, bad_interrupt
	.word	\name
	.endm

	.section .vectors, "a", %progbits
	.global	__vectors
__vectors:
	.word	__boot_stack_top
	.word	reset_handler
	vector	nmi_handler
	.word	fault_entry			/* HardFault */
	.word	fault_entry			/* MemManage */
	.word	fault_entry			/* BusFault */
	.word	fault_entry			/* UsageFault */
	.word	0, 0, 0, 0
	vector	svc_handler
	vector	debugmon_handler
	.word	0
	.word	pendsv_handler
	.word	systick_handler
	.irp	n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59
	vector	irq\n\()_handler
	.endr
	.if	(. - __vectors) != (16 + NR_IRQS) * 4
	.error	"vector table size does not match NR_IRQS"
	.endif

	.text

	.thumb_func
	.global	reset_handler
reset_handler:
	cpsid	i

	/* copy .data from flash; 从 flash 复制 .data */
	ldr	r0, =__data_start
	ldr	r1, =__data_end
	ldr	r2, =__data_load_start
	b	2f
1:	ldr	r3, [r2], #4
	str	r3, [r0], #4
2:	cmp	r0, r1
	blo	1b

	/* zero .bss; 清零 .bss */
	ldr	r0, =__bss_start
	ldr	r1, =__bss_end
	movs	r2, #0
	b	4f
3:	str	r2, [r0], #4
4:	cmp	r0, r1
	blo	3b

	bl	setup_arch
	bl	main
5:	b	5b

/*
 * Any vector without a handler ends up here.  IPSR holds the exception
 * number (PM0056 2.1.3), which panic_halt() reports.
 *
 * 所有没有处理函数的向量都到达此处. IPSR 保存异常号 (PM0056 2.1.3),
 * panic_halt() 报告该值.
 */
	.thumb_func
	.global	bad_interrupt
bad_interrupt:
	mrs	r0, ipsr
	ldr	r1, =panic_ipsr
	str	r0, [r1]
	movs	r0, #PANIC_BAD_INTERRUPT
	movs	r1, #PANIC_NO_TASK
	b	panic

/*
 * HardFault, MemManage, BusFault and UsageFault.  Bit 2 of EXC_RETURN
 * in LR tells which stack the exception frame is on (PM0056 2.3.7);
 * fault_report() takes the frame and EXC_RETURN.  R4 to R11 are not in
 * the frame and still hold the faulting code's values, so they are
 * stored first, into fault_callee rather than onto a stack that may be
 * the one at fault.
 *
 * HardFault、MemManage、BusFault 与 UsageFault. LR 中 EXC_RETURN 的第 2 位指出
 * 异常栈帧位于哪个栈 (PM0056 2.3.7); fault_report() 取得栈帧与 EXC_RETURN.
 * R4 至 R11 不在栈帧中, 仍保存出错代码的值, 因此先将其存入 fault_callee;
 * 出错的可能正是某个栈, 所以不压入栈中.
 */
	.thumb_func
	.global	fault_entry
fault_entry:
	ldr	r0, =fault_callee
	stmia	r0, {r4-r11}
	tst	lr, #4
	ite	eq
	mrseq	r0, msp
	mrsne	r0, psp
	mov	r1, lr
	b	fault_report

/*
 * void panic(uint8_t reason, uint8_t task)
 *
 * As on the AVR, the caller's stack may be the one that overflowed, so
 * SP becomes the top of the boot stack before panic_halt() runs any C.
 * In Thread mode CONTROL is cleared so that MSP is the stack in use; in
 * Handler mode MSP is in use already and the write has no effect
 * (PM0056 2.1.3).  The arguments stay in r0 and r1.
 *
 * 与 AVR 相同, 调用者的栈可能正是溢出的那个栈, 因此在 panic_halt() 执行任何 C
 * 代码之前把 SP 设为启动栈的栈顶. 在 Thread 模式下清零 CONTROL, 使当前栈为 MSP;
 * 在 Handler 模式下本来就使用 MSP, 该写入不起作用 (PM0056 2.1.3). 参数保留在
 * r0 与 r1 中.
 */
	.thumb_func
	.global	panic
panic:
	cpsid	i
	ldr	r2, =__boot_stack_top
	msr	msp, r2
	movs	r2, #0
	msr	control, r2
	isb
	b	panic_halt
