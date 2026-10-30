/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_EXEC_H
#define _LEONIX_EXEC_H

/*
 * most words a command line is cut into, the program name included
 * 一条命令行至多切分成的词数, 含程序名
 */
#define MAX_ARGS	6

int exec(int argc, const char *const argv[]);
void exec_interrupt(void);

#endif /* _LEONIX_EXEC_H */
