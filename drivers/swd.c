// SPDX-License-Identifier: GPL-2.0-only
/*
 * A Serial Wire Debug host on two GPIO pins, enough to identify an STM32F1
 * without a debug probe.  Reading /dev/swd connects to the target, powers
 * up its debug port and reads four words, one line each:
 *
 *   DPIDR   the SW-DP identification register
 *   IDCODE  DBGMCU_IDCODE at 0xE0042000, RM0008 31.6.1
 *   FLASH   the flash size register at 0x1FFFF7E0, KiB in the low half,
 *           RM0008 30.1.1
 *   CPUID   the Cortex-M3 CPUID register at 0xE000ED00
 *
 * The last three addresses are the STM32F1's; on another chip they read
 * whatever lies there, or fault.
 *
 * D8 (PB4) drives SWCLK, D9 (PB5) carries SWDIO, both left as inputs
 * between reads.  The pins swing 5 V into a 3.3 V target: STM32F103 PA13
 * and PA14 are marked FT in DS5319, and the target's 3.3 V high is above
 * the 1.9 V VIH (0.2 VCC + 0.9) the ATmega32U4 has at 5 V, datasheet 7766J.
 * A resistor of about 1 kOhm in the SWDIO line limits the current if both
 * ends drive it at once around a turnaround.
 *
 * Bit order and packet layout follow the Arm Debug Interface v5: the
 * target samples SWDIO on the rising edge of SWCLK and changes it after
 * that edge, so the host reads a bit while SWCLK is low.  SWD has no
 * lowest clock rate, which lets interrupts stretch any cycle.
 */
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <util/delay.h>

#include <leonix/fs.h>
#include <leonix/kernel.h>
#include <leonix/swd.h>

#define SWCLK		PB4
#define SWDIO		PB5
#define HALF_CYCLE_US	2

#define ACK_OK		1
#define ACK_WAIT	2
#define ACK_FAULT	4
#define ACK_NONE	7	/* nothing drove the line */
#define ERR_PARITY	8	/* not an ACK: a read whose parity bit was wrong */
#define ERR_POWER	9	/* not an ACK: the debug port never powered up */

#define DP		0
#define AP		1
#define WRITE		0
#define READ		1

/* DP registers, by their A[3:2] byte address */
#define DP_DPIDR	0x0	/* read */
#define DP_ABORT	0x0	/* write */
#define DP_CTRL_STAT	0x4
#define DP_SELECT	0x8
#define DP_RDBUFF	0xc

/* AHB-AP registers in bank 0 */
#define AP_CSW		0x0
#define AP_TAR		0x4
#define AP_DRW		0xc

#define ABORT_CLEAR_ALL	0x0000001eUL	/* STKCMPCLR, STKERRCLR, WDERRCLR, ORUNERRCLR */
#define PWRUP_REQ	0x50000000UL	/* CSYSPWRUPREQ | CDBGPWRUPREQ */
#define PWRUP_ACK	0xa0000000UL	/* CSYSPWRUPACK | CDBGPWRUPACK */
#define PWRUP_TRIES	16
#define CSW_WORD	0x23000002UL	/* 32-bit accesses, no auto-increment */

#define JTAG_TO_SWD	0xe79e
#define RESET_ONES	56		/* a line reset needs at least 50 */

enum { SWD_DPIDR, SWD_IDCODE, SWD_FLASH, SWD_CPUID, NR_SWD_WORDS };

static const uint32_t mem_addrs[] PROGMEM = {
	[SWD_IDCODE] = 0xe0042000UL,
	[SWD_FLASH] = 0x1ffff7e0UL,
	[SWD_CPUID] = 0xe000ed00UL,
};

static const char dpidr_name[] PROGMEM = "DPIDR ";
static const char idcode_name[] PROGMEM = "IDCODE";
static const char flash_name[] PROGMEM = "FLASH ";
static const char cpuid_name[] PROGMEM = "CPUID ";

static const char *const word_names[NR_SWD_WORDS] PROGMEM = {
	dpidr_name, idcode_name, flash_name, cpuid_name,
};

static uint32_t words[NR_SWD_WORDS];
static uint8_t status[NR_SWD_WORDS];	/* ACK_OK, another ACK, or ERR_* */
static char line[24];

static void write_bits(uint32_t v, uint8_t n)
{
	while (n--) {
		if (v & 1)
			PORTB |= 1 << SWDIO;
		else
			PORTB &= ~(1 << SWDIO);
		v >>= 1;
		_delay_us(HALF_CYCLE_US);
		PORTB |= 1 << SWCLK;
		_delay_us(HALF_CYCLE_US);
		PORTB &= ~(1 << SWCLK);
	}
}

static uint32_t read_bits(uint8_t n)
{
	uint32_t v = 0;
	uint8_t i;

	for (i = 0; i < n; i++) {
		_delay_us(HALF_CYCLE_US);
		if (PINB & (1 << SWDIO))
			v |= (uint32_t)1 << i;
		PORTB |= 1 << SWCLK;
		_delay_us(HALF_CYCLE_US);
		PORTB &= ~(1 << SWCLK);
	}
	return v;
}

/* Low before output, so the pull-up is never switched on into the target. */
static void swdio_release(void)
{
	PORTB &= ~(1 << SWDIO);
	DDRB &= ~(1 << SWDIO);
}

static void swdio_drive(void)
{
	DDRB |= 1 << SWDIO;
}

/*
 * One SWD transaction.  @addr is the register's byte address, of which
 * only A[3:2] travel.  Returns ACK_OK, the ACK the target sent, or
 * ERR_PARITY for a read that arrived damaged.
 */
