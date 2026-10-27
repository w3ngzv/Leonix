// SPDX-License-Identifier: GPL-2.0-only
/*
 * The C string functions, for builds without a C library, as the file
 * of the same name in Linux.  Byte loops throughout: the kernel moves at most a
 * 1 KiB lxfs image at a time.  gcc also emits calls to memcpy() and
 * memset() for structure copies and initialisers.
 *
 * C 字符串函数, 用于不带 C 库的编译, 与 Linux 中的同名文件相同. 全部按字节
 * 循环: 内核一次移动的数据至多是 1 KiB 的 lxfs 映像. gcc 在复制结构体与初始化
 * 时也会生成对 memcpy() 与 memset() 的调用.
 */
#include <stddef.h>
#include <string.h>

void *memcpy(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	while (n--)
		*d++ = *s++;
	return dst;
}

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

void *memset(void *s, int c, size_t n)
{
	unsigned char *p = s;

	while (n--)
		*p++ = (unsigned char)c;
	return s;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *x = a, *y = b;

	for (; n; n--, x++, y++)
		if (*x != *y)
			return *x - *y;
	return 0;
}

size_t strlen(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	return p - s;
}

char *strchr(const char *s, int c)
{
	for (; *s != (char)c; s++)
		if (!*s)
			return NULL;
	return (char *)s;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

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

char *strcpy(char *dst, const char *src)
{
	char *d = dst;

	while ((*d++ = *src++))
		;
	return dst;
}

/*
 * C's strncpy(): pads with NUL up to @n and leaves @dst unterminated
 * when @src is @n bytes or longer, which lxfs relies on for names
 *
 * 与 C 的 strncpy() 相同: 以 NUL 填充到 @n 字节; @src 长度不小于 @n 时 @dst
 * 不带结束符, lxfs 的文件名依赖这一点
 */
char *strncpy(char *dst, const char *src, size_t n)
{
	size_t i;

	for (i = 0; i < n && src[i]; i++)
		dst[i] = src[i];
	for (; i < n; i++)
		dst[i] = '\0';
	return dst;
}

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

size_t strlcat(char *dst, const char *src, size_t size)
{
	size_t used = strlen(dst);

	if (used >= size)
		return used + strlen(src);
	return used + strlcpy(dst + used, src, size - used);
}
