// SPDX-License-Identifier: GPL-2.0-only
/*
 * A command line on ttyACM0, for looking at the running kernel from a
 * terminal program such as screen.
 *
 * The shell is a kernel task: the board has no MMU and no privilege
 * levels, so there is no user space to run it in.  It still keeps to
 * the boundary a user program would have, reading and writing through
 * file descriptors and asking the kernel for figures through the sched_
 * and log_ calls.  Echo and line editing are
 * the terminal's, in drivers/tty.c.
 */
#include <avr/pgmspace.h>
#include <string.h>

#include <leonix/jiffies.h>
#include <leonix/kernel.h>
#include <leonix/printk.h>
#include <leonix/sched.h>
#include <leonix/shell.h>
#include <leonix/tty.h>
#include <leonix/unistd.h>

#define SHELL_OUT	40	/* longest formatted output line */
#define PERCENT		100

/* Format into a line and write it out; the format string is in flash. */
static void __attribute__((noinline)) sh_printf_P(const char *fmt, ...)
{
	char out[SHELL_OUT];
	va_list ap;
	uint8_t len;

	va_start(ap, fmt);
	len = vsnprintk_P(out, sizeof(out), fmt, ap);
	va_end(ap);
	write(STDOUT_FILENO, out, len);
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

/* The names in directory @path, one per line; "/" without an argument. */
static void cmd_ls(const char *path)
{
	char name[NAME_MAX + 1];
	int fd;

	fd = open(path[0] ? path : "/", O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		sh_printf("ls: %s: error %d\n", path, -fd);
		return;
	}
	while (readdir(fd, name, sizeof(name)) > 0)
		sh_printf("%s\n", name);
	close(fd);
}

/* @cmd is the first word of the line, @arg the rest, possibly empty. */
static void run(const char *cmd, const char *arg)
{
	if (!strcmp_P(cmd, PSTR("help")))
		sh_printf("help ps dmesg uptime ls\n");
	else if (!strcmp_P(cmd, PSTR("ps")))
		cmd_ps();
	else if (!strcmp_P(cmd, PSTR("dmesg")))
		cmd_dmesg();
	else if (!strcmp_P(cmd, PSTR("uptime")))
		cmd_uptime();
	else if (!strcmp_P(cmd, PSTR("ls")))
		cmd_ls(arg);
	else
		sh_printf("%s: not found\n", cmd);
}

/* The terminal hands over a whole line, '\n' included, in one read(). */
static void shell_task(void)
{
	char line[TTY_LINE + 1];
	char *arg;
	int len;

	for (;;) {
		sh_printf("# ");
		len = read(STDIN_FILENO, line, TTY_LINE);
		if (len <= 1)
			continue;
		line[len - 1] = '\0';
		arg = strchr(line, ' ');
		if (arg)
			*arg++ = '\0';
		run(line, arg ? arg : "");
	}
}

/*
 * Open the terminal as standard input and output, then start the shell.
 * Called before sched_start(), after the USB driver has registered the
 * device.  Returns -1 if there is no such device or no room for the
 * task.
 */
int shell_init(uint8_t *stack, uint16_t size)
{
	char path[sizeof("/dev/ttyACM0")];

	strcpy_P(path, PSTR("/dev/ttyACM0"));
	if (open(path, O_RDWR) != STDIN_FILENO ||
	    dup2(STDIN_FILENO, STDOUT_FILENO) != STDOUT_FILENO)
		return -1;
	return task_create(shell_task, stack, size);
}
