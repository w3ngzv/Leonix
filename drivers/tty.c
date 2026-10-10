// SPDX-License-Identifier: GPL-2.0-only
/*
 * A terminal in canonical mode, the part of Linux's n_tty line discipline
 * that a command line needs.  The driver below moves raw bytes; readers
 * above get whole lines.
 *
 * Input: keys are echoed as they arrive, backspace and DEL take back the
 * last character, Enter ends the line as '\n' whether the terminal sends
 * CR, LF or CR LF (ICRNL), and Ctrl-C drops the line.  With no signals,
 * a dropped line reads as an empty one.  Output: '\n' goes out as CR LF
 * (ONLCR).
 *
 * There is one terminal, on the one serial port the board has.
 */
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
	tty_raw->write(s, len);
}

/* Fill tty_line with the next line from the terminal. */
static void tty_get_line(void)
{
	static char last;
	uint8_t len = 0;
	char c;

	for (;;) {
		tty_raw->read(&c, 1);
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
		/* Room is kept for the '\n'. */
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
 * Return the rest of the current line, at most @count bytes of it,
 * reading a new line first if none is left.  Only one task may read.
 */
static int tty_read(char *buf, uint8_t count)
{
	uint8_t n;

	if (tty_pos == tty_len)
		tty_get_line();
	n = tty_len - tty_pos;
	if (n > count)
		n = count;
	memcpy(buf, tty_line + tty_pos, n);
	tty_pos += n;
	return n;
}

static int tty_write(const char *buf, uint8_t count)
{
	uint8_t start = 0, i;

	for (i = 0; i < count; i++) {
		if (buf[i] != '\n')
			continue;
		tty_raw->write(buf + start, i - start);
		tty_raw->write("\r\n", 2);
		start = i + 1;
	}
	tty_raw->write(buf + start, count - start);
	return count;
}

static const struct file_operations tty_fops = {
	.read = tty_read,
	.write = tty_write,
};

/*
 * Put the line discipline over @raw and register the result as @name, a
 * string in flash.  Returns -1 if a terminal exists already or the
 * device table is full.
 */
int tty_register(const char *name, const struct file_operations *raw)
{
	if (tty_raw)
		return -1;
	tty_raw = raw;
	return register_chrdev(name, &tty_fops);
}
