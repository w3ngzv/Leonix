/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_PRINTK_H
#define _LEONIX_PRINTK_H

#include <stdint.h>

#include <leonix/kernel.h>

/*
 * The kernel log keeps the last LOG_RECORDS messages, each stamped with
 * the seconds since boot.  A message is cut to LOG_TEXT - 1 characters,
 * which with the stamp fills one row of the 16-column LCD.
 */
#define LOG_RECORDS	4
#define LOG_TEXT	14

struct log_record {
	uint16_t secs;
	char text[LOG_TEXT];
};

/* The format string lives in flash, as for snprintk(). */
#define printk(fmt, ...)	printk_P(PSTR(fmt), ##__VA_ARGS__)

void printk_P(const char *fmt, ...);
uint16_t log_last(struct log_record *rec);

#endif /* _LEONIX_PRINTK_H */
