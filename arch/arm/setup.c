// SPDX-License-Identifier: GPL-2.0-only
/*
 * Clock and exception setup, run by the start code before main().
 *
 * The core comes out of reset on the 8 MHz HSI (RM0008 7.2).  The board
 * has an 8 MHz crystal, which the PLL multiplies by 9 to the 72 MHz the
 * STM32F103 allows at most, with two Flash wait states and APB1 halved
 * to stay within its 36 MHz (RM0008 3.3.3 and 7.2).  If the crystal or
 * the PLL does not start, the CPU stays on the HSI; under QEMU, which
 * reads RCC as 0, it always does.  cpu_hz records the result for the
 * tick and the USART.
 *
 * 由启动代码在 main() 之前运行的时钟与异常设置.
 *
 * 内核复位后以 8 MHz 的 HSI 运行 (RM0008 7.2). 板上有一颗 8 MHz 晶振, 经 PLL
 * 九倍频得到 STM32F103 允许的最高频率 72 MHz, 同时设置两个 Flash 等待周期, 并把
 * APB1 二分频以不超过其 36 MHz 的上限 (RM0008 3.3.3 与 7.2). 晶振或 PLL 未能
 * 启动时, CPU 留在 HSI 上; QEMU 中 RCC 读出 0, 因此总是如此. cpu_hz 记录结果,
 * 供节拍与 USART 使用.
 */
#include <stdint.h>

#include <asm/stm32f1.h>
#include <asm/setup.h>

#define HSI_HZ		8000000u
#define HSE_HZ		8000000u	/* the board's crystal; 板上的晶振 */
#define PLL_MUL		9u
#define PLL_HZ		(HSE_HZ * PLL_MUL)
#define FLASH_LATENCY_72MHZ	2u

/*
 * Polls of a ready flag before giving up.  The crystal starts in 2 ms
 * typically (DS5319 table 22); at 8 MHz this count waits well over the
 * 100 ms that ST's HAL allows (HSE_STARTUP_TIMEOUT).
 *
 * 放弃之前轮询就绪标志的次数. 晶振的典型启动时间为 2 ms (DS5319 表 22); 在
 * 8 MHz 下该次数的等待远超过 ST 的 HAL 所允许的 100 ms (HSE_STARTUP_TIMEOUT).
 */
#define READY_POLLS	500000u

uint32_t cpu_hz = HSI_HZ;

static int wait_set(volatile uint32_t *reg, uint32_t mask, uint32_t value)
{
	uint32_t n;

	for (n = 0; n < READY_POLLS; n++)
		if ((*reg & mask) == value)
			return 0;
	return -1;
}

static void clock_init(void)
{
	RCC_CR |= RCC_CR_HSEON;
	if (wait_set(&RCC_CR, RCC_CR_HSERDY, RCC_CR_HSERDY) < 0)
		goto hsi;

	/* wait states before the clock rises; 先设等待周期, 再提高时钟 */
	FLASH_ACR = (FLASH_ACR & ~FLASH_ACR_LATENCY_MASK) | FLASH_LATENCY_72MHZ;
	RCC_CFGR = (RCC_CFGR & ~RCC_CFGR_PLLMUL_MASK) | RCC_CFGR_PLLSRC_HSE |
		   RCC_CFGR_PLLMUL(PLL_MUL) | RCC_CFGR_PPRE1_DIV2;
	RCC_CR |= RCC_CR_PLLON;
	if (wait_set(&RCC_CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY) < 0)
		goto pll_off;

	RCC_CFGR = (RCC_CFGR & ~RCC_CFGR_SW_MASK) | RCC_CFGR_SW_PLL;
	if (wait_set(&RCC_CFGR, RCC_CFGR_SWS_MASK, RCC_CFGR_SWS_PLL) < 0) {
		RCC_CFGR &= ~RCC_CFGR_SW_MASK;
		goto pll_off;
	}
	cpu_hz = PLL_HZ;
	return;

pll_off:
	RCC_CR &= ~RCC_CR_PLLON;
	FLASH_ACR &= ~FLASH_ACR_LATENCY_MASK;
hsi:
	RCC_CR &= ~RCC_CR_HSEON;
}

/**
 * setup_arch() - set the clock and the exception priorities
 *
 * STKALIGN keeps each exception frame 8-byte aligned, as AAPCS expects
 * of the handlers' C code; on the STM32F1 it resets to 0 (PM0056
 * 4.4.7).  MemManage, BusFault and UsageFault are enabled so that each
 * reports itself instead of escalating to HardFault (PM0056 4.4.9).
 * PendSV and SysTick get the lowest priority, the one usart.c gives its
 * interrupt, see switch.S.
 *
 * 设置时钟与异常优先级. STKALIGN 使每个异常栈帧 8 字节对齐, 这是 AAPCS 对处理
 * 程序中 C 代码的要求; STM32F1 上其复位值为 0 (PM0056 4.4.7). 启用
 * MemManage、BusFault 与 UsageFault, 使其各自报告, 不升级为 HardFault (PM0056
 * 4.4.9). PendSV 与 SysTick 取最低优先级, 与 usart.c 给其中断的优先级相同, 见
 * switch.S.
 *
 * Context: the start code, before main(), interrupts disabled.
 * 由启动代码在 main() 之前、关中断状态下调用.
 */
void setup_arch(void)
{
	SCB_CCR |= SCB_CCR_STKALIGN;
	SCB_SHCSR |= SCB_SHCSR_MEMFAULTENA | SCB_SHCSR_BUSFAULTENA |
		     SCB_SHCSR_USGFAULTENA;
	SCB_SHPR3 = IRQ_PRIO_KERNEL << 24 | IRQ_PRIO_KERNEL << 16;
	clock_init();
}
