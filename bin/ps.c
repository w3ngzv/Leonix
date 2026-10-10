// SPDX-License-Identifier: GPL-2.0-only
#include <avr/pgmspace.h>

#include <leonix/sched.h>
#include <leonix/stdio.h>
#include <leonix/unistd.h>

#include "bin.h"

static const char state_names[][2] PROGMEM = {
	[TASK_RUNNABLE] = "R",
	[TASK_SLEEPING] = "S",
	[TASK_BLOCKED] = "B",
	[TASK_DEAD] = "D",
};

/* One line per task: index, state as in Linux's ps, untouched stack. */
int ps_main(int argc, char **argv)
{
	uint8_t i, state;

	(void)argc;
	(void)argv;
	dprintf(STDOUT_FILENO, "ID S FREE\n");
	for (i = 0; (state = sched_task_state(i)) != SCHED_NO_STATE; i++)
		dprintf(STDOUT_FILENO, "%2u %S %4u\n", i, state_names[state],
			sched_stack_free(i));
	dprintf(STDOUT_FILENO, "id R %4u\n", sched_stack_free(SCHED_IDLE_TASK));
	return 0;
}
