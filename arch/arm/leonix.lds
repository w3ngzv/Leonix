/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The STM32F103C8 has 64 KiB of Flash at 0x08000000 and 20 KiB of SRAM
 * at 0x20000000 (DS5319 table 2).  The last 8 KiB of Flash are left to
 * lxfs, see drivers/stm32_flash.c.  The kernel's SRAM is kept within
 * 8 KiB, the SRAM of the STM32F100RB that qemu-system-arm's
 * stm32vldiscovery machine has, so the same image boots there.
 *
 * SRAM, from the bottom: .data, .bss, the task stacks in .noinit, then
 * the boot stack, which becomes the main stack that every exception
 * handler and panic() runs on.
 *
 * STM32F103C8 有位于 0x08000000 的 64 KiB Flash 与位于 0x20000000 的 20 KiB
 * SRAM (DS5319 表 2). Flash 的最后 8 KiB 留给 lxfs, 见 drivers/stm32_flash.c.
 * 内核的 SRAM 用量保持在 8 KiB 之内, 即 qemu-system-arm 的 stm32vldiscovery
 * 机器上 STM32F100RB 的 SRAM 大小, 因此同一映像可以在其中启动.
 *
 * SRAM 自底向上依次为: .data、.bss、.noinit 中的任务栈, 然后是启动栈. 启动栈
 * 随后成为主栈, 所有异常处理程序与 panic() 都在其上运行.
 */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(reset_handler)

LXFS_FLASH_BYTES = 8K;
/*
 * TODO: the main stack's use has not been measured; fill it with a
 * pattern in reset_handler and read it back as sched_stack_free() does.
 *
 * 待办: 主栈的用量没有测量过; 在 reset_handler 中填充图样, 再像
 * sched_stack_free() 那样读回.
 */
BOOT_STACK_BYTES = 1K;
QEMU_RAM_BYTES = 8K;

MEMORY
{
	flash (rx)  : ORIGIN = 0x08000000, LENGTH = 64K - LXFS_FLASH_BYTES
	lxfs (r)    : ORIGIN = 0x08000000 + 64K - LXFS_FLASH_BYTES,
		      LENGTH = LXFS_FLASH_BYTES
	ram (rwx)   : ORIGIN = 0x20000000, LENGTH = 20K
}

SECTIONS
{
	/*
	 * The vector table must be first: the core fetches the initial MSP
	 * and the reset vector from address 0, an alias of Flash when BOOT0
	 * is low (RM0008 3.4).
	 *
	 * 向量表必须在最前: 内核从地址 0 取初始 MSP 与复位向量, BOOT0 为低时该
	 * 地址是 Flash 的别名 (RM0008 3.4).
	 */
	.text : {
		KEEP(*(.vectors))
		*(.text .text.*)
		*(.rodata .rodata.*)
		/*
		 * the initcall table, as INIT_CALLS in Linux's
		 * include/asm-generic/vmlinux.lds.h, sorted so level 1
		 * runs first
		 *
		 * initcall 表, 与 Linux 的 include/asm-generic/vmlinux.lds.h
		 * 中的 INIT_CALLS 相同, 排序后级别 1 最先运行
		 */
		. = ALIGN(4);
		__initcall_start = .;
		KEEP(*(SORT(.initcall*.init)))
		__initcall_end = .;
		. = ALIGN(4);
	} > flash

	.ARM.exidx : {
		*(.ARM.exidx*)
	} > flash

	.data : {
		__data_start = .;
		*(.data .data.*)
		. = ALIGN(4);
		__data_end = .;
	} > ram AT > flash
	__data_load_start = LOADADDR(.data);

	.bss (NOLOAD) : {
		__bss_start = .;
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(4);
		__bss_end = .;
	} > ram

	.noinit (NOLOAD) : {
		*(.noinit .noinit.*)
	} > ram

	.boot_stack (NOLOAD) : {
		. = ALIGN(8);
		. += BOOT_STACK_BYTES;
		__boot_stack_top = .;
	} > ram

	__ram_start = ORIGIN(ram);
	__ram_end = __boot_stack_top;

	__lxfs_flash_start = ORIGIN(lxfs);
	__lxfs_flash_end = ORIGIN(lxfs) + LENGTH(lxfs);

	/DISCARD/ : {
		*(.ARM.attributes) *(.comment)
	}
}

ASSERT(__boot_stack_top - ORIGIN(ram) <= QEMU_RAM_BYTES,
       "the kernel's SRAM no longer fits the 8 KiB of QEMU's stm32vldiscovery")
