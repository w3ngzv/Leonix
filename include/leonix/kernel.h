/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_KERNEL_H
#define _LEONIX_KERNEL_H

#include <stdarg.h>
#include <stdint.h>

#ifdef __AVR__
#include <avr/pgmspace.h>
#else
/*
 * host builds of lib/, for testing: one address space
 * 为测试而在主机上编译 lib/: 只有一个地址空间
 */
#define PSTR(s)			(s)
#define pgm_read_byte(p)	(*(const uint8_t *)(p))
#endif

/*
 * Format strings live in flash, so that they cost no SRAM: AVR copies
 * every initialised string in .data to RAM at reset, see
 * Documentation/notes/avr.rst.  The macros wrap the literal in PSTR().
 *
 * 格式串存放在 flash 中, 以免占用 SRAM: AVR 在复位时把 .data 中每个已初始化的
 * 字符串都复制到 RAM, 见 Documentation/notes/avr.rst. 这些宏以 PSTR() 包装字面
 * 量.
 */
#define snprintk(buf, size, fmt, ...) \
	snprintk_P((buf), (size), PSTR(fmt), ##__VA_ARGS__)

uint8_t vsnprintk_P(char *buf, uint8_t size, const char *fmt, va_list ap);
uint8_t snprintk_P(char *buf, uint8_t size, const char *fmt, ...);

#endif /* _LEONIX_KERNEL_H */
