// SPDX-License-Identifier: GPL-2.0-only
#include <leonix/jiffies.h>
#include <leonix/sched.h>
#include <leonix/stdio.h>
#include <leonix/unistd.h>

#include "bin.h"

#define PERCENT		100

/*
 * The idle share divides by the uptime's hundredth, rounded up, since
 * idle ticks times 100 would overflow 32 bits after 12 hours.  Rounding
 * up keeps the share at or below 100%; rounding down gave 100% on a
 * board that was 99.9% idle.
 */
int uptime_main(int argc, char **argv)
{
	uint32_t now = get_jiffies();
	uint32_t idle = sched_idle_ticks();
	uint32_t hundredth = (now + PERCENT - 1) / PERCENT;

	(void)argc;
	(void)argv;
	dprintf(STDOUT_FILENO, "up %lu s, idle %lu%%\n", now / HZ,
		hundredth ? idle / hundredth : 0);
	return 0;
}
