// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host test of kernel/printk.c, run by "make check": the level a
 * message is stored with, and the text left once the level is taken off.
 * The Makefile sets the two CONFIG_ defaults that autoconf.h would.
 *
 * kernel/printk.c 的主机测试, 由 "make check" 运行: 检查消息所存的级别, 以及
 * 去掉级别之后的文本. 两个 CONFIG_ 默认值由 Makefile 给出, 代替 autoconf.h.
 */
#include <stdio.h>
#include <string.h>

#include <leonix/jiffies.h>
#include <leonix/printk.h>

static int fails;
static int kicks;

uint32_t get_jiffies(void)
{
	return 3 * HZ;
}

static void kick(void)
{
	kicks++;
}

/*
 * the newest record must have @level and @text
 * 最新的记录必须具有级别 @level 与文本 @text
 */
static void expect(int line, uint8_t level, const char *text)
{
	struct log_record rec;

	if (log_read(log_newest(), &rec) < 0) {
		printf("FAIL line %d: no record\n", line);
		fails++;
		return;
	}
	if (rec.level != level || strcmp(rec.text, text) || rec.secs != 3) {
		printf("FAIL line %d: got <%u>[%s] want <%u>[%s]\n", line,
		       rec.level, rec.text, level, text);
		fails++;
	}
}

int main(void)
{
	register_console(kick);

	printk_P("plain %d", 1);
	expect(__LINE__, MESSAGE_LOGLEVEL_DEFAULT, "plain 1");
	pr_emerg("emerg");
	expect(__LINE__, LOGLEVEL_EMERG, "emerg");
	pr_err("err %u", 3);
	expect(__LINE__, LOGLEVEL_ERR, "err 3");
	pr_warn("warn");
	expect(__LINE__, LOGLEVEL_WARNING, "warn");
	pr_info("info");
	expect(__LINE__, LOGLEVEL_INFO, "info");
	printk_P(KERN_DEBUG "debug");
	expect(__LINE__, LOGLEVEL_DEBUG, "debug");

	/*
	 * not a level: kept as text, at the default level
	 * 不是级别: 作为文本保留, 使用默认级别
	 */
	printk_P(KERN_SOH "9x");
	expect(__LINE__, MESSAGE_LOGLEVEL_DEFAULT, KERN_SOH "9x");
	printk_P(KERN_SOH);
	expect(__LINE__, MESSAGE_LOGLEVEL_DEFAULT, KERN_SOH);

	/*
	 * the level costs none of the text
	 * 级别不占用文本的长度
	 */
	pr_info("abcdefghijklmnop");
	expect(__LINE__, LOGLEVEL_INFO, "abcdefghijklm");

	/*
	 * pr_debug() without DEBUG adds nothing
	 * 未定义 DEBUG 时 pr_debug() 不添加记录
	 */
	{
		uint16_t seq = log_newest();

		pr_debug("gone %d", 1);
		if (log_newest() != seq) {
			printf("FAIL line %d: pr_debug() logged\n", __LINE__);
			fails++;
		}
	}

	if (suppress_message_printing(LOGLEVEL_INFO) ||
	    !suppress_message_printing(LOGLEVEL_DEBUG)) {
		printf("FAIL line %d: console level\n", __LINE__);
		fails++;
	}
	if (kicks != (int)log_newest()) {
		printf("FAIL line %d: %d kicks for %u records\n", __LINE__,
		       kicks, log_newest());
		fails++;
	}

	if (fails) {
		printf("printk: %d failed\n", fails);
		return 1;
	}
	printf("printk: all passed\n");
	return 0;
}
