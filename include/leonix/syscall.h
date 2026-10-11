/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_SYSCALL_H
#define _LEONIX_SYSCALL_H

/*
 * System call numbers, the operand of the SYS instruction.  Arguments
 * are pushed in the order of the C prototype and the result replaces
 * them; errors are negative errno values, as Linux returns them before
 * libc turns them into errno.  leonix-toolchain carries a copy of this
 * table, and VM_VERSION in vm.h changes whenever it does.
 *
 *   exit(status)                  no return
 *   read(fd, buf, count)          -> count or -errno
 *   write(fd, buf, count)         -> count or -errno
 *   open(path, flags)             -> fd or -errno
 *   close(fd)                     -> 0 or -errno
 *   readdir(fd, buf, size)        -> 1, 0 at the end, or -errno
 *   unlink(path)                  -> 0 or -errno
 *   exec(argc, argv)              -> exit status, 128 + fault, or -errno
 *   dup2(oldfd, newfd)            -> newfd or -errno
 *   statfs()                      -> free bytes on /
 */
#define SYS_exit	0
#define SYS_read	1
#define SYS_write	2
#define SYS_open	3
#define SYS_close	4
#define SYS_readdir	5
#define SYS_unlink	6
#define SYS_exec	7
#define SYS_dup2	8
#define SYS_statfs	9

#endif /* _LEONIX_SYSCALL_H */
