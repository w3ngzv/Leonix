// SPDX-License-Identifier: GPL-2.0-only
/*
 * Square waves for a frequency counter outside the chip.  signal_check.py
 * writes SIG_MODE before releasing the core, reads back the record that
 * gives the nominal input clock and divider, and times the wave with the
 * Leonardo's /dev/dap "f" command on D13, whose 16 MHz crystal is the
 * reference.  Every output pin is 5 V tolerant (DS5319 table 5), since
 * D13 is driven again after the measurement.
 *
 * 为芯片外部的频率计输出方波. signal_check.py 在释放内核之前写入 SIG_MODE,
 * 读回给出标称输入时钟与分频数的记录, 再以 Leonardo 的 /dev/dap "f" 命令在
 * D13 上计时, 以 Leonardo 的 16 MHz 晶振为基准. 各输出引脚均为 5 V 容忍
 * (DS5319 表 5), 因为测量结束后 D13 重新变为输出.
 *
 *   1  PA8  TIM1_CH1, SYSCLK HSI 8 MHz        HSI against the reference
 *   2  PA8  TIM1_CH1, SYSCLK HSE 8 MHz        HSE crystal
 *   3  PB6  TIM4_CH1, SYSCLK PLL 72 MHz,      PLL, and the x2 on the timer
 *           APB1 / 2                          clock when APB1 is divided
 *   4  PA9  USART1_TX sending 0x55, 1200 Bd   baud rate generator
 *
 *   1  PA8  TIM1_CH1, SYSCLK 为 HSI 8 MHz     HSI 相对基准的偏差
 *   2  PA8  TIM1_CH1, SYSCLK 为 HSE 8 MHz     HSE 晶振
 *   3  PB6  TIM4_CH1, SYSCLK 为 PLL 72 MHz,   PLL, 以及 APB1 分频时定时器
 *           APB1 二分频                       时钟的 x2
 *   4  PA9  USART1_TX 连续发送 0x55, 1200 Bd  波特率发生器
 */
#include "runtime.h"

/* above the records, below the stack; 位于记录之上、栈之下 */
#define SIG_MODE	REG(SRAM_BASE + 0x1c00u)

#define RCC_CR		REG(0x40021000u)
#define RCC_CFGR	REG(0x40021004u)
#define RCC_APB2ENR	REG(0x40021018u)
#define RCC_APB1ENR	REG(0x4002101cu)
#define FLASH_ACR	REG(0x40022000u)

#define CR_HSEON	(1u << 16)
#define CR_HSERDY	(1u << 17)
#define CR_PLLON	(1u << 24)
#define CR_PLLRDY	(1u << 25)
#define CFGR_SW_MASK	3u
#define CFGR_SWS_MASK	(3u << 2)
#define SW_HSE		1u
#define SW_PLL		2u
#define CFGR_PPRE1_DIV2	(4u << 8)
#define CFGR_PLLSRC_HSE	(1u << 16)
#define CFGR_PLLMUL9	(7u << 18)
#define ACR_LATENCY2	2u
#define ACR_PRFTBE	(1u << 4)

#define APB2_IOPA	(1u << 2)
#define APB2_IOPB	(1u << 3)
#define APB2_TIM1	(1u << 11)
#define APB2_USART1	(1u << 14)
#define APB1_TIM4	(1u << 2)

#define GPIOA_CRH	REG(0x40010804u)
#define GPIOB_CRL	REG(0x40010c00u)
#define PIN_AF_PP	0xau		/* AF push-pull 2 MHz; 复用推挽 */

#define TIM1		0x40012c00u
#define TIM4		0x40000800u
#define TIM_CR1(t)	REG((t) + 0x00)
#define TIM_EGR(t)	REG((t) + 0x14)
#define TIM_CCMR1(t)	REG((t) + 0x18)
#define TIM_CCER(t)	REG((t) + 0x20)
#define TIM_PSC(t)	REG((t) + 0x28)
#define TIM_ARR(t)	REG((t) + 0x2c)
#define TIM_CCR1(t)	REG((t) + 0x34)
#define TIM_BDTR(t)	REG((t) + 0x44)
#define OC1M_PWM1	(6u << 4)
#define OC1PE		(1u << 3)
#define CC1E		1u
#define BDTR_MOE	(1u << 15)	/* TIM1 only; 仅 TIM1 */
#define TIM_CEN		1u
#define TIM_UG		1u

