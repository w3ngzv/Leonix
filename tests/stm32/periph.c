// SPDX-License-Identifier: GPL-2.0-only
/*
 * Peripheral checks that need no wiring: clocks measured against each
 * other, ADC on the internal reference and temperature sensor, CRC, DMA,
 * timer chaining, GPIO on unconnected pins, USART in half-duplex, backup
 * registers.  periph_check.py holds the expected values and the
 * DS5319/RM0008 sections they come from.
 *
 * PB12 to PB15 must be left unconnected: the GPIO checks read their pull
 * resistors.  QEMU models none of these peripherals.
 *
 * 不需要接线的外设检查: 时钟之间互相测量, ADC 读取内部参考电压与温度传感器,
 * CRC, DMA, 定时器级联, 未连接引脚上的 GPIO, 半双工 USART, 备份寄存器. 期望值
 * 及其出处 (DS5319 与 RM0008 的章节) 见 periph_check.py.
 *
 * PB12 至 PB15 不得连接任何东西: GPIO 检查读取这些引脚的上拉与下拉电阻.
 * QEMU 不模拟这里的任何外设.
 */
#include "runtime.h"

#define RCC		0x40021000u
#define RCC_CR		REG(RCC + 0x00)
#define RCC_CFGR	REG(RCC + 0x04)
#define RCC_AHBENR	REG(RCC + 0x14)
#define RCC_APB2ENR	REG(RCC + 0x18)
#define RCC_APB1ENR	REG(RCC + 0x1c)
#define RCC_BDCR	REG(RCC + 0x20)
#define RCC_CSR		REG(RCC + 0x24)

#define CR_HSEON	(1u << 16)
#define CR_HSERDY	(1u << 17)
#define CFGR_SW_MASK	3u
#define CFGR_SWS_MASK	(3u << 2)
#define SW_HSI		0u
#define SW_HSE		1u
#define BDCR_LSEON	(1u << 0)
#define BDCR_LSERDY	(1u << 1)
#define BDCR_RTCSEL_LSE	(1u << 8)
#define BDCR_RTCSEL_LSI	(2u << 8)
#define BDCR_RTCSEL_HSE	(3u << 8)	/* HSE / 128 */
#define BDCR_RTCEN	(1u << 15)
#define BDCR_BDRST	(1u << 16)
#define CSR_LSION	(1u << 0)
#define CSR_LSIRDY	(1u << 1)

#define AHB_DMA1	(1u << 0)
#define AHB_CRC		(1u << 6)
#define APB2_AFIO	(1u << 0)
#define APB2_IOPA	(1u << 2)
#define APB2_IOPB	(1u << 3)
#define APB2_ADC1	(1u << 9)
#define APB2_USART1	(1u << 14)
#define APB1_TIM2	(1u << 0)
#define APB1_TIM3	(1u << 1)
#define APB1_USART2	(1u << 17)
#define APB1_USART3	(1u << 18)
#define APB1_BKP	(1u << 27)
#define APB1_PWR	(1u << 28)

#define PWR_CR		REG(0x40007000u)
#define PWR_DBP		(1u << 8)

#define RTC		0x40002800u
#define RTC_CRL		REG(RTC + 0x04)
#define RTC_PRLH	REG(RTC + 0x08)
#define RTC_PRLL	REG(RTC + 0x0c)
#define RTC_CNTH	REG(RTC + 0x18)
#define RTC_CNTL	REG(RTC + 0x1c)
#define CRL_RSF		(1u << 3)
#define CRL_CNF		(1u << 4)
#define CRL_RTOFF	(1u << 5)

#define BKP_DR(n)	REG(0x40006c00u + 4u * (n))	/* DR1 to DR10 */
#define NR_BKP		10

#define GPIOA		0x40010800u
#define GPIOB		0x40010c00u
#define GPIO_CRL(p)	REG((p) + 0x00)
#define GPIO_CRH(p)	REG((p) + 0x04)
#define GPIO_IDR(p)	REG((p) + 0x08)
#define GPIO_ODR(p)	REG((p) + 0x0c)
#define GPIO_BSRR(p)	REG((p) + 0x10)
#define GPIO_BRR(p)	REG((p) + 0x14)
#define GPIO_LCKR(p)	REG((p) + 0x18)
#define LCKK		(1u << 16)

