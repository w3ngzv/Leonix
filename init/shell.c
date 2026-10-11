// SPDX-License-Identifier: GPL-2.0-only
/*
 * A command line on ttyACM0, for looking at the running kernel from a
 * terminal program such as screen.
 *
 * The shell is a kernel task: the board has no MMU and no privilege
 * levels, so there is no user space to run it in.  It still keeps to
 * the boundary a user program would have, working through file
 * descriptors only.  Every command is a program in /bin; the shell cuts
 * the line into words, sets up a redirection, and runs the program, the
 * part of a shell that bash does with fork(), dup2() and execve().  Echo
 * and line editing are the terminal's, in drivers/tty.c.
 */
#include <avr/pgmspace.h>
#include <string.h>

#include <leonix/errno.h>
#include <leonix/exec.h>
#include <leonix/sched.h>
#include <leonix/shell.h>
#include <leonix/stdio.h>
#include <leonix/tty.h>
#include <leonix/unistd.h>

/* Where standard output waits while a redirection replaces it. */
#define SAVED_STDOUT	(MAX_FDS - 1)

/*
 * Cut @line into words at spaces, in place.  A word starting with '>' or
 * ">>", with the file name after it or in the next word, redirects
 * standard output and is taken out; *@target and *@flags describe it.
 * Returns the number of words left, or -1 for too many.
 */
static int split(char *line, char **argv, char **target, uint16_t *flags)
{
	int argc = 0;
	char *p = line;

	*target = NULL;
	for (;;) {
		while (*p == ' ')
			*p++ = '\0';
		if (!*p)
			break;
		if (*p == '>') {
			*flags = O_WRONLY | O_CREAT | O_TRUNC;
			if (*++p == '>') {
				*flags = O_WRONLY | O_CREAT | O_APPEND;
				p++;
			}
			while (*p == ' ')
				p++;
			*target = p;
		} else {
			if (argc == MAX_ARGS)
				return -1;
			argv[argc++] = p;
		}
		while (*p && *p != ' ')
			p++;
	}
	return argc;
}

/*
 * Point standard output at @target for one command, keeping the
 * terminal in SAVED_STDOUT.  Returns 0 or a negative errno.
 */
static int redirect(const char *target, uint16_t flags)
{
	int fd = open(target, flags);

	if (fd < 0)
		return fd;
	dup2(STDOUT_FILENO, SAVED_STDOUT);
	dup2(fd, STDOUT_FILENO);
	close(fd);
	return 0;
}

static void restore_stdout(void)
{
	dup2(SAVED_STDOUT, STDOUT_FILENO);
	close(SAVED_STDOUT);
}

static void run(char *line)
{
	char *argv[MAX_ARGS + 1];
	char *target;
	uint16_t flags;
	int argc, err;

	argc = split(line, argv, &target, &flags);
	if (argc < 0) {
		dprintf(STDOUT_FILENO, "sh: too many words\n");
		return;
	}
	if (!argc)
		return;
	argv[argc] = NULL;
	if (target) {
		err = redirect(target, flags);
		if (err < 0) {
			dprintf(STDOUT_FILENO, "sh: %s: error %d\n", target, -err);
			return;
		}
	}
	err = exec(argc, argv);
	if (target)
		restore_stdout();
	if (err == -ENOENT)
		dprintf(STDOUT_FILENO, "sh: %s: not found\n", argv[0]);
	else if (err == -ENOEXEC)
		dprintf(STDOUT_FILENO, "sh: %s: not a program\n", argv[0]);
}

/* The terminal hands over a whole line, '\n' included, in one read(). */
static void shell_task(void)
{
	char line[TTY_LINE + 1];
	int len;

	for (;;) {
		dprintf(STDOUT_FILENO, "# ");
		len = read(STDIN_FILENO, line, TTY_LINE);
		if (len <= 1)
			continue;
		line[len - 1] = '\0';
		run(line);
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
