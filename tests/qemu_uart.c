// SPDX-License-Identifier: GPL-2.0-only
/*
 * stdout on USART0 of the ATmega2560, for host tests built with avr-gcc
 * and run under qemu-system-avr by "make check-qemu".  QEMU sends each
 * byte written to UDR0 to its serial port at once, so the baud rate
 * is never set.
 *
 * ATmega2560 USART0 上的 stdout, 供以 avr-gcc 构建、由 "make check-qemu" 在
 * qemu-system-avr 下运行的主机测试使用. QEMU 把写入 UDR0 的每个字节立即送往其串
 * 口, 因此不设置波特率.
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

/*
 * before main(), so that the test itself needs no change
 * 在 main() 之前运行, 测试本身因此无需修改
 */
__attribute__((constructor)) static void uart_init(void)
{
	UCSR0B = _BV(TXEN0);
	stdout = &uart_stdout;
}
