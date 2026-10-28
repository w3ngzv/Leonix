/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _HOST_ASM_IRQFLAGS_H
#define _HOST_ASM_IRQFLAGS_H

/*
 * For the host tests, which have no interrupts to disable.
 * 用于主机测试, 主机测试中没有需要关闭的中断.
 */
static inline void arch_local_irq_disable(void)
{
}

static inline void arch_local_irq_enable(void)
{
}

static inline unsigned long arch_local_irq_save(void)
{
	return 0;
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	(void)flags;
}

#endif /* _HOST_ASM_IRQFLAGS_H */
