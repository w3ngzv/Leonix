/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_TTY_H
#define _LEONIX_TTY_H

#include <leonix/fs.h>

/* Longest line a reader gets, the newline included. */
#define TTY_LINE	32

int tty_register(const char *name, const struct file_operations *raw);

#endif /* _LEONIX_TTY_H */
