/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_LEDS_H
#define _LEONIX_LEDS_H

/*
 * The board's LEDs, in arch/<arch>/leds.c.  All stay off while the
 * system runs normally.  The error LED flashes while the LCD is offline,
 * which a dead display cannot report itself; panic() lights LEDs of its
 * own accord.
 *
 * 板上的 LED, 实现在 arch/<arch>/leds.c. 系统正常运行时全部熄灭. 液晶屏离线
 * 期间错误指示 LED 闪烁, 因为失效的显示屏无法报告自身的故障; panic() 自行点亮
 * LED.
 */
void leds_init(void);
void led_error_off(void);
void led_error_toggle(void);

#endif /* _LEONIX_LEDS_H */