#define USART1_SR	REG(0x40013800u)
#define USART1_DR	REG(0x40013804u)
#define USART1_BRR	REG(0x40013808u)
#define USART1_CR1	REG(0x4001380cu)
#define SR_TXE		(1u << 7)
#define CR1_UE_TE	((1u << 13) | (1u << 3))

#define WAIT_LONG	2000000u
/* output, and count rate after PSC; 输出频率与经 PSC 后的计数频率 */
#define SIG_HZ		500u
#define TICK_HZ		1000000u
#define BAUD		1200u
#define PLL_HZ		72000000u

/* op code, shared with signal_check.py; 操作码, 与 signal_check.py 共用 */
#define S_READY		0x300u

static uint32_t switch_to(uint32_t sw)
{
	RCC_CFGR = (RCC_CFGR & ~CFGR_SW_MASK) | sw;
	return wait_set(0x40021004u, CFGR_SWS_MASK, sw << 2, WAIT_LONG) != 0;
}

static uint32_t hse_on(void)
{
	RCC_CR |= CR_HSEON;
	return wait_set(0x40021000u, CR_HSERDY, CR_HSERDY, WAIT_LONG) != 0;
}

static uint32_t pll_on(void)
{
	if (!hse_on())
		return 0;
	FLASH_ACR = ACR_PRFTBE | ACR_LATENCY2;
	RCC_CFGR = CFGR_PLLSRC_HSE | CFGR_PLLMUL9 | CFGR_PPRE1_DIV2;
	RCC_CR |= CR_PLLON;
	if (!wait_set(0x40021000u, CR_PLLRDY, CR_PLLRDY, WAIT_LONG))
		return 0;
	return switch_to(SW_PLL);
}

/*
 * PWM at SIG_HZ, 50 % duty, from a timer clock of @clk_hz.
 * 由 @clk_hz 的定时器时钟产生 SIG_HZ、占空比 50 % 的 PWM.
 */
static uint32_t pwm(uint32_t tim, uint32_t clk_hz)
{
	uint32_t period = TICK_HZ / SIG_HZ;

	TIM_PSC(tim) = clk_hz / TICK_HZ - 1;
	TIM_ARR(tim) = period - 1;
	TIM_CCR1(tim) = period / 2;
	TIM_CCMR1(tim) = OC1M_PWM1 | OC1PE;
	TIM_CCER(tim) = CC1E;
	if (tim == TIM1)
		TIM_BDTR(tim) = BDTR_MOE;
	TIM_EGR(tim) = TIM_UG;
	TIM_CR1(tim) = TIM_CEN;
	return (TIM_PSC(tim) + 1) * (TIM_ARR(tim) + 1);
}

void suite_main(void)
{
	uint32_t mode = SIG_MODE, ok = 1, clk = HSI_HZ, div = 0;

	set_phase(mode);
	switch (mode) {
	case 1:
	case 2:
		if (mode == 2) {
			ok = hse_on() && switch_to(SW_HSE);
			clk = 8000000u;
		}
		RCC_APB2ENR |= APB2_IOPA | APB2_TIM1;
		GPIOA_CRH = (GPIOA_CRH & ~0xfu) | PIN_AF_PP;
		div = pwm(TIM1, clk);
		break;
	case 3:
		ok = pll_on();
		/* APB1 36 MHz, timer clock x2; APB1 为 36 MHz, 定时器时钟 x2 */
		clk = PLL_HZ;
		RCC_APB2ENR |= APB2_IOPB;
		RCC_APB1ENR |= APB1_TIM4;
		GPIOB_CRL = (GPIOB_CRL & ~(0xfu << 24)) | PIN_AF_PP << 24;
		div = pwm(TIM4, clk);
		break;
	case 4:
		RCC_APB2ENR |= APB2_IOPA | APB2_USART1;
		GPIOA_CRH = (GPIOA_CRH & ~(0xfu << 4)) | PIN_AF_PP << 4;
		USART1_BRR = (HSI_HZ + BAUD / 2) / BAUD;
		USART1_CR1 = CR1_UE_TE;
		/* 0x55 framed 8N1 is 0101010101: half the baud rate */
		/* 0x55 以 8N1 成帧后为 0101010101: 波特率的一半 */
		div = 2 * USART1_BRR;
		break;
	default:
		ok = 0;
	}
	rec(S_READY, mode, ok, clk, div, 0);
	HDR[H_STATE] = DONE;
	for (;;) {
		if (mode == 4 && (USART1_SR & SR_TXE))
			USART1_DR = 0x55;
	}
}
