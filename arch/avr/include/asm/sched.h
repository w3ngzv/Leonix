/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_AVR_SCHED_H
#define _ASM_AVR_SCHED_H

#include <avr/sleep.h>

/*
 * Bytes a task stack must hold beyond the task's own use.  Both ways
 * into schedule() run on the outgoing task's stack: the tick pushes the
 * PC, switch.S the 33-byte frame, then timer_interrupt() tail-jumps
 * through scheduler_tick() into schedule(); arch_yield() is entered by
 * a call and builds the same frame.  With avr-gcc 9.5.0 -Os either path
 * peaks at 55 bytes, in get_jiffies() called from schedule().  The
 * 2-byte canary brings it to 57, rounded up here.  Another compiler may
 * need a different value.
 *
 * 任务栈在任务自身用量之外必须容纳的字节数. 进入 schedule() 的两条路径都运行在
 * 切出任务的栈上: 节拍中断压入 PC, switch.S 压入 33 字节的栈帧, 然后
 * timer_interrupt() 经 scheduler_tick() 尾跳转进入 schedule(); arch_yield() 以
 * call 进入, 构造同样的栈帧. avr-gcc 9.5.0 -Os 下两条路径的峰值都是 55 字节, 出
 * 现在 schedule() 调用的 get_jiffies() 中. 加上 2 字节的金丝雀为 57 字节, 此处
 * 向上取整. 换用其他编译器可能需要不同的值.
 */
#define TASK_STACK_RESERVE	64

/*
 * Puts a stack in .noinit, above the boot key, see memlayout.h.  avr-gcc
 * gives .noinit sections no contents, so they cost no flash, and the
 * start code does not zero them; task_create() fills the stack anyway.
 *
 * 把栈放在 .noinit 中, 位于启动键之上, 见 memlayout.h. avr-gcc 不为 .noinit 段
 * 生成内容, 因此不占 flash, 启动代码也不清零; 无论如何 task_create() 都会填充
 * 栈.
 */
#define __task_stack	__attribute__((section(".noinit.stacks")))

/*
 * idle() needs task_exit()'s return address and a 2-byte frame
 * idle() 需要 task_exit() 的返回地址与 2 字节的栈帧
 */
#define IDLE_STACK_SIZE		72

_Static_assert(TASK_STACK_RESERVE + 4 <= IDLE_STACK_SIZE,
	       "idle stack too small for the switch frame");

/*
 * Sized from what sched_stack_free() reads on the board, with the
 * measurements in Documentation/notes/scheduler.rst.  Recheck them with
 * ps after any change to a task.
 *
 * 栈的大小依据 sched_stack_free() 在板上的读数确定, 测量值见
 * Documentation/notes/scheduler.rst. 修改任务之后用 ps 重新核对.
 */
#define STATUS_STACK_SIZE	224
#define INIT_STACK_SIZE		318

/*
 * Idle mode stops the CPU but not Timer1 (datasheet 7.1), so the next
 * tick wakes it.
 *
 * Idle 模式下 CPU 停止而 Timer1 继续计数 (数据手册 7.1), 下一个节拍把 CPU
 * 唤醒.
 */
static inline void arch_cpu_idle_prepare(void)
{
	set_sleep_mode(SLEEP_MODE_IDLE);
}

static inline void arch_cpu_idle(void)
{
	sleep_mode();
}

#endif /* _ASM_AVR_SCHED_H */