static uint8_t transfer(uint8_t apndp, uint8_t rnw, uint8_t addr,
			uint32_t *data)
{
	uint8_t req, ack;
	uint32_t v;

	req = apndp << 1 | rnw << 2 | (addr & 0x0c) << 1;
	if (__builtin_parity(req))
		req |= 1 << 5;
	req |= 1 | 1 << 7;		/* start and park; stop is 0 */

	swdio_drive();
	write_bits(req, 8);
	swdio_release();
	read_bits(1);			/* turnaround */
	ack = read_bits(3);
	if (ack != ACK_OK) {
		read_bits(1);
		swdio_drive();
		write_bits(0, 8);
		return ack;
	}
	if (rnw) {
		v = read_bits(32);
		if (read_bits(1) != (uint32_t)__builtin_parityl(v))
			ack = ERR_PARITY;
		*data = v;
		read_bits(1);
		swdio_drive();
	} else {
		read_bits(1);
		swdio_drive();
		write_bits(*data, 32);
		write_bits(__builtin_parityl(*data), 1);
	}
	write_bits(0, 8);		/* idle cycles to finish the transaction */
	return ack;
}

static uint8_t dp_write(uint8_t addr, uint32_t v)
{
	return transfer(DP, WRITE, addr, &v);
}

/* AP reads are posted: the value comes back from the following RDBUFF. */
static uint8_t mem_read(uint32_t addr, uint32_t *v)
{
	uint8_t ack;

	ack = transfer(AP, WRITE, AP_TAR, &addr);
	if (ack == ACK_OK)
		ack = transfer(AP, READ, AP_DRW, v);
	if (ack == ACK_OK)
		ack = transfer(DP, READ, DP_RDBUFF, v);
	return ack;
}

static void line_reset(void)
{
	write_bits(0xffffffffUL, 32);
	write_bits(0xffffffffUL, RESET_ONES - 32);
}

static uint8_t power_up(void)
{
	uint32_t ctrl = 0;
	uint8_t i, ack;

	ack = dp_write(DP_ABORT, ABORT_CLEAR_ALL);
	if (ack == ACK_OK)
		ack = dp_write(DP_CTRL_STAT, PWRUP_REQ);
	for (i = 0; ack == ACK_OK && i < PWRUP_TRIES; i++) {
		ack = transfer(DP, READ, DP_CTRL_STAT, &ctrl);
		if ((ctrl & PWRUP_ACK) == PWRUP_ACK)
			break;
	}
	if (ack == ACK_OK && (ctrl & PWRUP_ACK) != PWRUP_ACK)
		ack = ERR_POWER;
	if (ack == ACK_OK)
		ack = dp_write(DP_SELECT, 0);
	if (ack == ACK_OK) {
		ctrl = CSW_WORD;
		ack = transfer(AP, WRITE, AP_CSW, &ctrl);
	}
	return ack;
}

/* Fills words[] and status[]; a failed step marks every word after it. */
static void __attribute__((noinline)) identify(void)
{
	uint8_t i, ack;

	DDRB |= 1 << SWCLK;
	swdio_drive();
	line_reset();
	write_bits(JTAG_TO_SWD, 16);
	line_reset();
	write_bits(0, 8);

	ack = transfer(DP, READ, DP_DPIDR, &words[SWD_DPIDR]);
	status[SWD_DPIDR] = ack;
	if (ack == ACK_OK)
		ack = power_up();
	for (i = SWD_IDCODE; i < NR_SWD_WORDS; i++) {
		if (ack == ACK_OK)
			ack = mem_read(pgm_read_dword(&mem_addrs[i]), &words[i]);
		status[i] = ack;
	}

	swdio_release();
	DDRB &= ~(1 << SWCLK);
}

static const char *status_text(uint8_t s)
{
	switch (s) {
	case ACK_WAIT:
		return PSTR("wait");
	case ACK_FAULT:
		return PSTR("fault");
	case ACK_NONE:
		return PSTR("no reply");
	case ERR_PARITY:
		return PSTR("parity error");
	default:
		return PSTR("no power-up");
	}
}

static uint8_t __attribute__((noinline)) swd_line(uint8_t n)
{
	const char *name;

	if (n >= NR_SWD_WORDS)
		return 0;
	if (n == 0)
		identify();
	name = pgm_read_ptr(&word_names[n]);
	if (status[n] == ACK_OK)
		return snprintk(line, sizeof(line), "%S %08lx\n", name,
				words[n]);
	if (status[n] < ERR_PARITY && status[n] != ACK_WAIT &&
	    status[n] != ACK_FAULT && status[n] != ACK_NONE)
		return snprintk(line, sizeof(line), "%S ack %u\n", name,
				status[n]);
	return snprintk(line, sizeof(line), "%S %S\n", name,
			status_text(status[n]));
}

/* private_data is the line number, f_pos the offset in it, as in /proc. */
static int swd_read(struct file *f, char *buf, uint8_t count)
{
	uint8_t len, n;

	len = swd_line(f->private_data);
	if (!len)
		return 0;
	n = len - f->f_pos;
	if (n > count)
		n = count;
	memcpy(buf, line + f->f_pos, n);
	f->f_pos += n;
	if (f->f_pos == len) {
		f->f_pos = 0;
		f->private_data++;
	}
	return n;
}

static const struct file_operations swd_fops = {
	.read = swd_read,
};

int swd_init(void)
{
	return register_chrdev(PSTR("swd"), &swd_fops);
}
