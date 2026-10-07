// SPDX-License-Identifier: GPL-2.0-only
#include <avr/io.h>
#include <util/delay.h>

#define BLINK_HALF_PERIOD_MS	500

/*
 * For now main() only blinks the L LED on PC7.  It is the first test on
 * real hardware: it confirms the LED pin, and the time from pressing
 * reset to the first blink is the Caterina timeout.
 */
int main(void)
{
	DDRC |= 1 << DDC7;

	for (;;) {
		PINC = 1 << PINC7;
		_delay_ms(BLINK_HALF_PERIOD_MS);
	}
}
