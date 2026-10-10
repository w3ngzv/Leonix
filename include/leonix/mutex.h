/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_MUTEX_H
#define _LEONIX_MUTEX_H

#include <leonix/wait.h>

/*
 * A sleeping lock for tasks.  Interrupt handlers must not take it, since
 * they cannot sleep.
 */
struct mutex {
	volatile uint8_t locked;
	struct wait_queue wq;
};

#define MUTEX_INIT	{ 0, WAIT_QUEUE_INIT }

/* The test and the set run with interrupts disabled inside wait_event(). */
static inline void mutex_lock(struct mutex *m)
{
	wait_event(&m->wq, !m->locked && (m->locked = 1));
}

/*
 * Every waiter is woken and they race for the lock; the losers wait
 * again.  With four tasks at most, a queue of turns would cost more than
 * it saves.
 */
static inline void mutex_unlock(struct mutex *m)
{
	m->locked = 0;
	wake_up(&m->wq);
}

#endif /* _LEONIX_MUTEX_H */
