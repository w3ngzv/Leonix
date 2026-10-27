// SPDX-License-Identifier: GPL-2.0-only
/*
 * The Leonardo's L, TX and RX LEDs, see include/leonix/leds.h.  RX is
 * the error LED.  L is driven through a buffer from PC7 and lights on
 * high; TX (PD5) and RX (PB0) light on low (Documentation/booting.rst,
 * section 4).  arch/avr/panic.c lights all three.
 *
 * Leonardo 的 L、TX 与 RX 三个 LED, 见 include/leonix/leds.h. RX 用作错误指示.
 * L 经缓冲器由 PC7 驱动, 高电平点亮; TX (PD5) 与 RX (PB0) 低电平点亮
 * (Documentation/booting.rst 第 4 节). arch/avr/panic.c 点亮全部三个.
 */
#include <avr/io.h>

#include <leonix/leds.h>

/**
 * leds_init() - turn L and TX off
 *
 * PC7 is made an output rather than left floating after reset.  RX is
 * left to led_error_off(), which the LCD task calls.
 *
 * 熄灭 L 与 TX. 复位后把 PC7 设为输出, 不让其悬空. RX 由 LCD 任务调用的
 * led_error_off() 设置.
 *
 * Context: main(), before sched_start().
 * 由 main() 在 sched_start() 之前调用.
 */
void leds_init(void)
{
	DDRC |= 1 << DDC7;
	PORTD |= 1 << PORTD5;
	DDRD |= 1 << DDD5;
}

/**
 * led_error_off() - turn the RX LED off
 *
 * 熄灭 RX LED.
 *
 * Context: a task.
 * 在任务中调用.
 */
void led_error_off(void)
{
	PORTB |= 1 << PORTB0;
	DDRB |= 1 << DDB0;
}

/**
 * led_error_toggle() - invert the RX LED
 *
 * Writing 1 to PINB0 toggles PORTB0 (datasheet 10.2.2).
 *
 * 翻转 RX LED. 向 PINB0 写 1 即翻转 PORTB0 (数据手册 10.2.2).
 *
 * Context: a task, after led_error_off().
 * 在任务中、led_error_off() 之后调用.
 */
void led_error_toggle(void)
{
	PINB = 1 << PINB0;
}
