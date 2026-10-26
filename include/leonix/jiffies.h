/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_JIFFIES_H
#define _LEONIX_JIFFIES_H

#include <stdint.h>

#define HZ	1000

void time_init(void);
void timer_interrupt(void);
uint32_t get_jiffies(void);

/**
 * time_after_eq() - whether a tick count has reached a deadline
 * @now: jiffies
 * @deadline: jiffies
 *
 * @now 达到或越过 @deadline 时为真, 跨越回绕时同样成立.
 *
 * Return: 1 once @now has reached or passed @deadline, across
 * wraparound, else 0.
 * @now 达到或越过 @deadline 时返回 1, 否则返回 0.
 */
static inline int time_after_eq(uint32_t now, uint32_t deadline)
{
	return (int32_t)(now - deadline) >= 0;
}

#endif /* _LEONIX_JIFFIES_H */
