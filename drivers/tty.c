// SPDX-License-Identifier: GPL-2.0-only
/*
 * A terminal in canonical mode, the part of Linux's n_tty line discipline
 * a command line needs.  The driver below moves raw bytes; readers get
 * whole lines.
 *
 * Input is echoed as it arrives.  Backspace and DEL erase a character,
 * CR, LF or CR LF end the line as '\n' (ICRNL), and Ctrl-C drops the
 * line, which then reads as empty.  Output '\n' goes out as CR LF
 * (ONLCR).  There is one terminal, on the board's one serial port.
 *
 * Ctrl-C also stops the program the shell is running (ISIG), as soon as
 * the raw driver has it, whether or not anything reads.  The input up to
 * it is then dropped, as n_tty does without NOFLSH.
 *
 * 规范模式的终端, 即 Linux n_tty 行规程中命令行所需的部分. 下层驱动收发
 * 原始字节, 上层读者得到整行.
 *
 * 输入随到随回显. 退格与 DEL 删去一个字符, CR、LF 或 CR LF 都以 '\n'
 * 结束一行 (ICRNL), Ctrl-C 丢弃该行, 读者随后读到空行. 输出的 '\n' 以
 * CR LF 发出 (ONLCR). 板上只有一个串口, 因此只有一个终端.
 *
 * Ctrl-C 同时停止 shell 正在运行的程序 (ISIG), 下层驱动一收到即生效, 与是否
 * 有读者无关. 截至该字节的输入随后被丢弃, 与 n_tty 未设 NOFLSH 时相同.
 */
#include <stddef.h>

#include <leonix/exec.h>
#include <leonix/irqflags.h>
#include <leonix/string.h>
#include <leonix/tty.h>

#define KEY_CTRL_C	0x03
#define KEY_BS		0x08
#define KEY_DEL		0x7f

static const struct file_operations *tty_raw;
static char tty_line[TTY_LINE];
static uint8_t tty_len;		/* bytes in tty_line once complete */
static uint8_t tty_pos;		/* bytes already handed to readers */
/* Ctrl-C bytes in the raw driver's buffer; 下层驱动缓冲区中的 Ctrl-C 字节数 */
static volatile uint8_t tty_intr_queued;

static void echo(const char *s, uint8_t len)
{
	tty_raw->write(NULL, s, len);
}

/* a Ctrl-C has left the raw buffer; 一个 Ctrl-C 已从下层缓冲区取出 */
static void intr_taken(void)
{
	unsigned long flags;

	echo("^C\r\n", 4);
	local_irq_save(flags);
	tty_intr_queued--;
	local_irq_restore(flags);
}

/* fill tty_line with the next line; 从终端读入下一行, 存入 tty_line */
static void tty_get_line(void)
{
	static char last;
	uint8_t len = 0;
	char c;

	for (;;) {
		tty_raw->read(NULL, &c, 1);
		if (c == '\n' && last == '\r') {
			last = c;
			continue;
		}
		last = c;
		if (c == '\r' || c == '\n') {
			echo("\r\n", 2);
			break;
		}
		if (c == KEY_CTRL_C) {
			intr_taken();
			len = 0;
			break;
		}
		if (c == KEY_BS || c == KEY_DEL) {
			if (len) {
				len--;
				echo("\b \b", 3);
			}
			continue;
		}
		/*
		 * Keep room for the '\n'.  TODO: input past the end of the
		 * line is dropped without any sign.
		 *
		 * 为 '\n' 留出位置. 待办: 超出行尾的输入被丢弃, 没有任何提示.
		 */
		if (c < ' ' || len == TTY_LINE - 1)
			continue;
		tty_line[len++] = c;
		echo(&c, 1);
	}
	tty_line[len++] = '\n';
	tty_len = len;
	tty_pos = 0;
}

/*
 * The rest of the current line, at most @count bytes of it, after reading
 * a new line if none is left.  Only one task may read.
 *
 * 返回当前行余下的部分, 至多 @count 字节; 当前行已读完时先读入新的一行.
 * 只允许一个任务读取.
 */
static int tty_read(struct file *f, char *buf, uint8_t count)
{
	uint8_t n;

	(void)f;
	if (tty_pos == tty_len)
		tty_get_line();
	n = tty_len - tty_pos;
	if (n > count)
		n = count;
	memcpy(buf, tty_line + tty_pos, n);
	tty_pos += n;
	return n;
}

static int tty_write(struct file *f, const char *buf, uint8_t count)
{
	uint8_t start = 0, i;

	(void)f;
	for (i = 0; i < count; i++) {
		if (buf[i] != '\n')
			continue;
		tty_raw->write(NULL, buf + start, i - start);
		tty_raw->write(NULL, "\r\n", 2);
		start = i + 1;
	}
	tty_raw->write(NULL, buf + start, count - start);
	return count;
}

static const struct file_operations tty_fops = {
	.read = tty_read,
	.write = tty_write,
};

/**
 * tty_receive() - see a byte as it enters the raw driver's buffer
 * @c: the byte
 *
 * TODO: a byte that finds the buffer full is not seen until a reader
 * makes room, so Ctrl-C cannot stop a program that never reads while
 * the buffer is full of typed-ahead input.
 *
 * 在字节 @c 进入下层驱动缓冲区时检查该字节. 待办: 缓冲区已满时到达的字节在
 * 读者腾出空间之前不会被检查到, 因此缓冲区被预先键入的输入占满时, Ctrl-C 无法
 * 停止一个从不读取的程序.
 *
 * Context: the raw driver's receive path, task or interrupt, after the
 * byte is stored.  在下层驱动的接收路径中、字节存入之后调用, 任务或中断均可.
 */
void tty_receive(char c)
{
	unsigned long flags;

	if (c != KEY_CTRL_C)
		return;
	local_irq_save(flags);
	tty_intr_queued++;
	local_irq_restore(flags);
	exec_interrupt();
}

/**
 * tty_intr_flush() - drop the input up to the Ctrl-C that stopped a program
 *
 * The rest of the current line goes too.  Each Ctrl-C taken here is
 * echoed; one a reader took was echoed then.
 *
 * 丢弃截至停止程序的那个 Ctrl-C 为止的输入, 当前行余下的部分一并丢弃. 此处取
 * 出的每个 Ctrl-C 都回显; 已被读者取出的在当时已回显.
 *
 * Context: the task that ran the program.  在运行该程序的任务中调用.
 */
void tty_intr_flush(void)
{
	char c;

	while (tty_intr_queued) {
		tty_raw->read(NULL, &c, 1);
		if (c == KEY_CTRL_C)
			intr_taken();
	}
	tty_pos = tty_len;
}

/**
 * tty_register() - put the line discipline over a raw device
 * @name: the terminal's name in /dev, a string in flash
 * @raw: the device, whose operations are called with no struct file
 *
 * 在原始设备 @raw 之上加上行规程, 以 @name 注册到 /dev. @name 是 flash
 * 中的字符串, @raw 的各项操作在调用时不带 struct file.
 *
 * Context: before sched_start().  在 sched_start() 之前调用.
 * Return: 0, -1 if a terminal exists already, or -ENOSPC if the device
 * table is full.  成功时返回 0, 已有终端时返回 -1, 设备表已满时返回
 * -ENOSPC.
 */
int tty_register(const char *name, const struct file_operations *raw)
{
	if (tty_raw)
		return -1;
	tty_raw = raw;
	return register_chrdev(name, &tty_fops);
}
