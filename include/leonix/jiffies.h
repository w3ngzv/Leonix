/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_JIFFIES_H
#define _LEONIX_JIFFIES_H

#include <stdint.h>

#define HZ	1000

void time_init(void);
uint32_t get_jiffies(void);

/* True once jiffies has reached or passed @deadline, across wraparound. */
static inline int time_after_eq(uint32_t now, uint32_t deadline)
{
	return (int32_t)(now - deadline) >= 0;
}

#endif /* _LEONIX_JIFFIES_H */
