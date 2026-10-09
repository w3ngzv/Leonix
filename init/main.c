// SPDX-License-Identifier: GPL-2.0-only
#include <avr/io.h>

#include <leonix/jiffies.h>
#include <leonix/lcd.h>
#include <leonix/sched.h>

#define TASK_STACK_SIZE		128
#define LCD_TASK_STACK_SIZE	192
#define TX_BLINK_HALF_PERIOD	(HZ / 8)
#define RX_ERROR_HALF_PERIOD	(HZ / 20)
#define LCD_RETRY_INTERVAL	(HZ / 2)


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

/*
 * Write @value in decimal, right-aligned in a field of @width characters.
 * A value with more digits than @width is written in full.  Digits are
 * produced from the right, so the field needs no reversal and no length
 * count before padding.  @width must not exceed LCD_COLS; a 32-bit value
 * has at most 10 digits, which also fits.
 */
static int lcd_put_right(uint32_t value, uint8_t width)
{
	char field[LCD_COLS + 1];
	char *p = field + LCD_COLS;

	*p = '\0';
	do {
		*--p = '0' + value % 10;
		value /= 10;
	} while (value);
	while (p > field + LCD_COLS - width)
		*--p = ' ';
	return lcd_puts(p);
}

/* Line 0 names the kernel.  The display needs it again after a replug. */
static int lcd_show_banner(void)
{
	if (lcd_init() < 0 || lcd_puts("Leonix idle") < 0)
		return -1;
	return 0;
}

/*
 * Line 0 ends with the share of ticks that found the CPU idle since the
 * previous update, line 1 with the seconds since sched_start().  Both
 * fields are right-aligned so a shorter number overwrites a longer one.
 */
static int lcd_show_status(uint32_t idle_ticks, uint32_t ticks)
{
	if (lcd_set_cursor(LCD_COLS - 5, 0) < 0 ||
	    lcd_put_right(ticks ? idle_ticks * 100 / ticks : 0, 4) < 0 ||
	    lcd_puts("%") < 0 ||
	    lcd_set_cursor(0, 1) < 0 ||
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
	uint32_t last_jiffies, last_idle;
	int online;

	PORTB |= 1 << PORTB0;
	DDRB |= 1 << DDB0;

	online = lcd_show_banner() == 0;
	last_jiffies = next_retry = next_blink = get_jiffies();
	last_idle = sched_idle_ticks();
	next_update = last_jiffies + HZ;
	for (;;) {
		if (online) {
			sleep_until(next_update);
			now = get_jiffies();
			if (lcd_show_status(sched_idle_ticks() - last_idle,
					    now - last_jiffies) < 0) {
				online = 0;
				next_retry = next_blink = get_jiffies();
				continue;
			}
			last_idle = sched_idle_ticks();
			last_jiffies = now;
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
				last_jiffies = get_jiffies();
				last_idle = sched_idle_ticks();
				next_update = last_jiffies + HZ;
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
