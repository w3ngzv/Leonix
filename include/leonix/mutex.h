/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_MUTEX_H
#define _LEONIX_MUTEX_H

#include <leonix/wait.h>

/*
 * A sleeping lock for tasks.  Interrupt handlers must not take it, since
 * they cannot sleep.
 *
 * 供任务使用的睡眠锁. 中断处理程序不得获取该锁, 因为中断处理程序不能睡眠.
 */
struct mutex {
	volatile uint8_t locked;
	struct wait_queue wq;
};

#define MUTEX_INIT	{ 0, WAIT_QUEUE_INIT }

/**
 * mutex_lock() - take a mutex, sleeping until it is free
 * @m: the mutex
 *
 * The test and the set run with interrupts disabled inside wait_event().
 *
 * 获取 @m, 锁被占用时睡眠到锁空闲. 测试与置位都在 wait_event() 内关中断进行.
 *
 * Context: a task.
 * 在任务中调用.
 */
static inline void mutex_lock(struct mutex *m)
{
	wait_event(&m->wq, !m->locked && (m->locked = 1));
}

/**
 * mutex_unlock() - release a mutex
 * @m: the mutex
 *
 * Every waiter is woken and they race for the lock; the losers wait
 * again.  With MAX_TASKS tasks at most, a queue of turns would cost more
 * than it saves.
 *
 * 释放 @m. 所有等待者都被唤醒并竞争该锁, 未获得者继续等待. 任务至多 MAX_TASKS
 * 个, 排队轮转的代价会超过其收益.
 *
 * Context: a task.
 * 在任务中调用.
 */
static inline void mutex_unlock(struct mutex *m)
{
	m->locked = 0;
	wake_up(&m->wq);
}

#endif /* _LEONIX_MUTEX_H */
