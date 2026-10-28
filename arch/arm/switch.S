/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Context switch on SysTick and PendSV, and the start of the first task.
 *
 * Tasks run in Thread mode on PSP.  On exception entry the core pushes
 * r0-r3, r12, lr, pc and xPSR onto PSP (PM0056 2.3.7); save_context
 * pushes r4-r11 below them and stores PSP in current->sp, so the
 * handler's C code runs on MSP and leaves the task stack alone.
 * arch_task_frame() in arch/arm/process.c builds the same 64-byte frame
 * for a task that has not run yet; the two must change together.
 *
 * SysTick, PendSV and the USART1 interrupt share the lowest priority
 * (setup_arch()), so none preempts another and each returns to Thread
 * mode: a switch inside one of them never leaves a handler half done.
 * The SysTick handler calls schedule() through timer_interrupt(), as on
 * the AVR; any other code that wants a switch pends PendSV.
 *
 * 在 SysTick 与 PendSV 中切换上下文, 并启动第一个任务.
 *
 * 任务在 Thread 模式下使用 PSP. 异常进入时内核把 r0-r3、r12、lr、pc 与 xPSR
 * 压入 PSP (PM0056 2.3.7); save_context 在其下压入 r4-r11, 并把 PSP 存入
 * current->sp, 处理程序的 C 代码因此运行在 MSP 上, 不触及任务栈.
 * arch/arm/process.c 的 arch_task_frame() 为尚未运行的任务构造同样的 64 字节
 * 栈帧, 两者必须一起修改.
 *
 * SysTick、PendSV 与 USART1 中断共用最低优先级 (setup_arch()), 互不抢占, 每个
 * 都返回 Thread 模式: 在其中切换任务, 不会留下执行了一半的处理程序. SysTick
 * 处理程序与 AVR 相同, 经 timer_interrupt() 调用 schedule(); 其他需要切换的代码
 * 挂起 PendSV.
 */

	.syntax	unified
	.cpu	cortex-m3
	.thumb

/*
 * word offsets in the frame, from current->sp up
 * 栈帧中的字偏移, 自 current->sp 向上
 */
#define FRAME_LR	13
#define FRAME_PC	14
#define FRAME_WORDS	16

#define CONTROL_SPSEL	2

	/* current->sp = PSP after pushing r4-r11; 压入 r4-r11 后保存 PSP */
	.macro	save_context
	mrs	r0, psp
	stmdb	r0!, {r4-r11}
	ldr	r1, =current
	ldr	r1, [r1]
	str	r0, [r1]
	.endm

	.macro	restore_context
	ldr	r1, =current
	ldr	r1, [r1]
	ldr	r0, [r1]
	ldmia	r0!, {r4-r11}
	msr	psp, r0
	.endm

	/*
	 * r0 is pushed with LR, which holds EXC_RETURN, to keep MSP 8-byte
	 * aligned for the C call (AAPCS)
	 * r0 与保存 EXC_RETURN 的 LR 一起压栈, 使 MSP 在调用 C 函数时保持
	 * 8 字节对齐 (AAPCS)
	 */
	.macro	switch_through fn
	save_context
	push	{r0, lr}
	bl	\fn
	pop	{r0, lr}
	restore_context
	bx	lr
	.endm

	.text

	.thumb_func
	.global	systick_handler
systick_handler:
	switch_through timer_interrupt

	.thumb_func
	.global	pendsv_handler
pendsv_handler:
	switch_through schedule

/*
 * Enter current from the frame arch_task_frame() built, without an
 * exception return: MSP goes back to the top of the boot stack, which
 * from now on serves the handlers, PSP is set past the frame and
 * becomes the stack in use, and the entry function is called with LR
 * set to task_exit() as in the frame.  The stacked PC has bit 0 clear;
 * bx needs it set.
 *
 * 不经异常返回, 从 arch_task_frame() 构造的栈帧进入 current: MSP 回到启动栈的
 * 栈顶, 此后供处理程序使用; PSP 设在栈帧之上并成为当前栈; 以栈帧中的
 * task_exit() 为 LR 调用入口函数. 栈帧中的 PC 第 0 位为 0, bx 要求该位为 1.
 */
	.thumb_func
	.global	arch_start_first_task
arch_start_first_task:
	ldr	r0, =__boot_stack_top
	msr	msp, r0
	ldr	r1, =current
	ldr	r1, [r1]
	ldr	r0, [r1]
	ldr	lr, [r0, #FRAME_LR * 4]
	ldr	r2, [r0, #FRAME_PC * 4]
	adds	r0, #FRAME_WORDS * 4
	msr	psp, r0
	movs	r1, #CONTROL_SPSEL
	msr	control, r1
	isb
	orr	r2, r2, #1
	cpsie	i
	bx	r2
