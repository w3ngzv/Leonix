/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_ARRAY_SIZE_H
#define _LEONIX_ARRAY_SIZE_H

/*
 * Linux's include/linux/array_size.h, without __must_be_array(): there is
 * no compiler.h to supply it, so a pointer passed here still compiles.
 *
 * Linux 的 include/linux/array_size.h, 但没有 __must_be_array(): 没有提供该宏的
 * compiler.h, 因此传入指针仍能通过编译.
 */
#define ARRAY_SIZE(arr)	(sizeof(arr) / sizeof((arr)[0]))

#endif /* _LEONIX_ARRAY_SIZE_H */