/*
 * CNF and MODE nibbles (RM0008 9.2.2): push-pull output at 2 MHz,
 * alternate function push-pull at 2 MHz, and input with the pull-up or
 * pull-down that ODR selects.
 *
 * CNF 与 MODE 的四位组合 (RM0008 9.2.2): 2 MHz 推挽输出, 2 MHz 复用推挽,
 * 以及由 ODR 选择上拉或下拉的输入.
 */
#define PIN_OUT_PP	0x2u
#define PIN_AF_PP	0xau
#define PIN_IN_PULL	0x8u
#define PB_HIGH_PINS	0xf000u		/* PB12 to PB15 */

#define CRC_DR		REG(0x40023000u)
#define CRC_CR		REG(0x40023008u)
#define NR_CRC_WORDS	64

#define DMA1		0x40020000u
#define DMA_ISR		REG(DMA1 + 0x00)
#define DMA_IFCR	REG(DMA1 + 0x04)
#define DMA_CCR1	REG(DMA1 + 0x08)
#define DMA_CNDTR1	REG(DMA1 + 0x0c)
#define DMA_CPAR1	REG(DMA1 + 0x10)
#define DMA_CMAR1	REG(DMA1 + 0x14)
#define CCR_EN		(1u << 0)
#define CCR_PINC	(1u << 6)
#define CCR_MINC	(1u << 7)
#define CCR_32BIT	((2u << 8) | (2u << 10))	/* PSIZE, MSIZE */
#define CCR_MEM2MEM	(1u << 14)
#define DMA_TCIF1	(1u << 1)
#define DMA_TEIF1	(1u << 3)
#define NR_DMA_WORDS	32
#define DMA_DST		((volatile uint32_t *)(SRAM_BASE + 0x1c00u))

#define TIM2		0x40000000u
#define TIM3		0x40000400u
#define TIM_CR1(t)	REG((t) + 0x00)
#define TIM_CR2(t)	REG((t) + 0x04)
#define TIM_SMCR(t)	REG((t) + 0x08)
#define TIM_EGR(t)	REG((t) + 0x14)
#define TIM_CNT(t)	REG((t) + 0x24)
#define TIM_PSC(t)	REG((t) + 0x28)
#define TIM_ARR(t)	REG((t) + 0x2c)
#define TIM_CEN		1u
#define TIM_UG		1u
#define MMS_UPDATE	(2u << 4)
#define TS_ITR2		(2u << 4)	/* TIM2: ITR2 is TIM3, RM0008 table 89 */
#define SMS_EXT1	7u
#define TIM3_PERIOD	100u
#define CHAIN_CYCLES	200000u

#define ADC1		0x40012400u
#define ADC_SR		REG(ADC1 + 0x00)
#define ADC_CR2		REG(ADC1 + 0x08)
#define ADC_SMPR1	REG(ADC1 + 0x0c)
#define ADC_SQR3	REG(ADC1 + 0x34)
#define ADC_DR		REG(ADC1 + 0x4c)
#define SR_EOC		(1u << 1)
#define CR2_ADON	(1u << 0)
#define CR2_CAL		(1u << 2)
#define CR2_RSTCAL	(1u << 3)
#define CR2_SWSTART_SEL	(7u << 17)
#define CR2_EXTTRIG	(1u << 20)
#define CR2_SWSTART	(1u << 22)
#define CR2_TSVREFE	(1u << 23)
#define SMPR1_239_16_17	((7u << 18) | (7u << 21))	/* 239.5 cycles */
#define ADC_CH_TEMP	16
#define ADC_CH_VREF	17
#define ADC_SAMPLES	64
/* DS5319 table 47: tSTAB 1 us; DS5319 表 47: tSTAB 为 1 us */
#define ADC_TSTAB	(HSI_HZ / 1000)

#define USART1		0x40013800u
#define USART2		0x40004400u
#define USART3		0x40004800u
#define USART_SR(u)	REG((u) + 0x00)
#define USART_DR(u)	REG((u) + 0x04)
#define USART_BRR(u)	REG((u) + 0x08)
#define USART_CR1(u)	REG((u) + 0x0c)
#define USART_CR3(u)	REG((u) + 0x14)
#define SR_RXNE		(1u << 5)
#define SR_TC		(1u << 6)
#define SR_ERRORS	0xfu		/* PE, FE, NE, ORE */
#define CR1_UE_TE_RE	((1u << 13) | (1u << 3) | (1u << 2))
#define CR3_HDSEL	(1u << 3)
#define BRR_115200	69u		/* 8 MHz / 115200 = 69.4 */

