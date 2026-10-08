// SPDX-License-Identifier: GPL-2.0-only
#include <avr/io.h>

#include <leonix/jiffies.h>
#include <leonix/sched.h>

#define TASK_STACK_SIZE		128
#define TX_BLINK_HALF_PERIOD	(HZ / 8)

/*
 * Kept as a writable global so it lands in .data and the copy loop in
 * start.S has something to copy.  A wrong copy shows up as a wrong
 * blink rate.
 */
uint16_t blink_half_period = HZ / 2;

static uint8_t blink_l_stack[TASK_STACK_SIZE];
static uint8_t blink_tx_stack[TASK_STACK_SIZE];

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

int main(void)
{
	time_init();
	task_create(blink_l, blink_l_stack, sizeof(blink_l_stack));
	task_create(blink_tx, blink_tx_stack, sizeof(blink_tx_stack));
	sched_start();
}
