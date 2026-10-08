// SPDX-License-Identifier: GPL-2.0-only
#include <avr/interrupt.h>
#include <avr/io.h>

#include <leonix/jiffies.h>

/*
 * Kept as a writable global so it lands in .data and the copy loop in
 * start.S has something to copy.  A wrong copy shows up as a wrong
 * blink rate.
 */
uint16_t blink_half_period = HZ / 2;

int main(void)
{
	uint32_t next_toggle;

	DDRC |= 1 << DDC7;

	time_init();
	sei();

	next_toggle = get_jiffies() + blink_half_period;
	for (;;) {
		if (time_after_eq(get_jiffies(), next_toggle)) {
			PINC = 1 << PINC7;
			next_toggle += blink_half_period;
		}
	}
}
