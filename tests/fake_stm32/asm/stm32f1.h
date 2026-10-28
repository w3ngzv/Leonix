/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _FAKE_STM32F1_H
#define _FAKE_STM32F1_H

/*
 * The Flash interface of arch/arm/include/asm/stm32f1.h, for
 * tests/test_stm32_flash.c.  Every read of FLASH_SR goes through
 * fake_flash_sr(), where the test carries out the program or erase the
 * driver has started, as the FPEC does while BSY is set.
 *
 * arch/arm/include/asm/stm32f1.h 中的 Flash 接口, 供
 * tests/test_stm32_flash.c 使用. 每次读取 FLASH_SR 都经由 fake_flash_sr(),
 * 测试在其中执行驱动已启动的编程或擦除, 与 BSY 置位期间 FPEC 的行为相同.
 */
#include <stdint.h>

extern uint32_t fake_flash_keyr, fake_flash_cr, fake_flash_ar;
uint32_t *fake_flash_sr(void);

#define FLASH_KEYR		fake_flash_keyr
#define FLASH_SR		(*fake_flash_sr())
#define FLASH_CR		fake_flash_cr
#define FLASH_AR		fake_flash_ar

#define FLASH_KEY1		0x45670123u
#define FLASH_KEY2		0xcdef89abu
#define FLASH_SR_BSY		(1u << 0)
#define FLASH_SR_PGERR		(1u << 2)
#define FLASH_SR_WRPRTERR	(1u << 4)
#define FLASH_SR_EOP		(1u << 5)
#define FLASH_CR_PG		(1u << 0)
#define FLASH_CR_PER		(1u << 1)
#define FLASH_CR_STRT		(1u << 6)
#define FLASH_CR_LOCK		(1u << 7)

#endif /* _FAKE_STM32F1_H */
