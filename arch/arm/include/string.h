/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ARM_STRING_H
#define _ARM_STRING_H

/*
 * The string functions the kernel uses, from lib/string.c.  avr-libc
 * provides them on AVR; for ARM the kernel is built against no C
 * library, as Linux is, and this header stands in for <string.h>.  The
 * arm-none-eabi-gcc of Homebrew, for one, comes without newlib.
 *
 * 内核使用的字符串函数, 实现在 lib/string.c. AVR 上由 avr-libc 提供; ARM 上
 * 内核与 Linux 一样不依赖 C 库编译, 本头文件代替 <string.h>. 例如 Homebrew
 * 的 arm-none-eabi-gcc 就不带 newlib.
 */
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
char *strchr(const char *s, int c);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);

#endif /* _ARM_STRING_H */
