// SPDX-License-Identifier: GPL-2.0-only
#include <avr/io.h>
#include <util/atomic.h>

#include <leonix/jiffies.h>
#include <leonix/kernel.h>
#include <leonix/lcd.h>
#include <leonix/printk.h>
#include <leonix/sched.h>
#include <leonix/usb.h>

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
#define LCD_PAGES		3	/* status, stack, log */


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
 * Format one line, or part of one, and write it at (@col, @row).
 *
 * The lcd_show_*() functions are kept out of line.  Inlined, their line
 * buffers would all sit in lcd_task()'s frame, under every printk() and
 * I2C transfer the task makes.
 */
#define lcd_print_at(col, row, fmt, ...)				\
	({								\
		char __line[LCD_COLS + 1];				\
									\
		snprintk(__line, sizeof(__line), fmt, ##__VA_ARGS__);	\
		lcd_set_cursor((col), (row)) < 0 || lcd_puts(__line) < 0 \
			? -1 : 0;					\
	})

/* Line 0 names the kernel.  The display needs it again after a replug. */
static int __attribute__((noinline)) lcd_show_banner(void)
{
	if (lcd_init() < 0)
		return -1;
	return lcd_print_at(0, 0, "Leonix idle");
}

/*
 * The second page: line 0 is a label, line 1 the stack bytes each task
 * has never touched, from task 0 up, with the idle task last.  The
 * fields share the 16 columns equally.
 */
static int __attribute__((noinline)) lcd_show_stack(void)
{
	char line[LCD_COLS + 1];
	uint8_t n, i, width, len = 0;

	for (n = 0; sched_stack_free(n) != SCHED_NO_TASK; n++)
		;
	width = LCD_COLS / (n + 1);

	for (i = 0; i < n; i++)
		len += snprintk(line + len, sizeof(line) - len, "%*u", width,
				sched_stack_free(i));
	snprintk(line + len, sizeof(line) - len, "%*u", LCD_COLS - len,
		 sched_stack_free(SCHED_IDLE_TASK));

	/* line holds the figures; the label goes out first, from flash. */
	if (lcd_set_cursor(0, 0) < 0 ||
	    lcd_puts_P(PSTR("stack free      ")) < 0 ||
	    lcd_set_cursor(0, 1) < 0)
		return -1;
	return lcd_puts(line);
}

/*
 * The third page: line 0 the number and time of the newest kernel log
 * record, line 1 its text.  Older records stay in the log unread.
 */
static int __attribute__((noinline)) lcd_show_log(void)
{
	struct log_record rec;
	uint16_t seq = log_last(&rec);

	if (!seq)
		return lcd_print_at(0, 0, "log empty       ") < 0 ||
		       lcd_print_at(0, 1, "%16s", "") < 0 ? -1 : 0;
	if (lcd_print_at(0, 0, "log %-5u%5u s", seq, rec.secs) < 0)
		return -1;
	return lcd_print_at(0, 1, "%-16s", rec.text);
}

/*
 * Line 0 ends with the share of ticks that found the CPU idle since the
 * previous update, line 1 with the seconds since sched_start().  Both
 * fields are right-aligned so a shorter number overwrites a longer one.
 * @relabel rewrites the label on line 0, which the stack page covers.
 */
static int __attribute__((noinline)) lcd_show_status(uint32_t idle_ticks, uint32_t ticks, int relabel)
{
	unsigned int idle = ticks ? idle_ticks * 100 / ticks : 0;

	if (relabel && lcd_print_at(0, 0, "Leonix idle") < 0)
		return -1;
	if (lcd_print_at(LCD_COLS - 5, 0, "%4u%%", idle) < 0)
		return -1;
	return lcd_print_at(0, 1, "up%12lu s", get_jiffies() / HZ);
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
 * A backpack unplugged and back within one update period, under a
 * second, misses no ACK.  lcd_check() before each refresh catches it
 * instead, by the expander's power-on state, and the display is
 * initialised again on the spot.
 *
 * Online, the display steps every LCD_PAGE_UPDATES seconds through the
 * idle share with the uptime, the stack page and the newest log record.
 */
static void lcd_task(void)
{
	uint32_t now, next_update, next_retry, next_blink;
	uint32_t last_jiffies, last_idle, idle_now;
	uint8_t page_age = 0;
	int online, err;
	uint8_t page = 0;

	PORTB |= 1 << PORTB0;
	DDRB |= 1 << DDB0;

	online = lcd_show_banner() == 0;
	if (online)
		printk("pcf8574 at %x", lcd_address());
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
				if (++page == LCD_PAGES)
					page = 0;
			}
			err = lcd_check();
			if (err == 0) {
				printk("lcd replugged");
				err = lcd_show_banner();
				page_age = 0;
				page = 0;
			}
			if (err >= 0) {
				if (page == 1)
					err = lcd_show_stack();
				else if (page == 2)
					err = lcd_show_log();
				else
					err = lcd_show_status(idle_now - last_idle,
							      now - last_jiffies,
							      page_age == 0);
			}
			if (err < 0) {
				printk("lcd offline");
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
				printk("lcd online");
				online = 1;
				page = 0;
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
	printk("Leonix booted");
	task_create(blink_l, blink_l_stack, sizeof(blink_l_stack));
	task_create(blink_tx, blink_tx_stack, sizeof(blink_tx_stack));
	task_create(lcd_task, lcd_stack, sizeof(lcd_stack));
	if (usb_init() < 0)
		printk("usb init failed");
	sched_start();
}
