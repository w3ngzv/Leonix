// SPDX-License-Identifier: GPL-2.0-only
#include <avr/io.h>
#include <util/atomic.h>

#include <leonix/jiffies.h>
#include <leonix/lcd.h>
#include <leonix/sched.h>

/*
 * Sized from what sched_stack_free() reads on the board, with the
 * measurements in Documentation/notes/scheduler.rst.  Recheck them on
 * the stack page after any change to a task.
 */
#define TASK_STACK_SIZE		96
#define LCD_TASK_STACK_SIZE	224
#define TX_BLINK_HALF_PERIOD	(HZ / 8)
#define RX_ERROR_HALF_PERIOD	(HZ / 20)
#define LCD_RETRY_INTERVAL	(HZ / 2)
#define LCD_PAGE_UPDATES	5	/* seconds each page stays up */


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
 * The second page: line 0 is a label, line 1 the stack bytes each task
 * has never touched, from task 0 up, with the idle task last.  The
 * fields share the 16 columns equally.
 */
static int lcd_show_stack(void)
{
	uint8_t n, i, width;

	for (n = 0; sched_stack_free(n) != SCHED_NO_TASK; n++)
		;
	width = LCD_COLS / (n + 1);

	if (lcd_set_cursor(0, 0) < 0 ||
	    lcd_puts("stack free      ") < 0 ||
	    lcd_set_cursor(0, 1) < 0)
		return -1;
	for (i = 0; i < n; i++)
		if (lcd_put_right(sched_stack_free(i), width) < 0)
			return -1;
	return lcd_put_right(sched_stack_free(SCHED_IDLE_TASK),
			     LCD_COLS - n * width);
}

/*
 * Line 0 ends with the share of ticks that found the CPU idle since the
 * previous update, line 1 with the seconds since sched_start().  Both
 * fields are right-aligned so a shorter number overwrites a longer one.
 * @relabel rewrites the label on line 0, which the stack page covers.
 */
static int lcd_show_status(uint32_t idle_ticks, uint32_t ticks, int relabel)
{
	if (relabel && (lcd_set_cursor(0, 0) < 0 ||
			lcd_puts("Leonix idle") < 0))
		return -1;
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
 *
 * Online, the display alternates every LCD_PAGE_UPDATES seconds between
 * the idle share with the uptime and the stack page.
 */
static void lcd_task(void)
{
	uint32_t now, next_update, next_retry, next_blink;
	uint32_t last_jiffies, last_idle, idle_now;
	uint8_t page_age = 0;
	int online, stack_page = 0, err;

	PORTB |= 1 << PORTB0;
	DDRB |= 1 << DDB0;

	online = lcd_show_banner() == 0;
	last_jiffies = next_retry = next_blink = get_jiffies();
	last_idle = sched_idle_ticks();
	next_update = last_jiffies + HZ;
	for (;;) {
		if (online) {
			sleep_until(next_update);
			/*
			 * Both counters are read at one instant, so the idle
			 * ticks and the elapsed ticks cover the same window.
			 * The refresh itself is part of the window: with the
			 * I2C driver sleeping during transfers, most of its
			 * time is idle.
			 */
			ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
				now = get_jiffies();
				idle_now = sched_idle_ticks();
			}
			if (++page_age == LCD_PAGE_UPDATES) {
				page_age = 0;
				stack_page = !stack_page;
			}
			if (stack_page)
				err = lcd_show_stack();
			else
				err = lcd_show_status(idle_now - last_idle,
						      now - last_jiffies,
						      page_age == 0);
			if (err < 0) {
				online = 0;
				next_retry = next_blink = get_jiffies();
				continue;
			}
			last_idle = idle_now;
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
				stack_page = 0;
				page_age = 0;
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
