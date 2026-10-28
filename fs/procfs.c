// SPDX-License-Identifier: GPL-2.0-only
/*
 * /proc: kernel state as text, for ps, dmesg and uptime.  The text is made
 * a line at a time as it is read; private_data holds the line number and
 * f_pos the offset in it, so a reader may take any size of piece.  Only
 * one task reads files, so one line buffer serves every open /proc file.
 * The line makers are noinline so that one frame at a time is on the
 * stack.
 *
 *   tasks    "ID S FREE", then a line per task, the idle task last
 *   kmsg     the kernel log, oldest first, "[secs] text"
 *   uptime   seconds since boot and of them idle, as in Linux
 *
 * /proc: 以文本形式给出的内核状态, 供 ps、dmesg 与 uptime 读取. 文本在读取
 * 时逐行生成; private_data 保存行号, f_pos 保存行内偏移, 因此读者可以按任意
 * 大小分段读取. 只有一个任务读文件, 所有打开的 /proc 文件共用一个行缓冲区.
 * 生成各行的函数标为 noinline, 使栈上同时只有其中一个的栈帧.
 *
 *   tasks    "ID S FREE", 随后每个任务一行, 空闲任务在最后
 *   kmsg     内核日志, 从最旧的开始, 格式为 "[秒] 文本"
 *   uptime   开机以来的秒数及其中空闲的秒数, 格式与 Linux 相同
 */
#include <leonix/pgmspace.h>

#include <leonix/errno.h>
#include <leonix/irqflags.h>
#include <leonix/jiffies.h>
#include <leonix/kernel.h>
#include <leonix/printk.h>
#include <leonix/procfs.h>
#include <leonix/sched.h>
#include <leonix/string.h>

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

/*
 * line @n of tasks into line[]: its length, or 0 past the end
 * 把 tasks 的第 @n 行写入 line[], 返回其长度, 超出末尾时返回 0
 */
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

	/*
	 * a record overwritten during the read ends the file early
	 * 读取期间有记录被覆盖时, 文件提前结束
	 */
	if (first + n > newest || log_read(first + n, &rec) < 0)
		return 0;
	return snprintk(line, sizeof(line), "[%5u] %s\n", rec.secs, rec.text);
}

static uint8_t __attribute__((noinline)) uptime_line(uint8_t n)
{
	unsigned long flags;
	uint32_t now, idle;

	if (n)
		return 0;
	local_irq_save(flags);
	now = get_jiffies();
	idle = sched_idle_ticks();
	local_irq_restore(flags);
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
 * private_data holds the file in bits 7-5 and the line in bits 4-0; no
 * file here has more than 32 lines.
 *
 * private_data 的第 7-5 位保存文件编号, 第 4-0 位保存行号; 此处没有超过
 * 32 行的文件.
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

/**
 * procfs_open() - open /proc or a file in it
 * @f: the file to fill in
 * @rest: the path after "/proc": "" or "/" for the directory, or "/name"
 * @flags: open() flags
 *
 * 打开 /proc 或其中的文件. @rest 是 "/proc" 之后的路径, "" 或 "/" 表示
 * 目录本身, "/name" 表示文件.
 *
 * Return: 0, -EROFS for writing or O_CREAT, or -ENOENT if no file has
 * the name.  成功时返回 0, 以写方式或带 O_CREAT 打开时返回 -EROFS, 没有
 * 同名文件时返回 -ENOENT.
 */
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
