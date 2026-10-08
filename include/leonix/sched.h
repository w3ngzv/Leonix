/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_SCHED_H
#define _LEONIX_SCHED_H

#include <stdint.h>

#define MAX_TASKS	4

/*
 * Bytes a task stack must hold beyond the task's own use: the 33-byte
 * register frame from switch.S, the 2-byte PC pushed by the interrupt,
 * and the calls into timer_interrupt() and schedule() made on that same
 * stack.
 */
#define TASK_STACK_RESERVE	48

struct task {
	uint8_t *sp;	/* must stay first, switch.S stores through it */
};

extern struct task *current;

int task_create(void (*entry)(void), uint8_t *stack, uint16_t size);
void sched_start(void) __attribute__((noreturn));
void schedule(void);

#endif /* _LEONIX_SCHED_H */
