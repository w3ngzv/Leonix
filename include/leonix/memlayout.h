/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_MEMLAYOUT_H
#define _LEONIX_MEMLAYOUT_H

/*
 * SRAM layout, plain defines so that start.S can use them too.
 *
 *   0x0100 ..           .data, then .bss
 *            .. 0x07ff  boot stack, growing down from BOOT_STACK_TOP
 *   0x0800 .. 0x0801    Caterina's boot key, see arch/avr/usb.c
 *   0x0802 .. 0x0aff    .noinit: the larger task stacks, __task_stack
 *
 * The boot stack serves main() until sched_start(), then panic(), so it
 * sits below the boot key, where the task stacks cannot reach it.  The
 * Makefile checks both ends; its addresses must change with these.
 */
#define BOOT_KEY_ADDR		0x0800
#define BOOT_STACK_TOP		(BOOT_KEY_ADDR - 1)
#define TASK_STACKS_START	(BOOT_KEY_ADDR + 2)

#endif /* _LEONIX_MEMLAYOUT_H */
