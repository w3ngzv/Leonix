// SPDX-License-Identifier: GPL-2.0-only
#include <leonix/stdio.h>
#include <leonix/unistd.h>

/*
 * Format into a line on the stack and write it to @fd.  Returns what
 * write() returned.  Kept out of line, so that the line buffer is not in
 * the frame of every caller.
 */
int __attribute__((noinline)) dprintf_P(int fd, const char *fmt, ...)
{
	char line[DPRINTF_LINE];
	va_list ap;
	uint8_t len;

	va_start(ap, fmt);
	len = vsnprintk_P(line, sizeof(line), fmt, ap);
	va_end(ap);
	return write(fd, line, len);
}