#define WAIT_LONG	2000000u
/*
 * Reads of LSERDY, some 20 s: DS5319 table 23 gives tSU(LSE) typ 2.5 s at
 * 25 C.  ONE_SECOND is in HCLK cycles at 8 MHz.
 *
 * LSERDY 的读取次数, 约 20 s: DS5319 表 23 给出 25 C 时 tSU(LSE) 典型值为
 * 2.5 s. ONE_SECOND 以 8 MHz 的 HCLK 周期计.
 */
#define WAIT_LSE	20000000u
#define ONE_SECOND	8000000u
#define TIMED_OUT	0xffffffffu

/* op codes, shared with periph_check.py; 操作码, 与 periph_check.py 共用 */
enum {
	P_HSE = 0x200, P_LSI, P_HSI, P_LSE, P_BKP,
	P_ADC_CAL, P_ADC_VREF, P_ADC_TEMP,
	P_CRC, P_DMA, P_TIMCHAIN,
	P_GPIO_OUT, P_GPIO_BSRR_BOTH, P_GPIO_BRR, P_GPIO_PULLUP,
	P_GPIO_PULLDOWN, P_GPIO_LOCK,
	P_USART,
};

static uint32_t sysclk(uint32_t sw)
{
	RCC_CFGR = (RCC_CFGR & ~CFGR_SW_MASK) | sw;
	return wait_set(RCC + 0x04, CFGR_SWS_MASK, sw << 2, WAIT_LONG);
}

static void backup_reset(uint32_t rtcsel)
{
	RCC_BDCR = BDCR_BDRST;
	RCC_BDCR = 0;
	RCC_BDCR = rtcsel | BDCR_RTCEN;
}

/* RTC prescaler 0 counts RTCCLK itself; 预分频为 0 时对 RTCCLK 本身计数 */
static uint32_t rtc_start(void)
{
	if (!wait_set(RTC + 0x04, CRL_RTOFF, CRL_RTOFF, WAIT_LONG))
		return 0;
	RTC_CRL |= CRL_CNF;
	RTC_PRLH = 0;
	RTC_PRLL = 0;
	RTC_CNTH = 0;
	RTC_CNTL = 0;
	RTC_CRL &= ~CRL_CNF;
	if (!wait_set(RTC + 0x04, CRL_RTOFF, CRL_RTOFF, WAIT_LONG))
		return 0;
	RTC_CRL &= ~CRL_RSF;
	return wait_set(RTC + 0x04, CRL_RSF, CRL_RSF, WAIT_LONG);
}

static uint32_t rtc_count(void)
{
	uint32_t hi, lo;

	do {
		hi = RTC_CNTH;
		lo = RTC_CNTL;
	} while (hi != RTC_CNTH);
	return (hi << 16) | lo;
}

/* RTCCLK periods in @cycles of HCLK; @cycles 个 HCLK 周期内的 RTCCLK 数 */
static uint32_t rtc_ticks(uint32_t cycles)
{
	uint32_t start;

	if (!rtc_start())
		return TIMED_OUT;
	start = rtc_count();
	delay_cycles(cycles);
	return rtc_count() - start;
}

static void check_clocks(void)
{
	uint32_t ok, n;

	RCC_APB1ENR |= APB1_PWR | APB1_BKP;
	PWR_CR |= PWR_DBP;

	RCC_CR |= CR_HSEON;
	ok = wait_set(RCC + 0x00, CR_HSERDY, CR_HSERDY, WAIT_LONG);
	rec(P_HSE, ok != 0, WAIT_LONG - ok, 0, RCC_CR, 0);
	if (!ok)
		return;

	/* LSI counted for one second of HSE; 以 HSE 计一秒, 数 LSI 周期 */
	RCC_CSR |= CSR_LSION;
	ok = wait_set(RCC + 0x24, CSR_LSIRDY, CSR_LSIRDY, WAIT_LONG);
	backup_reset(BDCR_RTCSEL_LSI);
	n = TIMED_OUT;
	if (ok && sysclk(SW_HSE))
		n = rtc_ticks(ONE_SECOND);
	rec(P_LSI, ok != 0, ONE_SECOND, 0, n, 0);

	/* HSE / 128 over 8000000 HSI cycles; 在该时长内数 HSE / 128 */
	backup_reset(BDCR_RTCSEL_HSE);
	n = TIMED_OUT;
	if (sysclk(SW_HSI))
		n = rtc_ticks(ONE_SECOND);
	rec(P_HSI, 1, ONE_SECOND, 0, n, 0);

	/*
	 * The 32.768 kHz crystal is on the board, not in the chip.
	 * 32.768 kHz 晶振在板上, 不在芯片内.
	 */
	backup_reset(0);
	RCC_BDCR |= BDCR_LSEON;
	ok = wait_set(RCC + 0x20, BDCR_LSERDY, BDCR_LSERDY, WAIT_LSE);
	n = TIMED_OUT;
	if (ok) {
		RCC_BDCR |= BDCR_RTCSEL_LSE | BDCR_RTCEN;
		if (sysclk(SW_HSE))
			n = rtc_ticks(ONE_SECOND);
		sysclk(SW_HSI);
	}
	rec(P_LSE, ok != 0, WAIT_LSE - ok, 0, n, 0);
	sysclk(SW_HSI);
}

