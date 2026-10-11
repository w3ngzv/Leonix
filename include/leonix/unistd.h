/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_UNISTD_H
#define _LEONIX_UNISTD_H

#include <stdint.h>

#include <leonix/fs.h>

#define STDIN_FILENO	0
#define STDOUT_FILENO	1
#define MAX_FDS		4

/*
 * The calls a program makes, named and numbered as in Linux.  There are
 * no processes, so there is one table of descriptors, the shell's, and
 * the programs it runs share it.
 */
int open(const char *path, uint16_t flags);
int close(int fd);
int read(int fd, char *buf, uint8_t count);
int write(int fd, const char *buf, uint8_t count);
int readdir(int fd, char *name, uint8_t size);
int unlink(const char *path);
int dup2(int oldfd, int newfd);

#endif /* _LEONIX_UNISTD_H */
