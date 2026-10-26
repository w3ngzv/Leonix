// SPDX-License-Identifier: GPL-2.0-only
/*
 * A small formatter, in place of avr-libc's vfprintf.
 *
 * Conversions: %c %s %S %d %u %x %%.  %S takes a string in flash.  An
 * optional 0 flag and a width pad numbers and strings on the left, with
 * zeros or spaces, or with - on the right with spaces; a width of * is
 * taken from an int argument, as in C.
 * l before d, u or x takes a 32-bit argument, since int is 16 bits here.
 * Anything else in a conversion is copied as it is.
 *
 * Output is cut at @size - 1 characters and always terminated.
 *
 * 小型格式化函数, 代替 avr-libc 的 vfprintf.
 *
 * 支持的转换: %c %s %S %d %u %x %%. %S 取 flash 中的字符串. 可选的 0 标志
 * 与宽度在左侧以零或空格填充数字与字符串, 加 - 则在右侧以空格填充; 宽度为
 * * 时与 C 相同, 取自一个 int 参数. d、u、x 之前的 l 取 32 位参数, 因为此处
 * int 为 16 位. 转换中的其他字符原样复制.
 *
 * 输出在 @size - 1 个字符处截断, 并且总有结束符.
 */
#include <leonix/kernel.h>

/*
 * decimal digits in UINT32_MAX; hex needs 8
 * UINT32_MAX 的十进制位数; 十六进制只需 8 位
 */
#define MAX_DIGITS	10

struct out {
	char *p;
	char *end;		/* last byte, kept for the terminator */
};

static void put(struct out *o, char c)
{
	if (o->p < o->end)
		*o->p++ = c;
}

static void pad(struct out *o, char c, uint8_t n)
{
	while (n--)
		put(o, c);
}

static void put_number(struct out *o, uint32_t v, uint8_t base, int neg,
		       uint8_t width, char padc, int left)
{
	char digits[MAX_DIGITS];
	uint8_t n = 0, len;

	do {
		uint8_t d = v % base;

		digits[n++] = d < 10 ? '0' + d : 'a' + d - 10;
		v /= base;
	} while (v);

	len = n + neg;
	if (neg && padc == '0')
		put(o, '-');
	if (!left && width > len)
		pad(o, padc, width - len);
	if (neg && padc != '0')
		put(o, '-');
	while (n)
		put(o, digits[--n]);
	if (left && width > len)
		pad(o, ' ', width - len);
}

static void put_string(struct out *o, const char *s, int in_flash,
		       uint8_t width, int left)
{
	const char *q = s;
	uint8_t len = 0, n;

	while (in_flash ? pgm_read_byte(q) : *q) {
		q++;
		len++;
	}
	if (!left && width > len)
		pad(o, ' ', width - len);
	n = len;
	while (n--) {
		put(o, in_flash ? pgm_read_byte(s) : *s);
		s++;
	}
	if (left && width > len)
		pad(o, ' ', width - len);
}

/**
 * vsnprintk_P() - format into a buffer
 * @buf: where to store the text
 * @size: bytes in @buf, the terminator included
 * @fmt: format, a string in flash
 * @ap: arguments for @fmt
 *
 * 按 flash 中的格式串 @fmt 与参数 @ap 格式化到 @buf, @size 是 @buf 的字节数
 * (含结束符).
 *
 * Return: characters stored, not counting the terminator.
 * 写入的字符数, 不含结束符.
 */
uint8_t vsnprintk_P(char *buf, uint8_t size, const char *fmt, va_list ap)
{
	struct out o = { buf, buf + size - 1 };
	char c;

	if (size == 0)
		return 0;

	while ((c = pgm_read_byte(fmt++))) {
		char padc = ' ';
		uint8_t width = 0;
		int is_long = 0, left = 0;

		if (c != '%') {
			put(&o, c);
			continue;
		}
		c = pgm_read_byte(fmt++);
		if (c == '-') {
			left = 1;
			c = pgm_read_byte(fmt++);
		} else if (c == '0') {
			padc = '0';
			c = pgm_read_byte(fmt++);
		}
		if (c == '*') {
			width = (uint8_t)va_arg(ap, int);
			c = pgm_read_byte(fmt++);
		}
		while (c >= '0' && c <= '9') {
			width = width * 10 + (c - '0');
			c = pgm_read_byte(fmt++);
		}
		if (c == 'l') {
			is_long = 1;
			c = pgm_read_byte(fmt++);
		}

		switch (c) {
		case 'c':
			put(&o, (char)va_arg(ap, int));
			break;
		case 's':
			put_string(&o, va_arg(ap, const char *), 0, width, left);
			break;
		case 'S':
			put_string(&o, va_arg(ap, const char *), 1, width, left);
			break;
		case 'd': {
			int32_t v = is_long ? va_arg(ap, int32_t)
					    : va_arg(ap, int);
			uint32_t mag = v < 0 ? -(uint32_t)v : (uint32_t)v;

			put_number(&o, mag, 10, v < 0, width, padc, left);
			break;
		}
		case 'u':
		case 'x':
			put_number(&o, is_long ? va_arg(ap, uint32_t)
					       : va_arg(ap, unsigned int),
				   c == 'u' ? 10 : 16, 0, width, padc, left);
			break;
		case '%':
			put(&o, '%');
			break;
		case '\0':
			put(&o, '%');	/* a lone % at the end */
			fmt--;
			break;
		default:
			put(&o, '%');	/* unknown: copied as written */
			put(&o, c);
			break;
		}
	}
	*o.p = '\0';
	return o.p - buf;
}

/**
 * snprintk_P() - format into a buffer
 * @buf: where to store the text
 * @size: bytes in @buf, the terminator included
 * @fmt: format, a string in flash
 * @...: arguments for @fmt
 *
 * Called through snprintk(), which puts the literal in flash.
 *
 * 与 vsnprintk_P() 相同, 参数以可变参数传入. 经由 snprintk() 调用,
 * snprintk() 把字面量放在 flash 中.
 *
 * Return: characters stored, not counting the terminator.
 * 写入的字符数, 不含结束符.
 */
uint8_t snprintk_P(char *buf, uint8_t size, const char *fmt, ...)
{
	va_list ap;
	uint8_t n;

	va_start(ap, fmt);
	n = vsnprintk_P(buf, size, fmt, ap);
	va_end(ap);
	return n;
}
