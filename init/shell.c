// SPDX-License-Identifier: GPL-2.0-only
/*
 * A command line on ttyACM0, for looking at the running kernel from a
 * terminal program such as screen.
 *
 * The shell is a kernel task: the board has no MMU and no privilege
 * levels, so there is no user space to run it in.  It still keeps to
 * the boundary a user program would have, reading and writing lines
 * through the device's file_operations and asking the kernel for
 * figures through the sched_ and log_ calls.  Echo and line editing are
 * the terminal's, in drivers/tty.c.
 */
#include <avr/pgmspace.h>
#include <string.h>

#include <leonix/fs.h>
#include <leonix/jiffies.h>
#include <leonix/kernel.h>
#include <leonix/printk.h>
#include <leonix/sched.h>
#include <leonix/shell.h>
#include <leonix/tty.h>

#define SHELL_OUT	40	/* longest formatted output line */
#define PERCENT		100

static const struct file_operations *tty;

/* Format into a line and write it out; the format string is in flash. */
static void __attribute__((noinline)) sh_printf_P(const char *fmt, ...)
{
	char out[SHELL_OUT];
	va_list ap;
	uint8_t len;

	va_start(ap, fmt);
	len = vsnprintk_P(out, sizeof(out), fmt, ap);
	va_end(ap);
	tty->write(out, len);
}

#define sh_printf(fmt, ...)	sh_printf_P(PSTR(fmt), ##__VA_ARGS__)

static const char state_names[][2] PROGMEM = {
	[TASK_RUNNABLE] = "R",
	[TASK_SLEEPING] = "S",
	[TASK_BLOCKED] = "B",
	[TASK_DEAD] = "D",
};

/* One line per task: index, state as in Linux's ps, untouched stack. */
static void cmd_ps(void)
{
	uint8_t i, state;

	sh_printf("ID S FREE\n");
	for (i = 0; (state = sched_task_state(i)) != SCHED_NO_STATE; i++)
		sh_printf("%2u %S %4u\n", i, state_names[state],
			  sched_stack_free(i));
	sh_printf("id R %4u\n", sched_stack_free(SCHED_IDLE_TASK));
}

static void cmd_dmesg(void)
{
	struct log_record rec;
	uint16_t newest = log_newest();
	uint16_t seq = newest > LOG_RECORDS ? newest - LOG_RECORDS + 1 : 1;

	for (; seq && seq <= newest; seq++)
		if (log_read(seq, &rec) == 0)
			sh_printf("[%5u] %s\n", rec.secs, rec.text);
}

/*
 * The idle share divides by the uptime's hundredth, rounded up, since
 * idle ticks times 100 would overflow 32 bits after 12 hours.  Rounding
 * up keeps the share at or below 100%; rounding down gave 100% on a
 * board that was 99.9% idle.
 */
static void cmd_uptime(void)
{
	uint32_t now = get_jiffies();
	uint32_t idle = sched_idle_ticks();
	uint32_t hundredth = (now + PERCENT - 1) / PERCENT;

	sh_printf("up %lu s, idle %lu%%\n", now / HZ,
		  hundredth ? idle / hundredth : 0);
}

static void cmd_devices(void)
{
	const char *name;
	uint8_t i;

	for (i = 0; (name = chrdev_name(i)); i++)
		sh_printf("%S\n", name);
}

static void run(const char *cmd)
{
	if (!strcmp_P(cmd, PSTR("help")))
		sh_printf("help ps dmesg uptime devices\n");
	else if (!strcmp_P(cmd, PSTR("ps")))
		cmd_ps();
	else if (!strcmp_P(cmd, PSTR("dmesg")))
		cmd_dmesg();
	else if (!strcmp_P(cmd, PSTR("uptime")))
		cmd_uptime();
	else if (!strcmp_P(cmd, PSTR("devices")))
		cmd_devices();
	else
		sh_printf("%s: not found\n", cmd);
}

/* tty_read() hands over a whole line, '\n' included, in one call. */
static void shell_task(void)
{
	char line[TTY_LINE + 1];
	int len;

	for (;;) {
		sh_printf("# ");
		len = tty->read(line, TTY_LINE);
		if (len <= 1)
			continue;
		line[len - 1] = '\0';
		run(line);
	}
}

/*
 * Start the shell on ttyACM0.  Called before sched_start(), after the
 * USB driver has registered the device.  Returns -1 if there is no such
 * device or no room for the task.
 */
int shell_init(uint8_t *stack, uint16_t size)
{
	char name[sizeof("ttyACM0")];

	strcpy_P(name, PSTR("ttyACM0"));
	tty = chrdev_get(name);
	if (!tty)
		return -1;
	return task_create(shell_task, stack, size);
}
