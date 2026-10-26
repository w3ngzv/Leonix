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
 * 规范模式的终端, 即 Linux n_tty 行规程中命令行所需的部分. 下层驱动收发
 * 原始字节, 上层读者得到整行.
 *
 * 输入随到随回显. 退格与 DEL 删去一个字符, CR、LF 或 CR LF 都以 '\n'
 * 结束一行 (ICRNL), Ctrl-C 丢弃该行, 读者随后读到空行. 输出的 '\n' 以
 * CR LF 发出 (ONLCR). 板上只有一个串口, 因此只有一个终端.
 */
#include <stddef.h>
#include <string.h>

#include <leonix/tty.h>

#define KEY_CTRL_C	0x03
#define KEY_BS		0x08
#define KEY_DEL		0x7f

static const struct file_operations *tty_raw;
static char tty_line[TTY_LINE];
static uint8_t tty_len;		/* bytes in tty_line once complete */
static uint8_t tty_pos;		/* bytes already handed to readers */

static void echo(const char *s, uint8_t len)
{
	tty_raw->write(NULL, s, len);
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
			echo("^C\r\n", 4);
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
		/* keep room for the '\n'; 为 '\n' 留出位置 */
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
