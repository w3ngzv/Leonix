// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host test of lib/vsprintf.c, run by "make check".  The formatter has
 * no AVR dependency apart from reading the format string from flash,
 * which include/leonix/kernel.h maps to an ordinary read off the AVR.
 */
#include <stdio.h>
#include <string.h>
#include <leonix/kernel.h>
static int fails;
#define T(exp, size, fmt, ...) do { char b[64]; uint8_t n = snprintk(b, size, fmt, ##__VA_ARGS__); \
	if (strcmp(b, exp) || n != strlen(exp)) { printf("FAIL %-18s got [%s] n=%u want [%s]\n", fmt, b, n, exp); fails++; } } while (0)
int main(void) {
	T("idle  99%", 64, "idle %3u%%", 99);
	T("  0", 64, "%3u", 0);
	T("-5", 64, "%d", -5);
	T("   -5", 64, "%5d", -5);
	T("-0005", 64, "%05d", -5);
	T("-32768", 64, "%d", -32768);
	T("65535", 64, "%u", 65535u);
	T("4294967295", 64, "%lu", 4294967295ul);
	T("-2147483648", 64, "%ld", (int32_t)-2147483647 - 1);
	T("0x0027", 64, "0x%04x", 0x27);
	T("deadbeef", 64, "%lx", 0xdeadbeeful);
	T("up        123 s", 64, "up%11lu s", 123ul);
	T("[ ab]", 64, "[%3s]", "ab");
	T("[flash]", 64, "[%S]", "flash");
	T("c=Z", 64, "c=%c", 'Z');
	T("abcd", 5, "abcdefgh");
	T("12", 3, "%u", 12345);
	T("", 1, "xyz");
	T("50%", 64, "%u%%", 50);
	T("trail%", 64, "trail%");
	T("[%q]", 64, "[%q]");
	T("   42", 64, "%*u", 5, 42);
	T("[  x]", 64, "[%*s]", 3, "x");
	T("00042", 64, "%0*u", 5, 42);
	printf(fails ? "%d failures\n" : "all passed\n", fails);
	return fails != 0;
}
