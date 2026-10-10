// SPDX-License-Identifier: GPL-2.0-only
/*
 * HD44780 character LCD behind a PCF8574 I2C expander, 4-bit mode,
 * write only.
 *
 * The expander pins are wired on the backpack board, not by the LCD
 * or the PCF8574, and boards differ.  The mapping below is the common
 * one: P0 RS, P1 RW, P2 E, P3 backlight, P4..P7 to DB4..DB7.
 *
 * RW is held low, so the busy flag is never read.  Each nibble is one
 * I2C transaction of an address and three data bytes, about 0.4 ms at
 * 100 kHz, which already exceeds the 37 us most instructions need
 * (HD44780U table 6).  Only clear needs an explicit wait.
 */
#include <avr/pgmspace.h>
#include <util/delay.h>

#include <leonix/i2c.h>
#include <leonix/lcd.h>
#include <leonix/panic.h>
#include <leonix/sched.h>

#define PCF_RS		(1 << 0)
#define PCF_RW		(1 << 1)
#define PCF_E		(1 << 2)
#define PCF_BACKLIGHT	(1 << 3)

/* PCF8574 answers at 0x20..0x27, PCF8574A at 0x38..0x3f. */
#define PCF8574_BASE	0x20
#define PCF8574A_BASE	0x38
#define PCF_NR_ADDRS	8

#define LCD_CLEAR		0x01
#define LCD_ENTRY_INC		0x06	/* cursor moves right, no shift */
#define LCD_DISPLAY_OFF		0x08
#define LCD_DISPLAY_ON		0x0c	/* no cursor, no blink */
#define LCD_FUNC_4BIT_2LINE	0x28	/* 4-bit, 2 lines, 5x8 dots */
#define LCD_SET_DDRAM		0x80

/*
 * Table 6 gives no execution time for clear display.  Return home, the
 * other instruction that resets the address counter, lists 1.52 ms at
 * 270 kHz; 2 ms leaves a margin for a slower oscillator.
 */
#define LCD_CLEAR_MS	2

static uint8_t pcf_addr;

/*
 * Sleep in normal running.  After panic() the scheduler must not be
 * entered again, so the wait is a busy loop instead; the HD44780 waits
 * only have a lower bound, and either way meets it.
 */
static void lcd_delay(uint16_t ms)
{
	if (!oops_in_progress) {
		msleep(ms);
		return;
	}
	while (ms--)
		_delay_ms(1);
}

/*
 * E must rise after RS and the data are stable, and the HD44780 latches
 * on the falling edge, hence three bytes: set up, E high, E low.
 */
static int lcd_write_nibble(uint8_t nibble, uint8_t rs)
{
	uint8_t out = (uint8_t)(nibble << 4) | rs | PCF_BACKLIGHT;
	uint8_t seq[3] = { out, out | PCF_E, out };

	return i2c_write(pcf_addr, seq, sizeof(seq));
}

static int lcd_write_byte(uint8_t byte, uint8_t rs)
{
	if (lcd_write_nibble(byte >> 4, rs) < 0)
		return -1;
	return lcd_write_nibble(byte & 0x0f, rs);
}

static int lcd_command(uint8_t cmd)
{
	return lcd_write_byte(cmd, 0);
}

static int pcf_probe(void)
{
	static const uint8_t bases[] = { PCF8574_BASE, PCF8574A_BASE };
	uint8_t b, i;

	for (b = 0; b < sizeof(bases); b++)
		for (i = 0; i < PCF_NR_ADDRS; i++)
			if (i2c_write(bases[b] + i, 0, 0) == 0) {
				pcf_addr = bases[b] + i;
				return 0;
			}
	return -1;
}

/*
 * Follows HD44780U figure 24.  The display may have been left in 4-bit
 * mode by an earlier run, with half a byte already latched, so the
 * three 0x3 nibbles are needed to resynchronise it, not only after
 * power on.
 *
 * Returns 0, or -1 if no expander answers or a write fails.
 */
int lcd_init(void)
{
	i2c_init();
	if (pcf_probe() < 0)
		return -1;

	lcd_delay(50);			/* > 40 ms after VCC rises to 2.7 V */
	if (lcd_write_nibble(0x3, 0) < 0)
		return -1;
	lcd_delay(5);			/* > 4.1 ms */
	if (lcd_write_nibble(0x3, 0) < 0)
		return -1;
	lcd_delay(1);			/* > 100 us */
	if (lcd_write_nibble(0x3, 0) < 0 ||
	    lcd_write_nibble(0x2, 0) < 0)
		return -1;

	if (lcd_command(LCD_FUNC_4BIT_2LINE) < 0 ||
	    lcd_command(LCD_DISPLAY_OFF) < 0 ||
	    lcd_clear() < 0 ||
	    lcd_command(LCD_ENTRY_INC) < 0 ||
	    lcd_command(LCD_DISPLAY_ON) < 0)
		return -1;
	return 0;
}

int lcd_clear(void)
{
	if (lcd_command(LCD_CLEAR) < 0)
		return -1;
	lcd_delay(LCD_CLEAR_MS);
	return 0;
}

/*
 * In a two-line display the second line starts at DDRAM 0x40
 * (HD44780U, Set DDRAM Address).  Out-of-range positions are clamped.
 */
int lcd_set_cursor(uint8_t col, uint8_t row)
{
	static const uint8_t row_start[LCD_ROWS] = { 0x00, 0x40 };

	if (col >= LCD_COLS)
		col = LCD_COLS - 1;
	if (row >= LCD_ROWS)
		row = LCD_ROWS - 1;
	return lcd_command(LCD_SET_DDRAM | (row_start[row] + col));
}

int lcd_puts(const char *s)
{
	while (*s)
		if (lcd_write_byte((uint8_t)*s++, PCF_RS) < 0)
			return -1;
	return 0;
}

/*
 * Check that the display has kept the state lcd_init() gave it.
 *
 * A backpack unplugged and plugged back between two transfers never
 * misses an ACK, yet its HD44780 has reset to 8-bit mode and reads every
 * later nibble wrongly.  The PCF8574 has reset too, and its outputs read
 * back high (TI SCPS068K, power-on reset), while every byte the driver
 * writes last leaves E and RW low.
 *
 * Only E and RW are compared.  Both drive HD44780 inputs, so they read
 * back what was written.  The backlight pin does not: on the board this
 * was tested with, it reads low while written high, as a transistor base
 * would hold it, and comparing the whole byte reported a replug on every
 * refresh.
 *
 * Returns 1 if the display is as left, 0 if it must be initialised
 * again, or -1 if the expander does not answer.
 */
int lcd_check(void)
{
	uint8_t port;

	if (i2c_read(pcf_addr, &port, 1) < 0)
		return -1;
	return !(port & (PCF_E | PCF_RW));
}

/* The I2C address lcd_init() found the expander at, 0 before that. */
uint8_t lcd_address(void)
{
	return pcf_addr;
}