static void check_backup(void)
{
	uint32_t i, v;

	for (i = 1; i <= NR_BKP; i++)
		BKP_DR(i) = (i * 0x1357u) & 0xffffu;
	for (i = 1; i <= NR_BKP; i++) {
		v = (i * 0x1357u) & 0xffffu;
		rec(P_BKP, i, v, 0, BKP_DR(i) & 0xffffu, 0);
	}
}

static uint32_t adc_sum(uint32_t channel)
{
	uint32_t i, sum = 0;

	ADC_SQR3 = channel;
	for (i = 0; i < ADC_SAMPLES; i++) {
		ADC_CR2 |= CR2_SWSTART;
		if (!wait_set(ADC1 + 0x00, SR_EOC, SR_EOC, WAIT_LONG))
			return TIMED_OUT;
		sum += ADC_DR & 0xfffu;
	}
	return sum;
}

static void check_adc(void)
{
	uint32_t ok;

	RCC_APB2ENR |= APB2_ADC1;
	ADC_CR2 = CR2_ADON;
	delay_cycles(ADC_TSTAB);
	ADC_CR2 |= CR2_RSTCAL;
	ok = wait_set(ADC1 + 0x08, CR2_RSTCAL, 0, WAIT_LONG);
	ADC_CR2 |= CR2_CAL;
	ok = ok && wait_set(ADC1 + 0x08, CR2_CAL, 0, WAIT_LONG);
	rec(P_ADC_CAL, ok != 0, 0, 0, ADC_CR2, 0);

	ADC_SMPR1 = SMPR1_239_16_17;
	ADC_CR2 |= CR2_TSVREFE | CR2_EXTTRIG | CR2_SWSTART_SEL;
	delay_cycles(ADC_TSTAB);
	rec(P_ADC_VREF, ADC_SAMPLES, 0, 0, adc_sum(ADC_CH_VREF), 0);
	rec(P_ADC_TEMP, ADC_SAMPLES, 0, 0, adc_sum(ADC_CH_TEMP), 0);
	ADC_CR2 = 0;
}

static uint32_t pattern(uint32_t i)
{
	return i * 0x9e3779b9u;
}

