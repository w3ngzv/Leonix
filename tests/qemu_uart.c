// SPDX-License-Identifier: GPL-2.0-only
/*
 * stdout on USART0 of the ATmega2560, for host tests built with avr-gcc
 * and run under qemu-system-avr by "make check-qemu".  QEMU sends each
 * byte written to UDR0 to its serial port at once, so the baud rate
 * is never set.
 */
#include <avr/io.h>
#include <stdio.h>

static int uart_putchar(char c, FILE *stream)
{
	(void)stream;
	if (c == '\n')
		uart_putchar('\r', stream);
	while (!(UCSR0A & _BV(UDRE0)))
		;
	UDR0 = c;
	return 0;
}

static FILE uart_stdout = FDEV_SETUP_STREAM(uart_putchar, NULL, _FDEV_SETUP_WRITE);

/* Before main(), so that the test itself needs no change. */
__attribute__((constructor)) static void uart_init(void)
{
	UCSR0B = _BV(TXEN0);
	stdout = &uart_stdout;
}
