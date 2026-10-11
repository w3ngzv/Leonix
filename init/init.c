// SPDX-License-Identifier: GPL-2.0-only
/*
 * The task that runs user space: /bin/sh on the console, started again
 * whenever it ends.  Linux runs /sbin/init this way, or a shell with
 * init=/bin/sh; the kernel itself has no commands.
 */
#include <avr/pgmspace.h>
#include <stddef.h>
#include <string.h>

#include <leonix/exec.h>
#include <leonix/init.h>
#include <leonix/jiffies.h>
#include <leonix/printk.h>
#include <leonix/sched.h>
#include <leonix/unistd.h>

/* A shell that cannot start is tried again this often, not in a loop. */
#define INIT_RETRY_MS	1000

static void init_task(void)
{
	static char sh[] = "sh";
	static char *argv[] = { sh, NULL };
	int status;

	for (;;) {
		status = exec(1, argv);
		printk("sh exited %d", status);
		msleep(INIT_RETRY_MS);
	}
}

/*
 * Open the console as standard input and output, then start the task.
 * Called before sched_start(), after the USB driver has registered the
 * device.  Returns -1 if there is no console or no room for the task.
 */
int init_start(uint8_t *stack, uint16_t size)
{
	char path[sizeof("/dev/ttyACM0")];

	strcpy_P(path, PSTR("/dev/ttyACM0"));
	if (open(path, O_RDWR) != STDIN_FILENO ||
	    dup2(STDIN_FILENO, STDOUT_FILENO) != STDOUT_FILENO)
		return -1;
	return task_create(init_task, stack, size);
}
