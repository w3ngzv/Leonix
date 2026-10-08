// SPDX-License-Identifier: GPL-2.0-only
#include <avr/io.h>
#include <stdlib.h>
#include <string.h>

#include <leonix/jiffies.h>
#include <leonix/lcd.h>
#include <leonix/sched.h>

#define TASK_STACK_SIZE		128
#define LCD_TASK_STACK_SIZE	192
#define TX_BLINK_HALF_PERIOD	(HZ / 8)
#define RX_ERROR_HALF_PERIOD	(HZ / 20)

/* "4294967" is the most uptime in seconds a 32-bit jiffies reaches. */
#define UPTIME_DIGITS		7

/*
 * Kept as a writable global so it lands in .data and the copy loop in
 * start.S has something to copy.  A wrong copy shows up as a wrong
 * blink rate.
 */
uint16_t blink_half_period = HZ / 2;

static uint8_t blink_l_stack[TASK_STACK_SIZE];
static uint8_t blink_tx_stack[TASK_STACK_SIZE];
static uint8_t lcd_stack[LCD_TASK_STACK_SIZE];

/*
 * Both tasks spin on jiffies and never yield.  If both LEDs blink, the
 * tick interrupt is taking the CPU away from each of them in turn.
 */
static void blink_l(void)
{
	uint32_t next_toggle = get_jiffies() + blink_half_period;

	DDRC |= 1 << DDC7;
	for (;;) {
		if (time_after_eq(get_jiffies(), next_toggle)) {
			PINC = 1 << PINC7;
			next_toggle += blink_half_period;
		}
	}
}

static void blink_tx(void)
{
	uint32_t next_toggle = get_jiffies() + TX_BLINK_HALF_PERIOD;

	DDRD |= 1 << DDD5;
	for (;;) {
		if (time_after_eq(get_jiffies(), next_toggle)) {
			PIND = 1 << PIND5;
			next_toggle += TX_BLINK_HALF_PERIOD;
		}
	}
}

/*
 * The RX LED stays off while the LCD works and flashes at 10 Hz once a
 * bus transfer has failed, since a dead display cannot report its own
 * fault.  The LED lights on low, so the pin starts high.
 */
static void lcd_failed(void)
{
	uint32_t next_toggle = get_jiffies();

	PORTB |= 1 << PORTB0;
	DDRB |= 1 << DDB0;
	for (;;) {
		if (time_after_eq(get_jiffies(), next_toggle)) {
			PINB = 1 << PINB0;
			next_toggle += RX_ERROR_HALF_PERIOD;
		}
	}
}

/* Line 0 names the kernel, line 1 counts seconds since sched_start(). */
static void lcd_task(void)
{
	char digits[UPTIME_DIGITS + 1];
	uint32_t next_update;
	uint8_t used;

	if (lcd_init() < 0 || lcd_puts("Leonix") < 0)
		lcd_failed();

	next_update = get_jiffies();
	for (;;) {
		if (!time_after_eq(get_jiffies(), next_update))
			continue;
		ultoa(get_jiffies() / HZ, digits, 10);
		if (lcd_set_cursor(0, 1) < 0 ||
		    lcd_puts("up ") < 0 ||
		    lcd_puts(digits) < 0 ||
		    lcd_puts(" s") < 0)
			lcd_failed();
		/* The count shrinks when jiffies wraps; blank what it left. */
		for (used = 3 + strlen(digits) + 2; used < LCD_COLS; used++)
			if (lcd_puts(" ") < 0)
				lcd_failed();
		next_update += HZ;
	}
}

int main(void)
{
	time_init();
	task_create(blink_l, blink_l_stack, sizeof(blink_l_stack));
	task_create(blink_tx, blink_tx_stack, sizeof(blink_tx_stack));
	task_create(lcd_task, lcd_stack, sizeof(lcd_stack));
	sched_start();
}
