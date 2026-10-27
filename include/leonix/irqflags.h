/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_IRQFLAGS_H
#define _LEONIX_IRQFLAGS_H

#include <asm/irqflags.h>

/*
 * Linux's include/linux/irqflags.h, without the tracing.  A save and
 * restore pair keeps the caller's state, so code that may run with
 * interrupts already off uses it in place of disable and enable.  Each
 * arch/<arch>/include/asm/irqflags.h supplies the arch_ functions, with
 * a compiler barrier in every one.
 *
 * Linux 的 include/linux/irqflags.h, 去掉了跟踪部分. save 与 restore 成对使用
 * 时保持调用者原有的状态, 因此可能在已关中断时运行的代码用这一对, 不用 disable
 * 与 enable. arch_ 各函数由 arch/<arch>/include/asm/irqflags.h 提供, 每个都带
 * 有编译器屏障.
 */
#define local_irq_disable()	arch_local_irq_disable()
#define local_irq_enable()	arch_local_irq_enable()
#define local_irq_save(flags)	((flags) = arch_local_irq_save())
#define local_irq_restore(flags)	arch_local_irq_restore(flags)

#endif /* _LEONIX_IRQFLAGS_H */
