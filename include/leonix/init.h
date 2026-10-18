/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_INIT_H
#define _LEONIX_INIT_H

#include <stdint.h>

/*
 * Initcalls, after Linux's include/linux/init.h.  A function named in
 * one of the macros below is called once by do_initcalls() before the
 * scheduler starts, level 1 first, and returns 0 or a negative errno.
 * The pointer to it goes in the section .initcallN.init, which
 * arch/avr/leonix.lds gathers into flash.
 */
typedef int (*initcall_t)(void);

#define __define_initcall(fn, id)					\
	static const initcall_t __initcall_##fn##id			\
	__attribute__((used, section(".initcall" #id ".init"))) = fn

#define core_initcall(fn)		__define_initcall(fn, 1)
#define postcore_initcall(fn)		__define_initcall(fn, 2)
#define arch_initcall(fn)		__define_initcall(fn, 3)
#define subsys_initcall(fn)		__define_initcall(fn, 4)
#define fs_initcall(fn)			__define_initcall(fn, 5)
#define device_initcall(fn)		__define_initcall(fn, 6)
#define late_initcall(fn)		__define_initcall(fn, 7)

#define __initcall(fn)			device_initcall(fn)

void do_initcalls(void);
int init_start(uint8_t *stack, uint16_t size);

#endif /* _LEONIX_INIT_H */
