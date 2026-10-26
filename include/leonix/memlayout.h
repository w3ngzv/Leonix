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
 *
 * SRAM 布局, 以普通的 #define 写出, 使 start.S 也能使用.
 *
 *   0x0100 ..           .data, 然后是 .bss
 *            .. 0x07ff  启动栈, 自 BOOT_STACK_TOP 向下增长
 *   0x0800 .. 0x0801    Caterina 的启动键, 见 arch/avr/usb.c
 *   0x0802 .. 0x0aff    .noinit: 较大的任务栈, __task_stack
 *
 * 启动栈供 main() 使用到 sched_start(), 之后供 panic() 使用, 因此位于启动键之
 * 下, 任务栈无法到达. Makefile 检查两端; 此处修改时, Makefile 中的地址必须随之
 * 修改.
 */
#define BOOT_KEY_ADDR		0x0800
#define BOOT_STACK_TOP		(BOOT_KEY_ADDR - 1)
#define TASK_STACKS_START	(BOOT_KEY_ADDR + 2)

#endif /* _LEONIX_MEMLAYOUT_H */
