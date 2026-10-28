/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _FAKE_SECTIONS_H
#define _FAKE_SECTIONS_H

/*
 * the lxfs area of arch/arm/leonix.lds, an array in the test
 * arch/arm/leonix.lds 中的 lxfs 区域, 在测试中是一个数组
 */
#include <stdint.h>

#define FAKE_FLASH_BYTES	8192

extern uint8_t fake_flash[FAKE_FLASH_BYTES];

#define __lxfs_flash_start	((const uint8_t *)fake_flash)
#define __lxfs_flash_end	((const uint8_t *)fake_flash + FAKE_FLASH_BYTES)

#endif /* _FAKE_SECTIONS_H */
