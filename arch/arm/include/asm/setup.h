/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ARM_SETUP_H
#define _ASM_ARM_SETUP_H

#include <stdint.h>

/*
 * SYSCLK, which is also HCLK and PCLK2, set by setup_arch()
 * SYSCLK, 同时也是 HCLK 与 PCLK2, 由 setup_arch() 设置
 */
extern uint32_t cpu_hz;

void setup_arch(void);

#endif /* _ASM_ARM_SETUP_H */
