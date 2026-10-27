/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_SCHED_H
#define _LEONIX_SCHED_H

#include <stdint.h>

#include <asm/sched.h>

#define MAX_TASKS	3

/*
 * sched_stack_free() arguments and result outside the task table
 * sched_stack_free() 在任务表之外的参数与返回值
 */
#define SCHED_IDLE_TASK	0xff
#define SCHED_NO_TASK	0xffff
#define SCHED_NO_STATE	0xff

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
uint8_t sched_current(void);

/*
 * in arch/<arch>/, for kernel/sched.c only; asm/sched.h has the rest
 * 定义在 arch/<arch>/ 中, 只供 kernel/sched.c 使用; 其余见 asm/sched.h
 */
uint8_t *arch_task_frame(uint8_t *top, void (*entry)(void),
			 void (*exit)(void));
_Noreturn void arch_start_first_task(void);
void arch_yield(void);

#endif /* _LEONIX_SCHED_H */
