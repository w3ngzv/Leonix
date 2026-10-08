// SPDX-License-Identifier: GPL-2.0-only
/*
 * Round-robin scheduler.  Every timer tick moves to the next task, so a
 * task that never gives up the CPU still only gets one tick at a time.
 */
#include <avr/interrupt.h>

#include <leonix/sched.h>

/* Number of registers r1..r31 in the frame below r0 and SREG. */
#define NR_SAVED_GPRS	31

static struct task tasks[MAX_TASKS];
static uint8_t nr_tasks;
static uint8_t current_index;

struct task *current;

extern void arch_start_first_task(void) __attribute__((noreturn));

/* An entry function that returns lands here, see task_create(). */
static void task_exit(void)
{
	cli();
	for (;;)
		;
}

/*
 * Lay out @stack so that the restore path in switch.S, followed by reti,
 * enters @entry with interrupts enabled.  From the top of the stack
 * down: the address of task_exit() as @entry's return address, @entry
 * itself as the address reti returns to, then r0, SREG and r1..r31.
 * The return addresses are stored high byte at the lower address, the
 * order CALL leaves them in.
 *
 * Returns 0, or -1 if the task table is full or @size is too small.
 */
int task_create(void (*entry)(void), uint8_t *stack, uint16_t size)
{
	uint16_t exit_pc = (uint16_t)task_exit;
	uint16_t entry_pc = (uint16_t)entry;
	struct task *t;
	uint8_t *sp;
	uint8_t i;

	if (nr_tasks == MAX_TASKS || size < TASK_STACK_RESERVE)
		return -1;

	t = &tasks[nr_tasks];
	sp = stack + size - 1;

	*sp-- = exit_pc & 0xff;
	*sp-- = exit_pc >> 8;
	*sp-- = entry_pc & 0xff;
	*sp-- = entry_pc >> 8;
	*sp-- = 0;			/* r0 */
	/*
	 * SREG with I clear, the same as every frame saved inside the
	 * interrupt.  reti sets I itself.  Setting it here would enable
	 * interrupts one pop early.
	 */
	*sp-- = 0;
	for (i = 0; i < NR_SAVED_GPRS; i++)
		*sp-- = 0;		/* r1..r31, r1 must be zero for C */

	t->sp = sp;
	nr_tasks++;
	return 0;
}

/* Called from the tick interrupt with the outgoing task already saved. */
void schedule(void)
{
	current_index++;
	if (current_index == nr_tasks)
		current_index = 0;
	current = &tasks[current_index];
}

/*
 * Leaves the boot stack behind for good: main() never runs again once
 * the first task is entered.
 */
void sched_start(void)
{
	cli();
	if (nr_tasks == 0)
		for (;;)
			;

	current_index = 0;
	current = &tasks[0];
	arch_start_first_task();
}
