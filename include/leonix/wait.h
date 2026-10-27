/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_WAIT_H
#define _LEONIX_WAIT_H

#include <stdint.h>

#include <leonix/irqflags.h>
#include <leonix/jiffies.h>

/*
 * Tasks waiting for an event, one bit per entry of the task table.  With
 * at most MAX_TASKS tasks a byte holds every waiter, and adding, removing
 * and waking need no list.  The idle task never waits.
 *
 * 等待某一事件的任务, 任务表中每项一位. 任务至多 MAX_TASKS 个, 一个字节即可容纳
 * 全部等待者, 加入、移除与唤醒都不需要链表. 空闲任务从不等待.
 */
struct wait_queue {
	volatile uint8_t waiters;
};

#define WAIT_QUEUE_INIT	{ 0 }

uint8_t wake_up(struct wait_queue *wq);
void __wait(struct wait_queue *wq, uint32_t deadline, uint8_t timed);

/**
 * wait_event() - sleep until a condition holds
 * @wq: the queue to wait on
 * @cond: the condition
 *
 * @cond is evaluated with interrupts disabled, so an interrupt handler
 * that makes it true and then calls wake_up() cannot slip in between the
 * test and the sleep, and a @cond with a side effect, such as taking a
 * lock, is atomic.
 *
 * 在 @wq 上睡眠, 直到 @cond 成立. @cond 在关中断状态下求值, 因此使其成立并调用
 * wake_up() 的中断处理程序无法插入在测试与睡眠之间, 带副作用的 @cond (例如获取
 * 锁) 也是原子的.
 *
 * Context: a task.
 * 在任务中调用.
 */
#define wait_event(wq, cond)						\
	do {								\
		for (;;) {						\
			local_irq_disable();				\
			if (cond)					\
				break;					\
			__wait((wq), 0, 0);				\
		}							\
		local_irq_enable();					\
	} while (0)

/**
 * wait_event_timeout() - sleep until a condition holds or time runs out
 * @wq: the queue to wait on
 * @cond: the condition
 * @ms: milliseconds to wait at least
 *
 * As wait_event().  The clock is read before @cond each round, the order
 * 97dc0e9 settled for the I2C timeouts.
 *
 * 与 wait_event() 相同, 但至少等待 @ms 毫秒后放弃. 每轮先读时钟再求 @cond, 这是
 * 97dc0e9 为 I2C 超时确定的顺序.
 *
 * Context: a task.
 * 在任务中调用.
 * Return: 0 once @cond holds, or -1 on timeout.
 * @cond 成立时为 0, 超时为 -1.
 */
#define wait_event_timeout(wq, cond, ms)				\
	({								\
		uint32_t __deadline = get_jiffies() + (ms) + 1;		\
		int __ret = 0;						\
		for (;;) {						\
			uint8_t __late;					\
									\
			local_irq_disable();				\
			__late = time_after_eq(get_jiffies(), __deadline); \
			if (cond)					\
				break;					\
			if (__late) {					\
				__ret = -1;				\
				break;					\
			}						\
			__wait((wq), __deadline, 1);			\
		}							\
		local_irq_enable();					\
		__ret;							\
	})

#endif /* _LEONIX_WAIT_H */
