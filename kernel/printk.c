// SPDX-License-Identifier: GPL-2.0-only
/*
 * The kernel log: a ring of fixed-size records in SRAM, after Linux's
 * log_buf.  A console registered with register_console() is told of each
 * record and copies it out with log_read().
 *
 * printk() formats on the caller's stack and stores the record with
 * interrupts disabled, so a task, an interrupt handler or code before
 * sched_start() may call it.  The record and the formatter's frame are
 * on the caller's stack; keep printk() out of deep call chains such as
 * the I2C transfer path.
 *
 * 内核日志: SRAM 中由定长记录组成的环, 仿照 Linux 的 log_buf. 以
 * register_console() 注册的控制台在每条记录写入后得到通知, 再以 log_read()
 * 取出记录.
 *
 * printk() 在调用者的栈上格式化, 关中断后写入记录, 因此任务、中断处理程序
 * 以及 sched_start() 之前的代码都可以调用. 记录与格式化函数的栈帧都在调用者
 * 的栈上, 深层调用链 (例如 I2C 传输路径) 中不应调用 printk().
 */
#include <stdarg.h>

#include <leonix/irqflags.h>
#include <leonix/jiffies.h>
#include <leonix/printk.h>
#include <leonix/string.h>

static struct log_record log_buf[LOG_RECORDS];
static uint8_t log_next;	/* slot the next record goes into */
static uint16_t log_seq;	/* records written since boot */

/*
 * told of each new record; the console reads it from its own task
 * 每条新记录写入后调用; 控制台在自己的任务中读取记录
 */
static void (*console_kick)(void);

/*
 * The level that @fmt starts with, KERN_SOH and a digit, which are
 * stepped over; MESSAGE_LOGLEVEL_DEFAULT if it starts with none.
 *
 * @fmt 开头的级别, 即 KERN_SOH 与一个数字, 读取后跳过; 开头没有级别时返回
 * MESSAGE_LOGLEVEL_DEFAULT.
 */
static uint8_t printk_get_level(const char **fmt)
{
	uint8_t c;

	if (pgm_read_byte(*fmt) != KERN_SOH_ASCII)
		return MESSAGE_LOGLEVEL_DEFAULT;
	c = pgm_read_byte(*fmt + 1);
	if (c < '0' + LOGLEVEL_EMERG || c > '0' + LOGLEVEL_DEBUG)
		return MESSAGE_LOGLEVEL_DEFAULT;
	*fmt += 2;
	return c - '0';
}

/**
 * printk_P() - add a message to the kernel log
 * @fmt: format, a string in flash, as for vsnprintk_P(), after a
 *	 KERN_<LEVEL> if any
 * @...: arguments for @fmt
 *
 * Called through printk() or a pr_<level>() macro, which put the literal
 * in flash.  The message is cut to LOG_TEXT - 1 characters.
 *
 * 向内核日志添加一条消息. @fmt 可以以 KERN_<LEVEL> 开头. 经由 printk() 或
 * pr_<level>() 宏调用, 二者把字面量放在 flash 中. 消息截断为 LOG_TEXT - 1 个
 * 字符.
 *
 * Context: any, interrupt handlers included.  任意上下文, 包括中断处理程序.
 */
void printk_P(const char *fmt, ...)
{
	struct log_record rec;
	unsigned long flags;
	va_list ap;

	/*
	 * TODO: mark a message that was cut; today it ends short with no
	 * sign that more was meant.
	 *
	 * 待办: 为被截断的消息加上标记; 目前截断之后没有任何提示.
	 */
	rec.level = printk_get_level(&fmt);
	va_start(ap, fmt);
	vsnprintk_P(rec.text, sizeof(rec.text), fmt, ap);
	va_end(ap);
	rec.secs = get_jiffies() / HZ;

	local_irq_save(flags);
	log_buf[log_next] = rec;
	if (++log_next == LOG_RECORDS)
		log_next = 0;
	log_seq++;
	local_irq_restore(flags);
	/*
	 * TODO: the console is also the shell's terminal, and a record can
	 * land between the prompt and what is being typed.
	 *
	 * 待办: 控制台同时是 shell 的终端, 日志可能插在提示符与正在输入的
	 * 内容之间.
	 */
	if (console_kick)
		console_kick();
}

/**
 * register_console() - name the function told of new records
 * @kick: called after every record, possibly in an interrupt handler
 *
 * 指定每条记录写入后调用的函数 @kick, 该函数可能在中断处理程序中被调用.
 *
 * Context: before sched_start().  在 sched_start() 之前调用.
 */
void register_console(void (*kick)(void))
{
	console_kick = kick;
}

/**
 * log_newest() - the newest record's sequence number
 *
 * Return: the number, counted from 1, or 0 while the log is empty.
 * 最新记录的序号, 从 1 开始计数; 日志为空时返回 0.
 */
uint16_t log_newest(void)
{
	unsigned long flags;
	uint16_t seq;

	local_irq_save(flags);
	seq = log_seq;
	local_irq_restore(flags);
	return seq;
}

/**
 * log_read() - copy a record out of the log
 * @seq: its sequence number, counted from 1
 * @rec: where to copy it
 *
 * 把序号为 @seq (从 1 开始计数) 的记录复制到 @rec.
 *
 * Return: 0, or -1 if @seq is not written yet or already overwritten.
 * 成功时返回 0, @seq 尚未写入或已被覆盖时返回 -1.
 */
int log_read(uint16_t seq, struct log_record *rec)
{
	unsigned long flags;
	int ret = -1;

	local_irq_save(flags);
	if (seq && seq <= log_seq && log_seq - seq < LOG_RECORDS) {
		*rec = log_buf[(seq - 1) % LOG_RECORDS];
		ret = 0;
	}
	local_irq_restore(flags);
	return ret;
}

/**
 * suppress_message_printing() - whether the console leaves out a level
 * @level: a record's level
 *
 * Linux keeps the threshold in a variable that the syslog() call
 * changes; Leonix has no such call, so it is CONSOLE_LOGLEVEL_DEFAULT.
 *
 * 控制台是否略去级别为 @level 的记录. Linux 把阈值存放在 syslog() 调用可以修改
 * 的变量中; Leonix 没有该调用, 因此阈值是 CONSOLE_LOGLEVEL_DEFAULT.
 *
 * Return: nonzero if a record of @level is not to be printed.
 * 级别为 @level 的记录不应打印时返回非零值.
 */
int suppress_message_printing(uint8_t level)
{
	return level >= CONSOLE_LOGLEVEL_DEFAULT;
}
