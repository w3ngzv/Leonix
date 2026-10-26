// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stop everything once kernel state cannot be trusted, such as after a
 * stack overflow, and show why on the LEDs and the LCD.
 *
 * panic() itself is in start.S: the stack it was called on may be the one
 * that overflowed, so it moves SP to the boot stack before any C runs.
 *
 * 内核状态已不可信时 (例如栈溢出之后) 停止一切, 并在 LED 与 LCD 上显示
 * 原因. panic() 本身在 start.S 中: 调用 panic() 时所在的栈可能正是溢出的
 * 那个栈, 因此 panic() 在执行任何 C 代码之前把 SP 移到启动栈.
 */
#include <avr/io.h>

#include <leonix/kernel.h>
#include <leonix/lcd.h>
#include <leonix/panic.h>

volatile uint8_t oops_in_progress;

/* names in flash, for %S; 存放在 flash 中, 供 %S 输出 */
static const char *reason_name(uint8_t reason)
{
	switch (reason) {
	case PANIC_STACK_OVERFLOW:
		return PSTR("stack");
	case PANIC_BAD_INTERRUPT:
		return PSTR("bad irq");
	default:
		return PSTR("?");
	}
}

/*
 * The LCD may have been cut off in the middle of a transfer, so it is
 * initialised again.  A missing display costs only the I2C timeouts.
 *
 * LCD 可能在一次传输中途被打断, 因此重新初始化. 显示屏不存在时, 代价只是
 * 几次 I2C 超时.
 */
static void panic_show(uint8_t reason, uint8_t task)
{
	char line[LCD_COLS + 1];

	if (!IS_ENABLED(CONFIG_HD44780) || lcd_init() < 0)
		return;
	snprintk(line, sizeof(line), "PANIC %S", reason_name(reason));
	if (lcd_puts(line) < 0)
		return;

	if (task == PANIC_IDLE_TASK)
		snprintk(line, sizeof(line), "task idle");
	else if (task == PANIC_NO_TASK)
		snprintk(line, sizeof(line), "task -");
	else
		snprintk(line, sizeof(line), "task %u", task);
	if (lcd_set_cursor(0, 1) < 0)
		return;
	lcd_puts(line);
}

/**
 * panic_halt() - light the LEDs, show the reason on the LCD, and stop
 * @reason: PANIC_STACK_OVERFLOW or PANIC_BAD_INTERRUPT
 * @task: the task to blame, PANIC_NO_TASK or PANIC_IDLE_TASK
 *
 * All three LEDs lit and steady is a pattern no task produces, and needs
 * no display.  L lights on high, TX and RX on low
 * (Documentation/booting.rst, section 4).
 *
 * 三个 LED 同时常亮是任何任务都不会产生的状态, 且不依赖显示屏. L 高电平
 * 点亮, TX 与 RX 低电平点亮 (Documentation/booting.rst 第 4 节). @reason
 * 是停机原因, @task 是出错的任务, 或 PANIC_NO_TASK、PANIC_IDLE_TASK.
 *
 * Context: jumped to from panic() in start.S, on the boot stack, with
 * interrupts disabled.  由 start.S 中的 panic() 跳转进入, 运行在启动栈上,
 * 中断处于关闭状态.
 */
void panic_halt(uint8_t reason, uint8_t task)
{
	uint8_t nested = oops_in_progress;

	oops_in_progress = 1;
	PORTC |= 1 << PORTC7;
	DDRC |= 1 << DDC7;
	PORTD &= ~(1 << PORTD5);
	DDRD |= 1 << DDD5;
	PORTB &= ~(1 << PORTB0);
	DDRB |= 1 << DDB0;

	/*
	 * a fault while showing the first one: keep the LEDs only
	 * 显示第一次故障时再次出错: 只保留 LED
	 */
	if (!nested)
		panic_show(reason, task);
	for (;;)
		;
}
