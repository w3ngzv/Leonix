/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_AVR_IRQFLAGS_H
#define _ASM_AVR_IRQFLAGS_H

#include <avr/interrupt.h>
#include <avr/io.h>

/*
 * The I flag of SREG, as avr-libc's ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
 * keeps it.  cli() and sei() carry a memory clobber; the restore gets
 * its own before SREG is written, so that no store in the section is
 * moved past it.
 *
 * SREG 的 I 标志, 保存方式与 avr-libc 的 ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
 * 相同. cli() 与 sei() 带有 memory clobber; 恢复时在写入 SREG 之前另加一个,
 * 使临界区内的写入不会被移到其后.
 */
static inline void arch_local_irq_disable(void)
{
	cli();
}

static inline void arch_local_irq_enable(void)
{
	sei();
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags = SREG;

	cli();
	return flags;
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	__asm__ __volatile__("" ::: "memory");
	SREG = flags;
}

#endif /* _ASM_AVR_IRQFLAGS_H */
