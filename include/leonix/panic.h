/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_PANIC_H
#define _LEONIX_PANIC_H

/* Plain defines, so that start.S can load them too. */
#define PANIC_STACK_OVERFLOW	1
#define PANIC_BAD_INTERRUPT	2

/* Second argument of panic() when no single task is to blame. */
#define PANIC_NO_TASK		0xff
#define PANIC_IDLE_TASK		0xfe

#ifndef __ASSEMBLER__
#include <stdint.h>

/*
 * Set once panic() has started.  Drivers that panic() calls use it to
 * avoid sleeping and to keep their timeouts working with the tick off,
 * as console drivers check oops_in_progress in Linux.
 */
extern volatile uint8_t oops_in_progress;

void panic(uint8_t reason, uint8_t task) __attribute__((noreturn));
#endif

#endif /* _LEONIX_PANIC_H */
