// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stop everything once kernel state can no longer be trusted, such as
 * after a stack overflow has written over memory nobody owns.
 *
 * With no console the only signal is the three Leonardo LEDs, all lit
 * and steady, a pattern no running task produces.  L lights on high, TX
 * and RX on low (Documentation/booting.rst, section 4).
 */
#include <avr/interrupt.h>
#include <avr/io.h>

#include <leonix/panic.h>

void panic(void)
{
	cli();
	PORTC |= 1 << PORTC7;
	DDRC |= 1 << DDC7;
	PORTD &= ~(1 << PORTD5);
	DDRD |= 1 << DDD5;
	PORTB &= ~(1 << PORTB0);
	DDRB |= 1 << DDB0;
	for (;;)
		;
}
