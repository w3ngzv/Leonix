// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stop everything once kernel state can no longer be trusted, such as
 * after a stack overflow has written over memory nobody owns, and say
 * why on the LCD.
 *
 * panic() itself is in start.S: it moves SP to RAMEND before any C code
 * runs, because the stack it was called on may be the one that
 * overflowed.  The boot stack under RAMEND is unused once sched_start()
 * has left main() for good.
 */
#include <avr/io.h>

#include <leonix/lcd.h>
#include <leonix/panic.h>

volatile uint8_t oops_in_progress;

static const char *reason_name(uint8_t reason)
{
	switch (reason) {
	case PANIC_STACK_OVERFLOW:
		return "stack";
	case PANIC_BAD_INTERRUPT:
		return "bad irq";
	default:
		return "?";
	}
}

/*
 * The LCD may have been cut off in the middle of a transfer, so it is
 * initialised again from scratch.  A missing or dead display only costs
 * the I2C timeouts; the LEDs are lit before it is tried.
 */
static void panic_show(uint8_t reason, uint8_t task)
{
	char task_name[2] = { '0' + task, '\0' };

	if (lcd_init() < 0 ||
	    lcd_puts("PANIC ") < 0 ||
	    lcd_puts(reason_name(reason)) < 0 ||
	    lcd_set_cursor(0, 1) < 0 ||
	    lcd_puts("task ") < 0)
		return;
	if (task == PANIC_IDLE_TASK)
		lcd_puts("idle");
	else if (task == PANIC_NO_TASK)
		lcd_puts("-");
	else
		lcd_puts(task_name);
}

/*
 * The three Leonardo LEDs all lit and steady are a pattern no running
 * task produces, and they need no working display.  L lights on high,
 * TX and RX on low (Documentation/booting.rst, section 4).
 */
void panic_halt(uint8_t reason, uint8_t task)
{
	uint8_t nested = oops_in_progress;

	oops_in_progress = 1;
	PORTC |= 1 << PORTC7;
	DDRC |= 1 << DDC7;
	PORTD &= ~(1 << PORTD5);
	DDRD |= 1 << DDD5;
	PORTB &= ~(1 << PORTB0);
	DDRB |= 1 << DDB0;

	/* A fault while showing the first one: keep the LEDs only. */
	if (!nested)
		panic_show(reason, task);
	for (;;)
		;
}
