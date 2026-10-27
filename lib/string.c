// SPDX-License-Identifier: GPL-2.0-only
/*
 * The generic string functions, as the file of the same name in Linux:
 * each is built unless <asm/string.h> defines __HAVE_ARCH_<NAME>, so on
 * AVR, where avr-libc provides them all, nothing is.  Byte loops
 * throughout: the kernel moves at most a 1 KiB lxfs image at a time.
 * gcc also emits calls to memcpy() and memset() for structure copies
 * and initialisers.
 *
 * 通用的字符串函数, 与 Linux 中的同名文件相同: <asm/string.h> 未定义
 * __HAVE_ARCH_<NAME> 时才编译对应的函数, 因此在由 avr-libc 提供全部函数的
 * AVR 上, 本文件不编译出任何函数. 全部按字节循环: 内核一次移动的数据至多是
 * 1 KiB 的 lxfs 映像. gcc 在复制结构体与初始化时也会生成对 memcpy() 与
 * memset() 的调用.
 */
#include <stddef.h>

#include <leonix/string.h>

#ifndef __HAVE_ARCH_MEMCPY
void *memcpy(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	while (n--)
		*d++ = *s++;
	return dst;
}
#endif

#ifndef __HAVE_ARCH_MEMMOVE
void *memmove(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	if (d <= s)
		return memcpy(dst, src, n);
	while (n--)
		d[n] = s[n];
	return dst;
}
#endif

#ifndef __HAVE_ARCH_MEMSET
void *memset(void *s, int c, size_t n)
{
	unsigned char *p = s;

	while (n--)
		*p++ = (unsigned char)c;
	return s;
}
#endif

#ifndef __HAVE_ARCH_MEMCMP
int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *x = a, *y = b;

	for (; n; n--, x++, y++)
		if (*x != *y)
			return *x - *y;
	return 0;
}
#endif

#ifndef __HAVE_ARCH_STRLEN
size_t strlen(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	return p - s;
}
#endif

#ifndef __HAVE_ARCH_STRCHR
char *strchr(const char *s, int c)
{
	for (; *s != (char)c; s++)
		if (!*s)
			return NULL;
	return (char *)s;
}
#endif

#ifndef __HAVE_ARCH_STRCMP
int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}
#endif

#ifndef __HAVE_ARCH_STRNCMP
int strncmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		if (*a != *b)
			return (unsigned char)*a - (unsigned char)*b;
		if (!*a)
			break;
	}
	return 0;
}
#endif

#ifndef __HAVE_ARCH_STRCPY
char *strcpy(char *dst, const char *src)
{
	char *d = dst;

	while ((*d++ = *src++))
		;
	return dst;
}
#endif

/*
 * C's strncpy(): pads with NUL up to @n and leaves @dst unterminated
 * when @src is @n bytes or longer, which lxfs relies on for names
 *
 * 与 C 的 strncpy() 相同: 以 NUL 填充到 @n 字节; @src 长度不小于 @n 时 @dst
 * 不带结束符, lxfs 的文件名依赖这一点
 */
#ifndef __HAVE_ARCH_STRNCPY
char *strncpy(char *dst, const char *src, size_t n)
{
	size_t i;

	for (i = 0; i < n && src[i]; i++)
		dst[i] = src[i];
	for (; i < n; i++)
		dst[i] = '\0';
	return dst;
}
#endif

#ifndef __HAVE_ARCH_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t size)
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

#ifndef __HAVE_ARCH_STRLCAT
size_t strlcat(char *dst, const char *src, size_t size)
{
	size_t used = strlen(dst);

	if (used >= size)
		return used + strlen(src);
	return used + strlcpy(dst + used, src, size - used);
}
#endif
