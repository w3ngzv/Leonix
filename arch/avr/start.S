/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Reset entry and interrupt vector table for the ATmega32U4.
 *
 * Caterina jumps here with interrupts disabled and IVSEL clear, but on
 * the timeout path it leaves the USB controller running.  See
 * Documentation/booting.rst, section 3, for why the USB registers are
 * written unconditionally.
 *
 * ATmega32U4 的复位入口与中断向量表.
 *
 * Caterina 跳转到此处时中断已关闭且 IVSEL 为 0, 但在超时路径上 Caterina 让 USB
 * 控制器保持运行. USB 寄存器为何无条件写入, 见 Documentation/booting.rst 第 3
 * 节.
 */

#include <avr/io.h>

#include <leonix/memlayout.h>
#include <leonix/panic.h>

#define NR_VECTORS	43

	.section .vectors, "ax", @progbits
	.global	__vectors
__vectors:
	jmp	__reset
	.irp	n, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42
	.weak	__vector_\n
	.set	__vector_\n, __bad_interrupt
	jmp	__vector_\n
	.endr
	.if	(. - __vectors) != NR_VECTORS * 4
	.error	"vector table size does not match NR_VECTORS"
	.endif

	.text
	.global	__reset
__reset:
	clr	r1
	out	_SFR_IO_ADDR(SREG), r1
	ldi	r28, lo8(BOOT_STACK_TOP)
	ldi	r29, hi8(BOOT_STACK_TOP)
	out	_SFR_IO_ADDR(SPH), r29
	out	_SFR_IO_ADDR(SPL), r28

	/*
	 * return the USB controller and the PLL to their reset values
	 * 把 USB 控制器与 PLL 恢复为复位值
	 */
	ldi	r24, 1 << FRZCLK
	sts	_SFR_MEM_ADDR(USBCON), r24
	sts	_SFR_MEM_ADDR(USBINT), r1
	sts	_SFR_MEM_ADDR(UHWCON), r1
	out	_SFR_IO_ADDR(PLLCSR), r1
	ldi	r24, 1 << PDIV2
	out	_SFR_IO_ADDR(PLLFRQ), r24

	/*
	 * avr-gcc makes every object with .data refer to __do_copy_data,
	 * and every object with .bss to __do_clear_bss, expecting the
	 * libgcc versions in .init4.  Nothing jumps into .init4 under
	 * -nostartfiles, so those copies would sit in flash unused.
	 * Giving the two loops below the same names satisfies the
	 * references and keeps libgcc's out of the image.
	 *
	 * avr-gcc 使每个含 .data 的目标文件引用 __do_copy_data, 每个含 .bss 的
	 * 目标文件引用 __do_clear_bss, 预期由 .init4 中的 libgcc 版本提供. 在
	 * -nostartfiles 之下没有代码跳入 .init4, 那两段代码会闲置在 flash 中.
	 * 下面两个循环取同样的名字, 既满足引用, 又使 libgcc 的版本不进入映像.
	 */

	/*
	 * Copy .data from flash.  The image is below 64 KiB, so lpm reaches it.
	 *
	 * 从 flash 复制 .data. 映像小于 64 KiB, lpm 因此能够访问.
	 */
	.global	__do_copy_data
__do_copy_data:
	ldi	r26, lo8(__data_start)
	ldi	r27, hi8(__data_start)
	ldi	r30, lo8(__data_load_start)
	ldi	r31, hi8(__data_load_start)
	rjmp	2f
1:	lpm	r0, Z+
	st	X+, r0
2:	cpi	r26, lo8(__data_end)
	ldi	r24, hi8(__data_end)
	cpc	r27, r24
	brne	1b

	/* zero .bss; 清零 .bss */
	.global	__do_clear_bss
__do_clear_bss:
	ldi	r26, lo8(__bss_start)
	ldi	r27, hi8(__bss_start)
	rjmp	4f
3:	st	X+, r1
4:	cpi	r26, lo8(__bss_end)
	ldi	r24, hi8(__bss_end)
	cpc	r27, r24
	brne	3b

	call	main
	cli
5:	rjmp	5b

/*
 * Any vector without a handler ends up here.  The CPU has already
 * cleared the I flag on entry.  Which vector it was is lost, since every
 * unhandled vector is the same weak alias.
 *
 * 所有没有处理函数的向量都到达此处. 进入时 CPU 已清除 I 标志. 无法得知是哪个向
 * 量, 因为所有未处理的向量都是同一个弱别名.
 */
	.global	__bad_interrupt
__bad_interrupt:
	ldi	r24, PANIC_BAD_INTERRUPT
	ldi	r22, PANIC_NO_TASK
	rjmp	panic

/*
 * void panic(uint8_t reason, uint8_t task)
 *
 * The caller's stack may be the one that overflowed, so SP is moved to
 * the boot stack before panic_halt() runs any C.  No task uses the boot
 * stack, and it lies below the boot key.  The arguments stay in r24 and
 * r22, and r1 is cleared for C, since a bad interrupt can arrive with r1
 * in use.
 *
 * 调用者的栈可能正是溢出的那个栈, 因此在 panic_halt() 执行任何 C 代码之前把 SP
 * 移到启动栈. 没有任务使用启动栈, 且启动栈位于启动键之下. 参数保留在 r24 与 r22
 * 中; r1 为 C 代码清零, 因为意外中断到来时 r1 可能正在使用.
 */
	.global	panic
panic:
	cli
	ldi	r26, lo8(BOOT_STACK_TOP)
	ldi	r27, hi8(BOOT_STACK_TOP)
	out	_SFR_IO_ADDR(SPH), r27
	out	_SFR_IO_ADDR(SPL), r26
	clr	r1
	jmp	panic_halt
