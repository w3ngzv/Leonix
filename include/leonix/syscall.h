/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_SYSCALL_H
#define _LEONIX_SYSCALL_H

/*
 * System call numbers, the operand of the SYS instruction.  Arguments
 * are pushed in the order of the C prototype and the result replaces
 * them; errors are negative errno values, as Linux returns them before
 * libc turns them into errno.  Leonix_toolchain carries a copy of this
 * table, and VM_VERSION in vm.h changes whenever it does.
 *
 * 系统调用号, 即 SYS 指令的操作数. 参数按 C 原型的顺序压栈, 结果取代参数; 错误
 * 为负的 errno, 与 Linux 在 libc 将其转为 errno 之前的返回值相同.
 * Leonix_toolchain 保存该表的一份副本, 该表每次改变时 vm.h 中的 VM_VERSION 随之
 * 改变.
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
 *
 * 返回值: count 为字节数, fd 为描述符, exit status 为退出状态, 128 + fault
 * 为 128 加故障号, -errno 为负的 errno, free bytes on / 为 / 上的空闲字节数;
 * exit() 不返回, readdir() 到达末尾时返回 0.
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
