/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_AVR_STRING_H
#define _ASM_AVR_STRING_H

/*
 * avr-libc's string functions, written in assembly, take the place of
 * every one in lib/string.c.
 *
 * avr-libc 以汇编编写的字符串函数代替 lib/string.c 中的全部函数.
 */
#include <string.h>

#define __HAVE_ARCH_MEMCPY
#define __HAVE_ARCH_MEMMOVE
#define __HAVE_ARCH_MEMSET
#define __HAVE_ARCH_MEMCMP
#define __HAVE_ARCH_STRLEN
#define __HAVE_ARCH_STRCHR
#define __HAVE_ARCH_STRCMP
#define __HAVE_ARCH_STRNCMP
#define __HAVE_ARCH_STRCPY
#define __HAVE_ARCH_STRNCPY
#define __HAVE_ARCH_STRLCPY
#define __HAVE_ARCH_STRLCAT

#endif /* _ASM_AVR_STRING_H */
