/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ARM_STM32F1_H
#define _ASM_ARM_STM32F1_H

/*
 * The registers Leonix uses on the STM32F103, with base addresses from
 * RM0008 Rev 15 table 3 and offsets from the register map at the end of
 * each chapter; the Cortex-M3 core peripherals are from PM0056 Rev 5
 * chapter 4.  Plain defines, so that the assembly files can use them.
 *
 * Leonix 在 STM32F103 上使用的寄存器. 基址取自 RM0008 Rev 15 表 3, 偏移取自
 * 各章末尾的寄存器表; Cortex-M3 内核外设取自 PM0056 Rev 5 第 4 章. 以普通的
 * #define 写出, 汇编文件也能使用.
 */
#ifdef __ASSEMBLER__
#define MMIO32(addr)		(addr)
#else
#include <stdint.h>
#define MMIO32(addr)		(*(volatile uint32_t *)(addr))
#endif

/* RCC, RM0008 7.3 */
#define RCC_BASE		0x40021000
#define RCC_CR			MMIO32(RCC_BASE + 0x00)
#define RCC_CFGR		MMIO32(RCC_BASE + 0x04)
#define RCC_APB2ENR		MMIO32(RCC_BASE + 0x18)

#define RCC_CR_HSEON		(1u << 16)
#define RCC_CR_HSERDY		(1u << 17)
#define RCC_CR_PLLON		(1u << 24)
#define RCC_CR_PLLRDY		(1u << 25)

#define RCC_CFGR_SW_MASK	(3u << 0)
#define RCC_CFGR_SW_PLL		(2u << 0)
#define RCC_CFGR_SWS_MASK	(3u << 2)
#define RCC_CFGR_SWS_PLL	(2u << 2)
#define RCC_CFGR_PPRE1_DIV2	(4u << 8)
#define RCC_CFGR_PLLSRC_HSE	(1u << 16)
#define RCC_CFGR_PLLMUL_MASK	(15u << 18)
#define RCC_CFGR_PLLMUL(n)	((uint32_t)((n) - 2) << 18)

#define RCC_APB2ENR_IOPAEN	(1u << 2)
#define RCC_APB2ENR_IOPCEN	(1u << 4)
#define RCC_APB2ENR_USART1EN	(1u << 14)

/* Flash interface, RM0008 3.3.3 and PM0075 Rev 2 chapter 3 */
#define FLASH_BASE		0x40022000
#define FLASH_ACR		MMIO32(FLASH_BASE + 0x00)
#define FLASH_KEYR		MMIO32(FLASH_BASE + 0x04)
#define FLASH_SR		MMIO32(FLASH_BASE + 0x0c)
#define FLASH_CR		MMIO32(FLASH_BASE + 0x10)
#define FLASH_AR		MMIO32(FLASH_BASE + 0x14)

#define FLASH_ACR_LATENCY_MASK	(7u << 0)
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

/* GPIO, RM0008 9.2 */
#define GPIOA_BASE		0x40010800
#define GPIOC_BASE		0x40011000
#define GPIO_CRL(base)		MMIO32((base) + 0x00)
#define GPIO_CRH(base)		MMIO32((base) + 0x04)
#define GPIO_ODR(base)		MMIO32((base) + 0x0c)
#define GPIO_BSRR(base)		MMIO32((base) + 0x10)
#define GPIO_BRR(base)		MMIO32((base) + 0x14)

/*
 * 4-bit CNF:MODE fields of CRL and CRH, RM0008 9.2.1 and 9.2.2
 * CRL 与 CRH 中 4 位的 CNF:MODE 字段, RM0008 9.2.1 与 9.2.2
 */
#define GPIO_MODE_MASK		0xfu
#define GPIO_INPUT_PULL		0x8u	/* CNF 10, MODE 00 */
#define GPIO_OUTPUT_PP_2MHZ	0x2u	/* CNF 00, MODE 10 */
#define GPIO_AF_PP_2MHZ		0xau	/* CNF 10, MODE 10 */

/* USART1, RM0008 27.6 */
#define USART1_BASE		0x40013800
#define USART1_SR		MMIO32(USART1_BASE + 0x00)
#define USART1_DR		MMIO32(USART1_BASE + 0x04)
#define USART1_BRR		MMIO32(USART1_BASE + 0x08)
#define USART1_CR1		MMIO32(USART1_BASE + 0x0c)

#define USART_SR_ORE		(1u << 3)
#define USART_SR_RXNE		(1u << 5)
#define USART_SR_TXE		(1u << 7)
#define USART_CR1_RE		(1u << 2)
#define USART_CR1_TE		(1u << 3)
#define USART_CR1_RXNEIE	(1u << 5)
#define USART_CR1_TXEIE		(1u << 7)
#define USART_CR1_UE		(1u << 13)

/* position in the vector table, RM0008 table 63; 在向量表中的位置 */
#define USART1_IRQ		37

/* SysTick, PM0056 4.5 */
#define STK_CTRL		MMIO32(0xe000e010)
#define STK_LOAD		MMIO32(0xe000e014)
#define STK_VAL			MMIO32(0xe000e018)

#define STK_CTRL_ENABLE		(1u << 0)
#define STK_CTRL_TICKINT	(1u << 1)
#define STK_CTRL_CLKSOURCE	(1u << 2)

/* NVIC, PM0056 4.3 */
#define NVIC_ISER(n)		MMIO32(0xe000e100 + 4 * (n))
#define NVIC_IPR_BASE		0xe000e400

/* SCB, PM0056 4.4 */
#define SCB_ICSR		MMIO32(0xe000ed04)
#define SCB_CCR			MMIO32(0xe000ed14)
#define SCB_SHPR3		MMIO32(0xe000ed20)
#define SCB_SHCSR		MMIO32(0xe000ed24)
#define SCB_CFSR		MMIO32(0xe000ed28)
#define SCB_HFSR		MMIO32(0xe000ed2c)
#define SCB_MMFAR		MMIO32(0xe000ed34)
#define SCB_BFAR		MMIO32(0xe000ed38)

#define SCB_ICSR_PENDSVSET	(1u << 28)
#define SCB_CCR_STKALIGN	(1u << 9)
#define SCB_SHCSR_MEMFAULTENA	(1u << 16)
#define SCB_SHCSR_BUSFAULTENA	(1u << 17)
#define SCB_SHCSR_USGFAULTENA	(1u << 18)
#define SCB_CFSR_MMARVALID	(1u << 7)
#define SCB_CFSR_BFARVALID	(1u << 15)

/*
 * The STM32F10xxx implements the top 4 bits of each priority byte
 * (PM0056 4.3.7); 0xf0 is the lowest priority.
 *
 * STM32F10xxx 只实现每个优先级字节的高 4 位 (PM0056 4.3.7); 0xf0 是最低优先级.
 */
#define IRQ_PRIO_KERNEL		0xf0u

#endif /* _ASM_ARM_STM32F1_H */
