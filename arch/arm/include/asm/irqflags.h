/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ARM_IRQFLAGS_H
#define _ASM_ARM_IRQFLAGS_H

/*
 * PRIMASK, which masks every exception of configurable priority
 * (PM0056 2.1.3), as Linux's header of the same name for ARM has it
 * for ARMv7-M.  Each instruction carries a memory clobber, so the compiler
 * keeps the accesses of a critical section inside it.
 *
 * PRIMASK 屏蔽所有优先级可配置的异常 (PM0056 2.1.3), 用法与 Linux 中 ARM 的
 * 同名头文件在 ARMv7-M 上相同. 每条指令都带有 memory
 * clobber, 编译器因此把临界区内的访问保持在临界区之内.
 */
static inline void arch_local_irq_disable(void)
{
	__asm__ __volatile__("cpsid i" ::: "memory");
}

static inline void arch_local_irq_enable(void)
{
	__asm__ __volatile__("cpsie i" ::: "memory");
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags;

	__asm__ __volatile__("mrs %0, primask\n\tcpsid i"
			     : "=r" (flags) : : "memory");
	return flags;
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	__asm__ __volatile__("msr primask, %0" : : "r" (flags) : "memory");
}

#endif /* _ASM_ARM_IRQFLAGS_H */
