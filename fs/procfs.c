// SPDX-License-Identifier: GPL-2.0-only
/*
 * /proc: kernel state as text, for ps, dmesg and uptime to read, the way
 * Linux shows it.  The text is made one line at a time as it is read:
 * private_data is the line, f_pos the offset in it, so a reader may take
 * it in any size of piece.  Only one task reads files, so one line
 * buffer serves every open /proc file.  The line makers are kept out of
 * line so that only one of their frames is on the stack at a time.
 *
 *   tasks    "ID S FREE", then a line per task, the idle task last
 *   kmsg     the kernel log, oldest first, "[secs] text"
 *   uptime   seconds since boot and of them idle, with hundredths, in
 *            Linux's format
 */
#include <avr/pgmspace.h>
#include <string.h>
#include <util/atomic.h>

#include <leonix/errno.h>
#include <leonix/jiffies.h>
#include <leonix/kernel.h>
#include <leonix/printk.h>
#include <leonix/procfs.h>
#include <leonix/sched.h>

#define PROC_LINE	24
#define CENTI		(HZ / 100)	/* jiffies per hundredth of a second */

enum { PROC_TASKS, PROC_KMSG, PROC_UPTIME, NR_PROC_FILES };

static const char tasks_name[] PROGMEM = "tasks";
static const char kmsg_name[] PROGMEM = "kmsg";
static const char uptime_name[] PROGMEM = "uptime";

static const char *const proc_names[NR_PROC_FILES] PROGMEM = {
	tasks_name, kmsg_name, uptime_name,
};

static const char state_letters[] PROGMEM = {
	[TASK_RUNNABLE] = 'R', [TASK_SLEEPING] = 'S',
	[TASK_BLOCKED] = 'B', [TASK_DEAD] = 'D',
};

static char line[PROC_LINE];

/* Line @n of the tasks file into line[]; returns its length, 0 past the end. */
static uint8_t __attribute__((noinline)) tasks_line(uint8_t n)
{
	uint8_t state;

	if (n == 0)
		return snprintk(line, sizeof(line), "ID S FREE\n");
	state = sched_task_state(n - 1);
	if (state != SCHED_NO_STATE)
		return snprintk(line, sizeof(line), "%2u %c %4u\n", n - 1,
				pgm_read_byte(&state_letters[state]),
				sched_stack_free(n - 1));
	if (sched_task_state(n - 2) != SCHED_NO_STATE || n == 1)
		return snprintk(line, sizeof(line), "id R %4u\n",
				sched_stack_free(SCHED_IDLE_TASK));
	return 0;
}

static uint8_t __attribute__((noinline)) kmsg_line(uint8_t n)
{
	struct log_record rec;
	uint16_t newest = log_newest();
	uint16_t first = newest > LOG_RECORDS ? newest - LOG_RECORDS + 1 : 1;

	/* A record overwritten while the file is read ends it early. */
	if (first + n > newest || log_read(first + n, &rec) < 0)
		return 0;
	return snprintk(line, sizeof(line), "[%5u] %s\n", rec.secs, rec.text);
}

static uint8_t __attribute__((noinline)) uptime_line(uint8_t n)
{
	uint32_t now, idle;

	if (n)
		return 0;
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		now = get_jiffies();
		idle = sched_idle_ticks();
	}
	return snprintk(line, sizeof(line), "%lu.%02u %lu.%02u\n",
			now / HZ, (uint8_t)(now % HZ / CENTI),
			idle / HZ, (uint8_t)(idle % HZ / CENTI));
}

static uint8_t proc_line(uint8_t file, uint8_t n)
{
	switch (file) {
	case PROC_TASKS:
		return tasks_line(n);
	case PROC_KMSG:
		return kmsg_line(n);
	default:
		return uptime_line(n);
	}
}

/*
 * The file number is kept in the top bits of private_data, the line in
 * the low five: no file here has more than 32 lines.
 */
#define PROC_FILE(f)	((f)->private_data >> 5)
#define PROC_LINE_NR(f)	((f)->private_data & 0x1f)

static int proc_read(struct file *f, char *buf, uint8_t count)
{
	uint8_t len, n;

	len = proc_line(PROC_FILE(f), PROC_LINE_NR(f));
	if (!len)
		return 0;
	n = len - f->f_pos;
	if (n > count)
		n = count;
	memcpy(buf, line + f->f_pos, n);
	f->f_pos += n;
	if (f->f_pos == len) {
		f->f_pos = 0;
		f->private_data++;
	}
	return n;
}

static const struct file_operations proc_file_fops = {
	.read = proc_read,
};

static int proc_readdir(struct file *f, char *name, uint8_t size)
{
	if (f->f_pos >= NR_PROC_FILES)
		return 0;
	strlcpy_P(name, pgm_read_ptr(&proc_names[f->f_pos++]), size);
	return 1;
}

static const struct file_operations proc_dir_fops = {
	.readdir = proc_readdir,
};

/* Mounted at /proc by fs/vfs.c. */
int procfs_open(struct file *f, const char *rest, uint16_t flags)
{
	uint8_t i;

	if (!rest[0] || !strcmp_P(rest, PSTR("/"))) {
		f->f_op = &proc_dir_fops;
		return 0;
	}
	if ((flags & O_ACCMODE) != O_RDONLY || (flags & O_CREAT))
		return -EROFS;
	for (i = 0; i < NR_PROC_FILES; i++) {
		if (strcmp_P(rest + 1, pgm_read_ptr(&proc_names[i])))
			continue;
		f->f_op = &proc_file_fops;
		f->private_data = i << 5;
		return 0;
	}
	return -ENOENT;
}
