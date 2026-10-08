// SPDX-License-Identifier: GPL-2.0-only
/*
 * System tick from Timer1 in CTC mode (mode 4, TOP = OCR1A).
 *
 * The timer counts from 0 to TIMER1_TOP inclusive, so one period is
 * TIMER1_TOP + 1 timer clocks: 16 MHz / 64 / 1000 = 250 clocks, 1 ms.
 */
#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/atomic.h>

#include <leonix/jiffies.h>

/* int is 16 bits here, and TIMER1_PRESCALE * HZ does not fit in it. */
#define TIMER1_PRESCALE	64UL
#define TIMER1_TOP	(F_CPU / TIMER1_PRESCALE / HZ - 1)

_Static_assert(F_CPU % (TIMER1_PRESCALE * HZ) == 0,
	       "tick period is not a whole number of timer clocks");
_Static_assert(TIMER1_TOP <= UINT16_MAX, "OCR1A cannot hold TIMER1_TOP");

static volatile uint32_t jiffies;

ISR(TIMER1_COMPA_vect)
{
	jiffies++;
}

void time_init(void)
{
	TCCR1B = 0;
	TCCR1A = 0;
	TCNT1 = 0;
	OCR1A = TIMER1_TOP;
	TIFR1 = 1 << OCF1A;
	TIMSK1 = 1 << OCIE1A;
	TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10);
}

/* A 32-bit load takes four instructions, so the tick ISR could split it. */
uint32_t get_jiffies(void)
{
	uint32_t now;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		now = jiffies;
	}
	return now;
}
