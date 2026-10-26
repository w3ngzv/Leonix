/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_EXEC_H
#define _LEONIX_EXEC_H

/* Most words a command line is cut into, the program name included. */
#define MAX_ARGS	6

int exec(int argc, const char *const argv[]);

#endif /* _LEONIX_EXEC_H */
