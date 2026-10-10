/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_WAIT_H
#define _LEONIX_WAIT_H

#include <stdint.h>
#include <avr/interrupt.h>

#include <leonix/jiffies.h>

/*
 * Tasks waiting for an event, one bit per entry of the task table.  With
 * at most MAX_TASKS tasks a byte holds every waiter, and adding, removing
 * and waking need no list.  The idle task never waits.
 */
struct wait_queue {
	volatile uint8_t waiters;
};

#define WAIT_QUEUE_INIT	{ 0 }

uint8_t wake_up(struct wait_queue *wq);
void __wait(struct wait_queue *wq, uint32_t deadline, uint8_t timed);

/*
 * Sleep until @cond holds.  @cond is evaluated with interrupts disabled,
 * so an interrupt handler that makes it true and then calls wake_up()
 * cannot slip in between the test and the sleep, and a @cond with a side
 * effect, such as taking a lock, is atomic.  Only a task may wait.
 */
#define wait_event(wq, cond)						\
	do {								\
		for (;;) {						\
			cli();						\
			if (cond)					\
				break;					\
			__wait((wq), 0, 0);				\
		}							\
		sei();							\
	} while (0)

/*
 * As wait_event(), giving up after at least @ms milliseconds.  Evaluates
 * to 0 once @cond holds, or to -1 on timeout.  The clock is read before
 * @cond each round, the order 97dc0e9 settled for the I2C timeouts.
 */
#define wait_event_timeout(wq, cond, ms)				\
	({								\
		uint32_t __deadline = get_jiffies() + (ms) + 1;		\
		int __ret = 0;						\
		for (;;) {						\
			uint8_t __late;					\
									\
			cli();						\
			__late = time_after_eq(get_jiffies(), __deadline); \
			if (cond)					\
				break;					\
			if (__late) {					\
				__ret = -1;				\
				break;					\
			}						\
			__wait((wq), __deadline, 1);			\
		}							\
		sei();							\
		__ret;							\
	})

#endif /* _LEONIX_WAIT_H */
