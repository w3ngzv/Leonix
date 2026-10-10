/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_SCHED_H
#define _LEONIX_SCHED_H

#include <stdint.h>

#define MAX_TASKS	4

/* sched_stack_free() arguments and result outside the task table. */
#define SCHED_IDLE_TASK	0xff
#define SCHED_NO_TASK	0xffff

/*
 * Bytes a task stack must hold beyond the task's own use.  Both ways
 * into schedule() run on the outgoing task's stack: the tick pushes the
 * PC, switch.S the 33-byte frame, then timer_interrupt() tail-jumps
 * through scheduler_tick() into schedule(); arch_yield() is entered by
 * a call and builds the same frame.  With avr-gcc 9.5.0 -Os either path
 * peaks at 55 bytes, in get_jiffies() called from schedule().  The
 * 2-byte canary brings it to 57, rounded up here.  Another compiler may
 * need a different value.
 */
#define TASK_STACK_RESERVE	64

enum task_state {
	TASK_RUNNABLE,
	TASK_SLEEPING,		/* until wake_at, or until woken */
	TASK_BLOCKED,		/* until woken, no deadline */
	TASK_DEAD,
};

struct task {
	uint8_t *sp;		/* must stay first, switch.S stores through it */
	uint8_t *stack;		/* lowest address, holds the canary */
	uint32_t wake_at;	/* jiffies, valid while TASK_SLEEPING */
	uint8_t state;
};

extern struct task *current;

int task_create(void (*entry)(void), uint8_t *stack, uint16_t size);
void sched_start(void) __attribute__((noreturn));
void scheduler_tick(void);
void schedule(void);
void sleep_until(uint32_t deadline);
void msleep(uint16_t ms);
uint32_t sched_idle_ticks(void);
uint16_t sched_stack_free(uint8_t index);

#endif /* _LEONIX_SCHED_H */
