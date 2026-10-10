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
 */
#include <leonix/kernel.h>

/* uint32_t has at most 10 decimal digits, 8 hex digits. */
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

/* Digits are produced from the right, as in lcd_put_right() before. */
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

/*
 * Format @fmt, a string in flash, into @buf of @size bytes.  Returns the
 * number of characters stored, not counting the terminator.
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

uint8_t snprintk_P(char *buf, uint8_t size, const char *fmt, ...)
{
	va_list ap;
	uint8_t n;

	va_start(ap, fmt);
	n = vsnprintk_P(buf, size, fmt, ap);
	va_end(ap);
	return n;
}
