// SPDX-License-Identifier: GPL-2.0-only
/*
 * Round-robin scheduler with sleeping.
 *
 * A task runs until the next tick or until it calls sleep_until(),
 * whichever comes first.  A sleeping task is skipped until its wake-up
 * time; when every task sleeps, the idle task puts the CPU into Idle
 * mode until the next interrupt.
 */
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/atomic.h>

#include <leonix/jiffies.h>
#include <leonix/panic.h>
#include <leonix/sched.h>

/* Number of registers r1..r31 in the frame below r0 and SREG. */
#define NR_SAVED_GPRS	31

/*
 * Written at the lowest two bytes of every stack.  The stack grows down,
 * so these are the last bytes an overflow reaches before it leaves the
 * stack.  The value is the top half of Linux's STACK_END_MAGIC.
 */
#define STACK_CANARY_LO	0xac
#define STACK_CANARY_HI	0x57

#define IDLE_STACK_SIZE	72

/* idle() itself needs task_exit()'s return address and a 2-byte frame. */
_Static_assert(IDLE_STACK_SIZE >= TASK_STACK_RESERVE + 4,
	       "idle stack too small for the switch frame");

static struct task tasks[MAX_TASKS];
static uint8_t nr_tasks;
static uint8_t current_index;

static struct task idle_task;
static uint8_t idle_stack[IDLE_STACK_SIZE];
static uint32_t idle_ticks;

struct task *current;

extern void arch_start_first_task(void) __attribute__((noreturn));
extern void arch_yield(void);

/* An entry function that returns lands here, see task_init(). */
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
 */
static void task_init(struct task *t, void (*entry)(void),
		      uint8_t *stack, uint16_t size)
{
	uint16_t exit_pc = (uint16_t)task_exit;
	uint16_t entry_pc = (uint16_t)entry;
	uint8_t *sp = stack + size - 1;
	uint8_t i;

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

	stack[0] = STACK_CANARY_LO;
	stack[1] = STACK_CANARY_HI;

	t->sp = sp;
	t->stack = stack;
	t->state = TASK_RUNNABLE;
}

/* Returns 0, or -1 if the task table is full or @size is too small. */
int task_create(void (*entry)(void), uint8_t *stack, uint16_t size)
{
	if (nr_tasks == MAX_TASKS || size < TASK_STACK_RESERVE)
		return -1;

	task_init(&tasks[nr_tasks], entry, stack, size);
	nr_tasks++;
	return 0;
}

/*
 * The outgoing task has just been saved on its own stack, so this is
 * the deepest that stack has been since the last check, give or take
 * what the task used and released in between.
 */
static void check_stack(const struct task *t)
{
	if (t->stack[0] != STACK_CANARY_LO || t->stack[1] != STACK_CANARY_HI)
		panic();
}

/*
 * Pick the next runnable task after the current one, waking any sleeper
 * whose time has come on the way.  The current task is looked at last,
 * so it keeps the CPU only when no other task can run.
 *
 * Called with interrupts disabled, either from the tick or from
 * arch_yield(), with the outgoing task already saved.
 */
void schedule(void)
{
	uint32_t now = get_jiffies();
	struct task *t;
	uint8_t i = current_index;
	uint8_t n;

	check_stack(current);

	for (n = 0; n < nr_tasks; n++) {
		if (++i == nr_tasks)
			i = 0;
		t = &tasks[i];
		if (t->state == TASK_SLEEPING && time_after_eq(now, t->wake_at))
			t->state = TASK_RUNNABLE;
		if (t->state == TASK_RUNNABLE) {
			current_index = i;
			current = t;
			return;
		}
	}
	current = &idle_task;
}

/*
 * Called from the tick interrupt.  The tick lands on whichever task is
 * running at that instant, so counting the ticks that land on the idle
 * task samples how much of the time the CPU had nothing to do.
 */
void scheduler_tick(void)
{
	if (current == &idle_task)
		idle_ticks++;
	schedule();
}

/* Ticks that found the CPU idle since sched_start(). */
uint32_t sched_idle_ticks(void)
{
	uint32_t ticks;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		ticks = idle_ticks;
	}
	return ticks;
}

/*
 * Sleep until jiffies reaches @deadline.  An absolute deadline lets a
 * periodic task add its period each round without drifting.
 *
 * Interrupts are disabled while the task is marked, so the tick cannot
 * see a half-written wake_at.  The call returns with interrupts enabled
 * whatever the caller had, since arch_yield() returns through reti.
 * Only a task may call it, after sched_start().
 */
void sleep_until(uint32_t deadline)
{
	cli();
	current->wake_at = deadline;
	current->state = TASK_SLEEPING;
	arch_yield();
}

/*
 * Sleep for at least @ms milliseconds.  The current tick may be about
 * to end, so one more is added.
 */
void msleep(uint16_t ms)
{
	sleep_until(get_jiffies() + ms + 1);
}

/*
 * Runs only when every other task sleeps.  In Idle mode the
 * CPU stops but Timer1 keeps counting (datasheet 7.1), and the next tick
 * wakes it into the interrupt that may switch to a woken task.
 */
static void idle(void)
{
	for (;;)
		sleep_mode();
}

/*
 * Leaves the boot stack behind for good: main() never runs again once
 * the first task is entered.
 */
void sched_start(void)
{
	cli();
	set_sleep_mode(SLEEP_MODE_IDLE);
	task_init(&idle_task, idle, idle_stack, sizeof(idle_stack));

	current_index = 0;
	current = nr_tasks ? &tasks[0] : &idle_task;
	arch_start_first_task();
}
