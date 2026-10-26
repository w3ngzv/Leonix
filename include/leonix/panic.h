/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_PANIC_H
#define _LEONIX_PANIC_H

/*
 * plain defines, so that start.S can load them too
 * 以普通的 #define 写出, 使 start.S 也能加载
 */
#define PANIC_STACK_OVERFLOW	1
#define PANIC_BAD_INTERRUPT	2

/*
 * second argument of panic() when no single task is to blame
 * 没有单个任务可归咎时 panic() 的第二个参数
 */
#define PANIC_NO_TASK		0xff
#define PANIC_IDLE_TASK		0xfe

#ifndef __ASSEMBLER__
#include <stdint.h>

/*
 * Set once panic() has started.  Drivers that panic() calls use it to
 * avoid sleeping and to keep their timeouts working with the tick off,
 * as console drivers check oops_in_progress in Linux.
 *
 * panic() 开始后置位. panic() 调用的驱动据此避免睡眠, 并在节拍停止时保持超时有
 * 效, 与 Linux 的控制台驱动检查 oops_in_progress 相同.
 */
extern volatile uint8_t oops_in_progress;

void panic(uint8_t reason, uint8_t task) __attribute__((noreturn));
#endif

#endif /* _LEONIX_PANIC_H */
