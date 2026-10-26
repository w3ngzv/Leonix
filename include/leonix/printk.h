/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_PRINTK_H
#define _LEONIX_PRINTK_H

#include <stdint.h>

#include <leonix/kernel.h>

/*
 * The kernel log keeps the last LOG_RECORDS messages, each stamped with
 * the seconds since boot.  A message is cut to LOG_TEXT - 1 characters,
 * which with the stamp makes a 16-byte record.  The length was chosen
 * for a row of the LCD, which no longer shows the log.
 *
 * Sixteen records hold everything written in the first seconds after
 * boot, before a terminal on the USB console is likely to be open.
 *
 * 内核日志保留最近 LOG_RECORDS 条消息, 每条带有开机以来的秒数. 消息截断为
 * LOG_TEXT - 1 个字符, 加上时间戳正好是 16 字节的记录. 该长度原本按 LCD 的一行
 * 选定, LCD 现已不再显示日志.
 *
 * 16 条记录能容纳开机后最初几秒写入的全部内容, 那时 USB 控制台上的终端多半尚未
 * 打开.
 */
#define LOG_RECORDS	16
#define LOG_TEXT	14

struct log_record {
	uint16_t secs;
	char text[LOG_TEXT];
};

/*
 * the format string lives in flash, as for snprintk()
 * 格式串存放在 flash 中, 与 snprintk() 相同
 */
#define printk(fmt, ...)	printk_P(PSTR(fmt), ##__VA_ARGS__)

void printk_P(const char *fmt, ...);
uint16_t log_newest(void);
int log_read(uint16_t seq, struct log_record *rec);
void register_console(void (*kick)(void));

#endif /* _LEONIX_PRINTK_H */
