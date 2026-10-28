/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_KERN_LEVELS_H
#define _LEONIX_KERN_LEVELS_H

/*
 * Linux v6.8's include/linux/kern_levels.h, without KERN_CONT and
 * LOGLEVEL_SCHED, which have no use here.  A level is two characters in
 * front of the format string; printk() takes them off and keeps the
 * number in the record.
 *
 * 取自 Linux v6.8 的 include/linux/kern_levels.h, 去掉了此处用不到的
 * KERN_CONT 与 LOGLEVEL_SCHED. 级别是格式串前面的两个字符, printk() 将其去掉,
 * 把数值保存在记录中.
 */
#define KERN_SOH	"\001"		/* ASCII Start Of Header */
#define KERN_SOH_ASCII	'\001'

#define KERN_EMERG	KERN_SOH "0"	/* system is unusable */
#define KERN_ALERT	KERN_SOH "1"	/* action must be taken immediately */
#define KERN_CRIT	KERN_SOH "2"	/* critical conditions */
#define KERN_ERR	KERN_SOH "3"	/* error conditions */
#define KERN_WARNING	KERN_SOH "4"	/* warning conditions */
#define KERN_NOTICE	KERN_SOH "5"	/* normal but significant condition */
#define KERN_INFO	KERN_SOH "6"	/* informational */
#define KERN_DEBUG	KERN_SOH "7"	/* debug-level messages */

#define KERN_DEFAULT	""		/* the default kernel loglevel */

/* integer equivalents of KERN_<LEVEL> */
#define LOGLEVEL_DEFAULT	-1	/* default (or last) loglevel */
#define LOGLEVEL_EMERG		0	/* system is unusable */
#define LOGLEVEL_ALERT		1	/* action must be taken immediately */
#define LOGLEVEL_CRIT		2	/* critical conditions */
#define LOGLEVEL_ERR		3	/* error conditions */
#define LOGLEVEL_WARNING	4	/* warning conditions */
#define LOGLEVEL_NOTICE		5	/* normal but significant condition */
#define LOGLEVEL_INFO		6	/* informational */
#define LOGLEVEL_DEBUG		7	/* debug-level messages */

#endif /* _LEONIX_KERN_LEVELS_H */
