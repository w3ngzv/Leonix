// SPDX-License-Identifier: GPL-2.0-only
/*
 * The LED of the STM32F103C8T6 core board, on PC13, which lights on low.
 * It is the error LED of include/leonix/leds.h; panic() lights it.
 *
 * STM32F103C8T6 核心板的 LED, 接在 PC13 上, 低电平点亮. 该 LED 即
 * include/leonix/leds.h 中的错误指示 LED; panic() 点亮该 LED.
 */
#include <asm/stm32f1.h>

#include <leonix/leds.h>

#define LED_PIN		13
#define LED_CRH_SHIFT	((LED_PIN - 8) * 4)

/**
 * leds_init() - make PC13 an output and turn the LED off
 *
 * 把 PC13 设为输出并熄灭 LED.
 *
 * Context: main() or panic(), with interrupts disabled.
 * 由 main() 或 panic() 在关中断状态下调用.
 */
void leds_init(void)
{
	RCC_APB2ENR |= RCC_APB2ENR_IOPCEN;
	GPIO_BSRR(GPIOC_BASE) = 1u << LED_PIN;
	GPIO_CRH(GPIOC_BASE) = (GPIO_CRH(GPIOC_BASE) &
				~(GPIO_MODE_MASK << LED_CRH_SHIFT)) |
			       GPIO_OUTPUT_PP_2MHZ << LED_CRH_SHIFT;
}

/**
 * led_error_off() - turn the LED off
 *
 * BSRR sets the pin in one write, so no interrupt can split a
 * read-modify-write of ODR (RM0008 9.2.5).
 *
 * 熄灭 LED. BSRR 以一次写入置位引脚, 中断不会打断对 ODR 的读-改-写
 * (RM0008 9.2.5).
 *
 * Context: a task, after leds_init().
 * 在任务中、leds_init() 之后调用.
 */
void led_error_off(void)
{
	GPIO_BSRR(GPIOC_BASE) = 1u << LED_PIN;
}

/**
 * led_error_toggle() - invert the LED
 *
 * 翻转 LED.
 *
 * Context: a task, after leds_init().
 * 在任务中、leds_init() 之后调用.
 */
void led_error_toggle(void)
{
	uint32_t bit = 1u << LED_PIN;

	if (GPIO_ODR(GPIOC_BASE) & bit)
		GPIO_BRR(GPIOC_BASE) = bit;
	else
		GPIO_BSRR(GPIOC_BASE) = bit;
}