static void check_crc_dma(void)
{
	uint32_t i, bad = 0, ok, crc_wait;
	static const uint32_t src[NR_DMA_WORDS] = {
		0x00000000, 0xffffffff, 0x55555555, 0xaaaaaaaa,
		0x01234567, 0x89abcdef, 0xdeadbeef, 0x0badf00d,
		0x00000001, 0x80000000, 0x7fffffff, 0xfffffffe,
		0x12345678, 0x87654321, 0x0f0f0f0f, 0xf0f0f0f0,
		0x00ff00ff, 0xff00ff00, 0x13579bdf, 0x2468ace0,
		0x11111111, 0x22222222, 0x44444444, 0x88888888,
		0xcafebabe, 0xfeedface, 0x31415926, 0x27182818,
		0x00010002, 0x00030004, 0x00050006, 0x00070008,
	};

	RCC_AHBENR |= AHB_CRC | AHB_DMA1;
	CRC_CR = 1;			/* RESET */
	/*
	 * On the board tested on 2026-10-26 a read right after RESET still
	 * returns the old value, and a word written that early is lost.
	 * RM0008 4.4.3 and ES096 give no delay, so wait for 0xffffffff.
	 *
	 * 2026-10-26 测试的板子在 RESET 之后立即读取, 读到的仍是旧值, 此时写入
	 * 的字会丢失. RM0008 4.4.3 与 ES096 都没有给出延迟, 因此等待读出
	 * 0xffffffff.
	 */
	crc_wait = wait_set(0x40023000u, 0xffffffffu, 0xffffffffu, WAIT_LONG);
	for (i = 0; i < NR_CRC_WORDS; i++)
		CRC_DR = pattern(i);
	rec(P_CRC, NR_CRC_WORDS, crc_wait, 0, CRC_DR, 0);

	for (i = 0; i < NR_DMA_WORDS; i++)
		DMA_DST[i] = 0;
	DMA_IFCR = 0xfu;
	DMA_CPAR1 = (uint32_t)src;
	DMA_CMAR1 = (uint32_t)DMA_DST;
	DMA_CNDTR1 = NR_DMA_WORDS;
	DMA_CCR1 = CCR_MEM2MEM | CCR_32BIT | CCR_MINC | CCR_PINC;
	DMA_CCR1 |= CCR_EN;
	ok = wait_set(DMA1 + 0x00, DMA_TCIF1 | DMA_TEIF1, DMA_TCIF1,
		      WAIT_LONG);
	for (i = 0; i < NR_DMA_WORDS; i++)
		bad += DMA_DST[i] != src[i];
	rec(P_DMA, NR_DMA_WORDS, bad, ok != 0, DMA_ISR & 0xfu, DMA_CNDTR1);
	DMA_CCR1 = 0;
}

/*
 * TIM3 overflows clock TIM2 through ITR2; TIM2 * period + TIM3 must
 * match the cycles counted by SysTick.
 *
 * TIM3 的溢出经 ITR2 驱动 TIM2 计数; TIM2 * 周期 + TIM3 应与 SysTick 计得的
 * 周期数相符.
 */
static void check_timers(void)
{
	RCC_APB1ENR |= APB1_TIM2 | APB1_TIM3;
	TIM_PSC(TIM3) = 0;
	TIM_ARR(TIM3) = TIM3_PERIOD - 1;
	TIM_CR2(TIM3) = MMS_UPDATE;
	TIM_PSC(TIM2) = 0;
	TIM_ARR(TIM2) = 0xffff;
	TIM_SMCR(TIM2) = TS_ITR2 | SMS_EXT1;
	TIM_EGR(TIM2) = TIM_UG;
	TIM_EGR(TIM3) = TIM_UG;
	TIM_CNT(TIM2) = 0;
	TIM_CNT(TIM3) = 0;
	TIM_CR1(TIM2) = TIM_CEN;
	TIM_CR1(TIM3) = TIM_CEN;
	delay_cycles(CHAIN_CYCLES);
	TIM_CR1(TIM3) = 0;
	rec(P_TIMCHAIN, TIM_CNT(TIM2), TIM_CNT(TIM3), CHAIN_CYCLES,
	    TIM3_PERIOD, 0);
	TIM_CR1(TIM2) = 0;
}

static void pb_high(uint32_t nibble)
{
	GPIO_CRH(GPIOB) = (GPIO_CRH(GPIOB) & 0x0000ffffu) |
			  nibble << 16 | nibble << 20 |
			  nibble << 24 | nibble << 28;
}

