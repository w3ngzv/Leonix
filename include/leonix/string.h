/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_STRING_H
#define _LEONIX_STRING_H

/*
 * The string functions of the kernel, as Linux's include/linux/string.h:
 * <asm/string.h> may provide any of them and define __HAVE_ARCH_<NAME>,
 * and lib/string.c provides the rest.  Kernel code includes this header,
 * never the C library's <string.h>.
 *
 * 内核的字符串函数, 与 Linux 的 include/linux/string.h 相同: <asm/string.h>
 * 可以提供其中任意几个并定义 __HAVE_ARCH_<NAME>, 其余由 lib/string.c 提供.
 * 内核代码包含本头文件, 不包含 C 库的 <string.h>.
 */
#include <stddef.h>

#include <asm/string.h>

#ifndef __HAVE_ARCH_MEMCPY
void *memcpy(void *dst, const void *src, size_t n);
#endif
#ifndef __HAVE_ARCH_MEMMOVE
void *memmove(void *dst, const void *src, size_t n);
#endif
#ifndef __HAVE_ARCH_MEMSET
void *memset(void *s, int c, size_t n);
#endif
#ifndef __HAVE_ARCH_MEMCMP
int memcmp(const void *a, const void *b, size_t n);
#endif
#ifndef __HAVE_ARCH_STRLEN
size_t strlen(const char *s);
#endif
#ifndef __HAVE_ARCH_STRCHR
char *strchr(const char *s, int c);
#endif
#ifndef __HAVE_ARCH_STRCMP
int strcmp(const char *a, const char *b);
#endif
#ifndef __HAVE_ARCH_STRNCMP
int strncmp(const char *a, const char *b, size_t n);
#endif
#ifndef __HAVE_ARCH_STRCPY
char *strcpy(char *dst, const char *src);
#endif
#ifndef __HAVE_ARCH_STRNCPY
char *strncpy(char *dst, const char *src, size_t n);
#endif
#ifndef __HAVE_ARCH_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t size);
#endif
#ifndef __HAVE_ARCH_STRLCAT
size_t strlcat(char *dst, const char *src, size_t size);
#endif

#endif /* _LEONIX_STRING_H */
