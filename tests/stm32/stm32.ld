/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Flash at 0x08000000, .bss at the bottom of SRAM; runtime.h gives the
 * rest of the SRAM layout.  The test programs have no .data.
 *
 * Flash 位于 0x08000000, .bss 位于 SRAM 底部; SRAM 的其余布局见 runtime.h.
 * 各测试程序没有 .data.
 */
SECTIONS
{
	.text 0x08000000 : {
		KEEP(*(.vectors))
		*(.text*)
		*(.rodata*)
	}
	.bss 0x20000000 (NOLOAD) : {
		__bss_start = .;
		*(.bss*) *(COMMON)
		__bss_end = .;
	}
	ASSERT(SIZEOF(.bss) <= 0x40, "tests/stm32: .bss overlaps the bit-band word")
	/DISCARD/ : { *(.data*) *(.ARM.*) *(.comment) }
}
