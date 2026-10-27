/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ARM_SCHED_H
#define _ASM_ARM_SCHED_H

/*
 * Bytes a task stack must hold beyond the task's own use.  Tasks run in
 * Thread mode on PSP and every handler on MSP (PM0056 2.1.2), so only
 * the switch frame lands on a task stack: the 32 bytes the exception
 * entry pushes, 4 of alignment padding with STKALIGN set (PM0056 2.3.7),
 * and r4-r11, 32 bytes, from entry.S.  With the 2-byte canary that makes
 * 70; arch_task_frame() may give up to 7 bytes more to align the top.
 *
 * 任务栈在任务自身用量之外必须容纳的字节数. 任务在 Thread 模式下使用 PSP, 所有
 * 处理程序使用 MSP (PM0056 2.1.2), 因此任务栈上只有切换栈帧: 异常进入时压入的
 * 32 字节, STKALIGN 置位时至多 4 字节的对齐填充 (PM0056 2.3.7), 以及 entry.S
 * 压入的 r4-r11, 共 32 字节. 加上 2 字节的金丝雀为 70 字节; arch_task_frame()
 * 为对齐栈顶还可能再用去至多 7 字节.
 */
#define TASK_STACK_RESERVE	80

/*
 * Task stacks go in .noinit, which the start code does not zero;
 * task_create() fills them anyway.  The exception frame wants 8-byte
 * alignment.
 *
 * 任务栈放在 .noinit 中, 启动代码不清零; 无论如何 task_create() 都会填充栈.
 * 异常栈帧要求 8 字节对齐.
 */
#define __task_stack	__attribute__((section(".noinit.stacks"), aligned(8)))

/*
 * idle() is a loop around wfi and pushes nothing of its own
 * idle() 只是围绕 wfi 的循环, 自身不压栈
 */
#define IDLE_STACK_SIZE		96

_Static_assert(TASK_STACK_RESERVE <= IDLE_STACK_SIZE,
	       "idle stack too small for the switch frame");

/*
 * Sized from what sched_stack_free() reads under QEMU, with the
 * measurements in Documentation/stm32f103.rst.  Recheck them with ps
 * after any change to a task.
 *
 * 栈的大小依据 sched_stack_free() 在 QEMU 下的读数确定, 测量值见
 * Documentation/stm32f103.rst. 修改任务之后用 ps 重新核对.
 */
#define STATUS_STACK_SIZE	512
#define INIT_STACK_SIZE		1024

static inline void arch_cpu_idle_prepare(void)
{
}

/*
 * wfi sleeps until an exception, the next SysTick at the latest
 * (PM0056 3.9.12)
 * wfi 睡眠到下一个异常, 最迟到下一个 SysTick (PM0056 3.9.12)
 */
static inline void arch_cpu_idle(void)
{
	__asm__ __volatile__("wfi" ::: "memory");
}

#endif /* _ASM_ARM_SCHED_H */
