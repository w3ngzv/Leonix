// SPDX-License-Identifier: GPL-2.0-only
/*
 * What a USART TX pin set to alternate function push-pull does while the
 * USART is still disabled, and what a half-duplex echo receives when it
 * starts from that state.  RM0008 27.3 says only that the TX pin "returns
 * to its IO port configuration" while the transmitter is disabled.  On
 * the board this project was written on, none of PA9, PA2 and PB10 is
 * driven while its USART is disabled, and each keeps the level it had.
 * From a low level USART1 and USART3 lose the first byte, and USART2
 * receives 0x00 with a framing error, then 0xd5 for 0x55.
 * usartidle_check.py names what each record shows; the results from
 * other boards, an ST part above all, tell whether this is how the
 * silicon behaves.
 *
 * Each case first holds the pin with the internal pull-down or pull-up,
 * so that the level it keeps is known, then sets alternate function
 * push-pull and reads the pin, then echoes five bytes in the order that
 * periph.c used before it sent an idle frame first: HDSEL, then UE, TE
 * and RE at once.  The USART is put back to its reset state through
 * RCC between cases.
 *
 * USART 尚未使能时, 设为复用推挽的 TX 引脚处于什么状态, 以及半双工回显从该
 * 状态开始时收到什么. RM0008 27.3 只写到发送器未使能时 TX 引脚 "returns to
 * its IO port configuration". 在本项目所用的板子上, USART 未使能时 PA9、
 * PA2 与 PB10 都不被驱动, 各自保持原有电平. 从低电平开始时, USART1 与
 * USART3 丢失第一个字节, USART2 收到带帧错误的 0x00, 随后把 0x55 收为
 * 0xd5. 各条记录的含义由 usartidle_check.py 说明;
 * 其他板子, 尤其是 ST 原厂芯片的结果, 可以说明这是否是芯片本身的行为.
 *
 * 每种情形先以内部下拉或上拉保持引脚, 使其保留的电平已知, 再设为复用推挽并
 * 读取引脚, 然后按 periph.c 改为先发送空闲帧之前的顺序回显五个字节: 先置
 * HDSEL, 再同时置 UE、TE 与 RE. 两种情形之间经 RCC 把 USART 复位.
 */
#include "runtime.h"

#define RCC_APB2RSTR	REG(0x4002100cu)
#define RCC_APB1RSTR	REG(0x40021010u)
#define RCC_APB2ENR	REG(0x40021018u)
#define RCC_APB1ENR	REG(0x4002101cu)
#define APB2_IOPA	(1u << 2)
#define APB2_IOPB	(1u << 3)

#define GPIOA		0x40010800u
#define GPIOB		0x40010c00u
#define GPIO_CRL(p)	REG((p) + 0x00)
#define GPIO_CRH(p)	REG((p) + 0x04)
#define GPIO_IDR(p)	REG((p) + 0x08)
#define GPIO_ODR(p)	REG((p) + 0x0c)
#define PIN_IN_PULL	0x8u		/* CNF 10, MODE 00; ODR picks up or down */
#define PIN_AF_PP	0xau		/* CNF 10, MODE 10 */

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

#define PULL_CYCLES	(HSI_HZ / 1000)		/* 1 ms on the pull resistor */
#define SETTLE_CYCLES	100
#define WAIT_LONG	2000000u
#define TIMED_OUT	0xffffffffu
#define REPEATS		2
#define ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))

/* op codes, shared with usartidle_check.py; 与 usartidle_check.py 共用 */
enum {
	U_PIN = 0x500,
	U_ECHO,
};

struct port {
	uint32_t usart;		/* register base */
	volatile uint32_t *rstr, *enr;
	uint32_t bit;		/* in RCC_APBxRSTR and RCC_APBxENR */
	uint32_t gpio;
	uint32_t pin;
};

static const struct port ports[] = {
	{ 0x40013800u, &RCC_APB2RSTR, &RCC_APB2ENR, 1u << 14, GPIOA, 9 },
	{ 0x40004400u, &RCC_APB1RSTR, &RCC_APB1ENR, 1u << 17, GPIOA, 2 },
	{ 0x40004800u, &RCC_APB1RSTR, &RCC_APB1ENR, 1u << 18, GPIOB, 10 },
};

static void pin_mode(const struct port *p, uint32_t mode)
{
	volatile uint32_t *cr = p->pin < 8 ? &GPIO_CRL(p->gpio)
					   : &GPIO_CRH(p->gpio);
	uint32_t shift = (p->pin & 7u) * 4u;

	*cr = (*cr & ~(0xfu << shift)) | mode << shift;
}

static uint32_t pin_level(const struct port *p)
{
	return GPIO_IDR(p->gpio) >> p->pin & 1u;
}

/* USART back to its reset state, clock on; USART 复位并打开时钟 */
static void usart_reset(const struct port *p)
{
	*p->rstr |= p->bit;
	*p->rstr &= ~p->bit;
	*p->enr |= p->bit;
	(void)*p->enr;
	delay_cycles(SETTLE_CYCLES);
}

static void one_case(uint32_t idx, const struct port *p, uint32_t pull,
		     uint32_t rep)
{
	static const uint8_t bytes[] = { 0x00, 0x55, 0xaa, 0xff, 0x3c };
	uint32_t id = (idx + 1) << 8 | pull << 4 | rep;
	uint32_t pulled, af, sr, early, i, ok, got;
	uint32_t u = p->usart;

	usart_reset(p);
	GPIO_ODR(p->gpio) = (GPIO_ODR(p->gpio) & ~(1u << p->pin)) |
			    pull << p->pin;
	pin_mode(p, PIN_IN_PULL);
	delay_cycles(PULL_CYCLES);
	pulled = pin_level(p);
	pin_mode(p, PIN_AF_PP);
	delay_cycles(SETTLE_CYCLES);
	af = pin_level(p);

	USART_BRR(u) = BRR_115200;
	USART_CR3(u) = CR3_HDSEL;
	USART_CR1(u) = CR1_UE_TE_RE;
	/* one frame time: has anything been received before the first write */
	delay_cycles(10u * BRR_115200);
	sr = USART_SR(u);
	early = sr & SR_RXNE ? USART_DR(u) & 0xffu : TIMED_OUT;
	rec(U_PIN, id, pulled, af, sr, early);

	for (i = 0; i < sizeof(bytes); i++) {
		USART_DR(u) = bytes[i];
		ok = wait_set(u, SR_RXNE, SR_RXNE, WAIT_LONG);
		sr = USART_SR(u);
		got = ok ? USART_DR(u) & 0xffu : TIMED_OUT;
		rec(U_ECHO, id, bytes[i], sr & SR_ERRORS, got, 0);
		wait_set(u, SR_TC, SR_TC, WAIT_LONG);
	}
	USART_CR1(u) = 0;
	USART_CR3(u) = 0;
	pin_mode(p, PIN_IN_PULL);
}

void suite_main(void)
{
	uint32_t i, pull, rep;

	RCC_APB2ENR |= APB2_IOPA | APB2_IOPB;
	(void)RCC_APB2ENR;
	delay_cycles(SETTLE_CYCLES);

	set_phase(1);
	for (rep = 0; rep < REPEATS; rep++)
		for (i = 0; i < ARRAY_SIZE(ports); i++)
			for (pull = 0; pull <= 1; pull++)
				one_case(i, &ports[i], pull, rep);
}
