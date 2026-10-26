/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_MODULE_H
#define _LEONIX_MODULE_H

#include <leonix/init.h>

/*
 * Leonix has no module loader, so every driver is built in and
 * module_init() is the device initcall, as in Linux with MODULE unset.
 */
#define module_init(x)	__initcall(x)

#endif /* _LEONIX_MODULE_H */
