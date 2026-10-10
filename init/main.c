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
 * Report each stack whose untouched bytes have reached a new low, the
 * way CONFIG_DEBUG_STACK_USAGE reports a new greatest depth in Linux.
 * Called once a second; after the first few seconds a report means a
 * path deeper than any before it has just run.
 */
static void __attribute__((noinline)) report_stack_usage(void)
{
	static uint16_t lowest[MAX_TASKS + 1] = {
		[0 ... MAX_TASKS] = SCHED_NO_TASK,
	};
	uint16_t free;
	uint8_t i;

	for (i = 0; i <= MAX_TASKS; i++) {
		free = sched_stack_free(i < MAX_TASKS ? i : SCHED_IDLE_TASK);
		if (free == SCHED_NO_TASK || free >= lowest[i])
			continue;
		lowest[i] = free;
		if (i < MAX_TASKS)
			printk("stack t%u %u", i, free);
		else
			printk("stack idle %u", free);
	}
}

/*
 * Line 0 ends with the share of ticks that found the CPU idle since the
 * previous update, line 1 with the seconds since sched_start().  Both
 * fields are right-aligned so a shorter number overwrites a longer one.
 * The label on line 0 is lcd_show_banner()'s.
 */
static int __attribute__((noinline)) lcd_show_status(uint32_t idle_ticks, uint32_t ticks)
{
	unsigned int idle = ticks ? idle_ticks * 100 / ticks : 0;

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
 * The stack figures and the kernel log go to the USB console, see
 * arch/avr/usb.c; the display keeps the status, which needs no computer.
 * The stack check runs at every refresh and every retry, so a stack
 * that runs low while the display is offline is still reported.
 */
static void lcd_task(void)
{
	uint32_t now, next_update, next_retry, next_blink;
	uint32_t last_jiffies, last_idle, idle_now;
	int online, err;

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
			report_stack_usage();
			err = lcd_check();
			if (err == 0) {
				printk("lcd replugged");
				err = lcd_show_banner();
			}
			if (err >= 0)
				err = lcd_show_status(idle_now - last_idle,
						      now - last_jiffies);
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
			report_stack_usage();
			if (lcd_show_banner() == 0) {
				printk("lcd online");
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
	printk("Leonix booted");
	task_create(blink_l, blink_l_stack, sizeof(blink_l_stack));
	task_create(blink_tx, blink_tx_stack, sizeof(blink_tx_stack));
	task_create(lcd_task, lcd_stack, sizeof(lcd_stack));
	if (usb_init() < 0)
		printk("usb init failed");
	sched_start();
}
