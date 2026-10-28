/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_PRINTK_H
#define _LEONIX_PRINTK_H

#include <stdint.h>

#include <leonix/kern_levels.h>
#include <leonix/kernel.h>

/*
 * The kernel log keeps the last LOG_RECORDS messages, each stamped with
 * the seconds since boot and its level.  A message is cut to LOG_TEXT - 1
 * characters.  The length was chosen for a row of the LCD, which no
 * longer shows the log.
 *
 * Sixteen records hold everything written in the first seconds after
 * boot, before a terminal on the USB console is likely to be open.
 *
 * 内核日志保留最近 LOG_RECORDS 条消息, 每条带有开机以来的秒数与级别. 消息截断
 * 为 LOG_TEXT - 1 个字符. 该长度原本按 LCD 的一行选定, LCD 现已不再显示日志.
 *
 * 16 条记录能容纳开机后最初几秒写入的全部内容, 那时 USB 控制台上的终端多半尚未
 * 打开.
 */
#define LOG_RECORDS	16
#define LOG_TEXT	14

/*
 * the defaults of Linux's include/linux/printk.h, from lib/Kconfig.debug
 * 与 Linux 的 include/linux/printk.h 相同, 取自 lib/Kconfig.debug
 */
#define MESSAGE_LOGLEVEL_DEFAULT CONFIG_MESSAGE_LOGLEVEL_DEFAULT
#define CONSOLE_LOGLEVEL_DEFAULT CONFIG_CONSOLE_LOGLEVEL_DEFAULT

struct log_record {
	uint16_t secs;
	uint8_t level;
	char text[LOG_TEXT];
};

/*
 * the format string lives in flash, as for snprintk()
 * 格式串存放在 flash 中, 与 snprintk() 相同
 */
#define printk(fmt, ...)	printk_P(PSTR(fmt), ##__VA_ARGS__)

/*
 * as in Linux, a pr_debug() is checked by the compiler and then dropped
 * unless the file defines DEBUG
 * 与 Linux 相同, pr_debug() 经编译器检查后丢弃, 除非该文件定义了 DEBUG
 */
#define no_printk(fmt, ...) do {					\
	if (0)								\
		printk_P(PSTR(fmt), ##__VA_ARGS__);			\
} while (0)

#define pr_emerg(fmt, ...) \
	printk(KERN_EMERG fmt, ##__VA_ARGS__)
#define pr_alert(fmt, ...) \
	printk(KERN_ALERT fmt, ##__VA_ARGS__)
#define pr_crit(fmt, ...) \
	printk(KERN_CRIT fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...) \
	printk(KERN_ERR fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...) \
	printk(KERN_WARNING fmt, ##__VA_ARGS__)
#define pr_notice(fmt, ...) \
	printk(KERN_NOTICE fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...) \
	printk(KERN_INFO fmt, ##__VA_ARGS__)
#ifdef DEBUG
#define pr_debug(fmt, ...) \
	printk(KERN_DEBUG fmt, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...) \
	no_printk(KERN_DEBUG fmt, ##__VA_ARGS__)
#endif

void printk_P(const char *fmt, ...);
uint16_t log_newest(void);
int log_read(uint16_t seq, struct log_record *rec);
void register_console(void (*kick)(void));
int suppress_message_printing(uint8_t level);

#endif /* _LEONIX_PRINTK_H */
