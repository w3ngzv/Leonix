/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared by the STM32F103 test programs in tests/stm32.  Each program
 * defines suite_main(); runtime.c supplies the vector table, the fault
 * handler and the result records that stm32test.py reads back through
 * the debugger.
 *
 * The SRAM layout fits the 8 KiB of the STM32F100 that qemu-system-arm
 * emulates as stm32vldiscovery, so that the same image runs there and on
 * the 20 KiB STM32F103.
 *
 * tests/stm32 中各 STM32F103 测试程序共用的头文件. 每个程序定义 suite_main();
 * runtime.c 提供向量表、故障处理程序, 以及 stm32test.py 经调试器读回的结果
 * 记录.
 *
 * SRAM 布局按 qemu-system-arm 以 stm32vldiscovery 模拟的 STM32F100 的 8 KiB
 * 安排, 同一映像因此既能在该模拟器中运行, 也能在 20 KiB 的 STM32F103 上运行.
 */
#ifndef TESTS_STM32_RUNTIME_H
#define TESTS_STM32_RUNTIME_H

#include <stdint.h>

#define REG(a)		(*(volatile uint32_t *)(a))

/*
 * 0x20000000 .bss, 0x20000040 bit-band word, 0x20000080 scratch bytes,
 * 0x20000100 header, 0x20000110 records, stack below 0x20002000.
 *
 * 0x20000000 为 .bss, 0x20000040 为位带测试字, 0x20000080 为暂存字节,
 * 0x20000100 为头部, 0x20000110 起为记录, 栈在 0x20002000 以下.
 */
#define SRAM_BASE	0x20000000u
#define BB_OFFSET	0x40u
#define SCRATCH		((volatile uint8_t *)(SRAM_BASE + 0x80u))
#define HDR		((volatile uint32_t *)(SRAM_BASE + 0x100u))
#define REC_BASE	((volatile uint32_t *)(SRAM_BASE + 0x110u))
#define REC_WORDS	6
#define REC_MAX		280
#define STACK_TOP	(SRAM_BASE + 0x2000u)

/* header words and states; 头部各字与状态, 与 stm32test.py 共用 */
enum { H_MAGIC, H_STATE, H_COUNT, H_FAULTS };
#define MAGIC		0x15a7e500u
#define PAGE_FULL	0xf011f011u
#define PAGE_ACK	0xacacacacu
#define DONE		0x600dc0deu

/* HCLK after reset is HSI; 复位后 HCLK 为 HSI */
#define HSI_HZ		8000000u

extern uint32_t last_cfsr, last_hfsr;
/* from stm32.ld; 由 stm32.ld 定义 */
extern uint32_t __bss_start[], __bss_end[];

void suite_main(void);
void reset_handler(void);
void fault_entry(void);
void fault_c(uint32_t *frame);
void set_phase(uint32_t phase);
void rec(uint32_t op, uint32_t a, uint32_t b, uint32_t c,
	 uint32_t lo, uint32_t hi);
uint32_t fault_count(void);
uint32_t wait_set(uint32_t addr, uint32_t mask, uint32_t want,
		  uint32_t limit);
void delay_cycles(uint32_t cycles);

#endif
