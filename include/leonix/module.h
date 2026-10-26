/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_MODULE_H
#define _LEONIX_MODULE_H

#include <leonix/init.h>

/*
 * Leonix has no module loader, so every driver is built in and
 * module_init() is the device initcall, as in Linux with MODULE unset.
 *
 * Leonix 没有模块加载程序, 每个驱动都编入内核, module_init() 即设备级 initcall,
 * 与 Linux 未定义 MODULE 时相同.
 */
#define module_init(x)	__initcall(x)

#endif /* _LEONIX_MODULE_H */
