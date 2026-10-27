/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_PGMSPACE_H
#define _LEONIX_PGMSPACE_H

/*
 * Constant data kept in flash.  AVR reaches flash through an address
 * space of its own, with lpm, and avr-libc's <avr/pgmspace.h> names the
 * macros and _P string functions below.  On Cortex-M, and in host builds
 * for testing, flash and SRAM share one address space and const data
 * stays in flash by itself, so the same names become plain accesses.
 *
 * 存放在 flash 中的常量数据. AVR 的 flash 属于单独的地址空间, 以 lpm 访问,
 * 下列宏与 _P 字符串函数由 avr-libc 的 <avr/pgmspace.h> 提供. Cortex-M 以及
 * 为测试而进行的主机编译只有一个地址空间, const 数据本身就留在 flash 中, 因此
 * 同样的名字成为普通的访问.
 */
#ifdef __AVR__
#include <avr/pgmspace.h>
#else
#include <stddef.h>
#include <stdint.h>

#include <leonix/string.h>

#define PROGMEM
#define PSTR(s)			(s)
#define pgm_read_byte(p)	(*(const uint8_t *)(p))
#define pgm_read_word(p)	(*(const uint16_t *)(p))
#define pgm_read_ptr(p)		(*(void *const *)(p))

#define strcmp_P		strcmp
#define strncmp_P		strncmp
#define strlen_P		strlen
#define strcpy_P		strcpy

/*
 * avr-libc's strlcpy_P(): copy at most @size - 1 bytes, always
 * terminate, return the length of @src
 * 与 avr-libc 的 strlcpy_P() 相同: 至多复制 @size - 1 字节, 总是写入结束符,
 * 返回 @src 的长度
 */
static inline size_t strlcpy_P(char *dst, const char *src, size_t size)
{
	size_t len = strlen(src);

	if (size) {
		size_t n = len < size - 1 ? len : size - 1;

		memcpy(dst, src, n);
		dst[n] = '\0';
	}
	return len;
}
#endif

#endif /* _LEONIX_PGMSPACE_H */
