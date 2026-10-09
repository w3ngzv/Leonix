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
#define LCD_RETRY_INTERVAL	(HZ / 2)

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
 * Each LED task sleeps until its next toggle.  The deadline advances by
 * a fixed period from the previous one, not from the time the task woke,
 * so the rate does not drift by however late a wake-up was.
 */
static void blink_l(void)
{
	uint32_t next_toggle = get_jiffies();

	DDRC |= 1 << DDC7;
	for (;;) {
		next_toggle += blink_half_period;
		sleep_until(next_toggle);
		PINC = 1 << PINC7;
	}
}

static void blink_tx(void)
{
	uint32_t next_toggle = get_jiffies();

	DDRD |= 1 << DDD5;
	for (;;) {
		next_toggle += TX_BLINK_HALF_PERIOD;
		sleep_until(next_toggle);
		PIND = 1 << PIND5;
	}
}

/* Write @value right-aligned in a field of @width characters. */
static int lcd_put_right(uint32_t value, uint8_t width)
{
	char digits[UPTIME_DIGITS + 1];
	uint8_t len;

	ultoa(value, digits, 10);
	for (len = strlen(digits); len < width; len++)
		if (lcd_puts(" ") < 0)
			return -1;
	return lcd_puts(digits);
}

/* Line 0 names the kernel.  The display needs it again after a replug. */
static int lcd_show_banner(void)
{
	if (lcd_init() < 0 || lcd_puts("Leonix") < 0)
		return -1;
	return 0;
}

/*
 * Line 1 counts the seconds since sched_start(), right-aligned so a
 * shorter number overwrites a longer one.
 */
static int lcd_show_status(void)
{
	if (lcd_set_cursor(0, 1) < 0 ||
	    lcd_puts("up") < 0 ||
	    lcd_put_right(get_jiffies() / HZ, LCD_COLS - 4) < 0 ||
	    lcd_puts(" s") < 0)
		return -1;
	return 0;
}

/*
 * A failed transfer means the backpack is gone or the bus glitched.
 * The task then retries a full lcd_init() every LCD_RETRY_INTERVAL,
 * since a backpack that lost power comes back with an uninitialised
 * HD44780, and goes back to the status once one succeeds.
 *
 * While offline the RX LED flashes at 10 Hz, since a dead display
 * cannot report its own fault.  RX lights on low, so it is off with the
 * pin high.
 *
 * The driver only writes, so a failure shows only as a missing ACK.  A
 * backpack unplugged and back within one update period, under a second,
 * goes unnoticed, and the display stays blank until the next reset.
 */
static void lcd_task(void)
{
	uint32_t now, next_update, next_retry, next_blink;
	int online;

	PORTB |= 1 << PORTB0;
	DDRB |= 1 << DDB0;

	online = lcd_show_banner() == 0;
	next_retry = next_blink = get_jiffies();
	next_update = next_retry + HZ;
	for (;;) {
		if (online) {
			sleep_until(next_update);
			if (lcd_show_status() < 0) {
				online = 0;
				next_retry = next_blink = get_jiffies();
				continue;
			}
			next_update += HZ;
			continue;
		}

		now = get_jiffies();
		if (time_after_eq(now, next_blink)) {
			PINB = 1 << PINB0;
			next_blink = now + RX_ERROR_HALF_PERIOD;
		}
		if (time_after_eq(now, next_retry)) {
			if (lcd_show_banner() == 0) {
				online = 1;
				PORTB |= 1 << PORTB0;
				next_update = get_jiffies() + HZ;
				continue;
			}
			next_retry = get_jiffies() + LCD_RETRY_INTERVAL;
		}
		sleep_until(next_blink);
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
