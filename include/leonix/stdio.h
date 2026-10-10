/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_STDIO_H
#define _LEONIX_STDIO_H

#include <leonix/kernel.h>

/* Longest line dprintf() writes at once; the rest of a longer one is cut. */
#define DPRINTF_LINE	40

/* As POSIX dprintf(), with the format string in flash. */
#define dprintf(fd, fmt, ...)	dprintf_P((fd), PSTR(fmt), ##__VA_ARGS__)

int dprintf_P(int fd, const char *fmt, ...);

#endif /* _LEONIX_STDIO_H */