static void check_gpio(void)
{
	uint32_t before, keys;

	RCC_APB2ENR |= APB2_IOPB;
	pb_high(PIN_OUT_PP);
	GPIO_ODR(GPIOB) = 0xa000;
	delay_cycles(100);
	rec(P_GPIO_OUT, 0xa000, 0, 0, GPIO_IDR(GPIOB) & PB_HIGH_PINS, 0);
	GPIO_ODR(GPIOB) = 0x5000;
	delay_cycles(100);
	rec(P_GPIO_OUT, 0x5000, 0, 0, GPIO_IDR(GPIOB) & PB_HIGH_PINS, 0);

	/* BSx has priority over BRx; BSx 优先于 BRx, RM0008 9.2.5 */
	GPIO_ODR(GPIOB) = 0;
	GPIO_BSRR(GPIOB) = (1u << 12) | (1u << (12 + 16));
	rec(P_GPIO_BSRR_BOTH, 0, 0, 0, GPIO_ODR(GPIOB) & PB_HIGH_PINS, 0);
	GPIO_ODR(GPIOB) = PB_HIGH_PINS;
	GPIO_BRR(GPIOB) = 1u << 13;
	rec(P_GPIO_BRR, 0, 0, 0, GPIO_ODR(GPIOB) & PB_HIGH_PINS, 0);

	pb_high(PIN_IN_PULL);
	GPIO_ODR(GPIOB) = PB_HIGH_PINS;
	delay_cycles(ONE_SECOND / 1000);
	rec(P_GPIO_PULLUP, 0, 0, 0, GPIO_IDR(GPIOB) & PB_HIGH_PINS, 0);
	GPIO_ODR(GPIOB) = 0;
	delay_cycles(ONE_SECOND / 1000);
	rec(P_GPIO_PULLDOWN, 0, 0, 0, GPIO_IDR(GPIOB) & PB_HIGH_PINS, 0);

	/*
	 * Lock PB12 to PB15 with the LCKK sequence, then try to change
	 * them; the lock holds until the next reset (RM0008 9.2.7).
	 *
	 * 以 LCKK 序列锁定 PB12 至 PB15, 再尝试修改其配置; 锁定持续到下一次复位
	 * (RM0008 9.2.7).
	 */
	keys = PB_HIGH_PINS;
	GPIO_LCKR(GPIOB) = LCKK | keys;
	GPIO_LCKR(GPIOB) = keys;
	GPIO_LCKR(GPIOB) = LCKK | keys;
	(void)GPIO_LCKR(GPIOB);
	before = GPIO_CRH(GPIOB);
	pb_high(PIN_OUT_PP);
	rec(P_GPIO_LOCK, GPIO_LCKR(GPIOB), before, 0, GPIO_CRH(GPIOB), 0);
}

/*
 * Half-duplex: RM0008 27.3.10 connects TX and RX inside the USART and
 * asks for the pin as open-drain or floating input.  The pin here is
 * push-pull, so this is a check of the echo only.
 *
 * 半双工: RM0008 27.3.10 在 USART 内部连接 TX 与 RX, 要求该引脚为开漏输出或
 * 浮空输入. 此处引脚为推挽, 因此只检查回显.
 */
static void usart_echo(uint32_t idx, uint32_t base)
{
	static const uint8_t bytes[] = { 0x00, 0x55, 0xaa, 0xff, 0x3c };
	uint32_t i, ok, got, sr;

	USART_BRR(base) = BRR_115200;
	USART_CR3(base) = CR3_HDSEL;
	USART_CR1(base) = CR1_UE_TE_RE;
	(void)USART_SR(base);
	(void)USART_DR(base);
	for (i = 0; i < sizeof(bytes); i++) {
		USART_DR(base) = bytes[i];
		ok = wait_set(base, SR_RXNE, SR_RXNE, WAIT_LONG);
		sr = USART_SR(base);
		got = ok ? USART_DR(base) & 0xffu : TIMED_OUT;
		rec(P_USART, idx, bytes[i], sr & SR_ERRORS, got, 0);
		wait_set(base, SR_TC, SR_TC, WAIT_LONG);
	}
	USART_CR1(base) = 0;
	USART_CR3(base) = 0;
}

static void check_usart(void)
{
	uint32_t crh;

	RCC_APB2ENR |= APB2_IOPA | APB2_USART1;
	RCC_APB1ENR |= APB1_USART2 | APB1_USART3;
	/* PA9 USART1_TX, PA2 USART2_TX, PB10 USART3_TX */
	crh = GPIO_CRH(GPIOA) & ~(0xfu << 4);
	GPIO_CRH(GPIOA) = crh | PIN_AF_PP << 4;
	GPIO_CRL(GPIOA) = (GPIO_CRL(GPIOA) & ~(0xfu << 8)) | PIN_AF_PP << 8;
	GPIO_CRH(GPIOB) = (GPIO_CRH(GPIOB) & ~(0xfu << 8)) | PIN_AF_PP << 8;
	usart_echo(1, USART1);
	usart_echo(2, USART2);
	usart_echo(3, USART3);
}

void suite_main(void)
{
	set_phase(1);
	check_clocks();
	set_phase(2);
	check_backup();
	set_phase(3);
	check_adc();
	set_phase(4);
	check_crc_dma();
	set_phase(5);
	check_timers();
	set_phase(6);
	check_gpio();
	set_phase(7);
	check_usart();
}
