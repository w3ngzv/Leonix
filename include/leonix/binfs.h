/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_BINFS_H
#define _LEONIX_BINFS_H

#include <leonix/fs.h>

/* Most words a command line is cut into, the program name included. */
#define MAX_ARGS	6

/*
 * A program in /bin.  The table of them is bin/programs.c, in flash, and
 * a program is added by writing its main() and adding a line there.
 */
struct program {
	const char *name;			/* in flash */
	int (*main)(int argc, char **argv);
};

extern const struct program programs[] PROGMEM;
extern const uint8_t nr_programs;

int binfs_open(struct file *f, const char *rest, uint16_t flags);
int exec(int argc, char **argv);

#endif /* _LEONIX_BINFS_H */
