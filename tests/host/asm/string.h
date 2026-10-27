/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _HOST_ASM_STRING_H
#define _HOST_ASM_STRING_H

/*
 * For the host tests, which build kernel files against the host's C
 * library: its string functions take the place of lib/string.c.
 *
 * 用于主机测试, 这些测试以主机的 C 库编译内核文件: 由其字符串函数代替
 * lib/string.c.
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

#endif /* _HOST_ASM_STRING_H */
