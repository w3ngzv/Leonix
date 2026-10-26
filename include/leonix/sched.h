/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_SCHED_H
#define _LEONIX_SCHED_H

#include <stdint.h>

#define MAX_TASKS	3

/*
 * sched_stack_free() arguments and result outside the task table
 * sched_stack_free() 在任务表之外的参数与返回值
 */
#define SCHED_IDLE_TASK	0xff
#define SCHED_NO_TASK	0xffff
#define SCHED_NO_STATE	0xff

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

enum task_state {
	TASK_RUNNABLE,
	TASK_SLEEPING,		/* until wake_at or woken; 到期或被唤醒 */
	TASK_BLOCKED,		/* until woken; 只能被唤醒 */
	TASK_DEAD,
};

struct task {
	/*
	 * must stay first, switch.S stores through it
	 * 必须在首位, switch.S 经由该成员保存 SP
	 */
	uint8_t *sp;
	/*
	 * lowest address, holds the canary
	 * 栈的最低地址, 存放金丝雀
	 */
	uint8_t *stack;
	/*
	 * jiffies, valid while TASK_SLEEPING
	 * 唤醒时刻, 以 jiffies 计, 只在 TASK_SLEEPING 时有效
	 */
	uint32_t wake_at;
	uint8_t state;
};

extern struct task *current;

int task_create(void (*entry)(void), uint8_t *stack, uint16_t size);
_Noreturn void sched_start(void);
void scheduler_tick(void);
void schedule(void);
void sleep_until(uint32_t deadline);
void msleep(uint16_t ms);
uint32_t sched_idle_ticks(void);
uint16_t sched_stack_free(uint8_t index);
uint8_t sched_task_state(uint8_t index);

/*
 * arch/avr/switch.S, for kernel/sched.c only
 * 定义在 arch/avr/switch.S, 只供 kernel/sched.c 使用
 */
_Noreturn void arch_start_first_task(void);
void arch_yield(void);

#endif /* _LEONIX_SCHED_H */
