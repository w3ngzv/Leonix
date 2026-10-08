// SPDX-License-Identifier: GPL-2.0-only
/*
 * TWI master, transmit only, polled.
 *
 * Only one task may use the bus; there is no locking.  i2c_write() waits
 * on jiffies for its timeout, so it must run with the tick going, that
 * is from a task and not from main() before sched_start().
 *
 * SDA is PD1 and SCL is PD0, D2 and D3 on the Leonardo header.  The
 * internal pull-ups stay off: the usual PCF8574 LCD backpacks carry
 * their own.
 */
#include <avr/io.h>
#include <util/twi.h>

#include <leonix/i2c.h>
#include <leonix/jiffies.h>

/* PCF8574 is a standard-mode part, 100 kHz at most. */
#define I2C_SCL_HZ	100000UL
#define I2C_TWBR	((F_CPU / I2C_SCL_HZ - 16) / 2)

/* One byte takes 90 us at 100 kHz; anything near 2 ms means a stuck bus. */
#define I2C_TIMEOUT_MS	2

_Static_assert(I2C_TWBR >= 10, "datasheet 20.5.2: TWBR below 10 corrupts master output");
_Static_assert(I2C_TWBR <= 255, "TWBR is 8 bits, needs a TWI prescaler");

void i2c_init(void)
{
	TWSR = 0;			/* prescaler 1 */
	TWBR = I2C_TWBR;
	TWCR = 1 << TWEN;
}

/*
 * True once @deadline has passed and @mask in TWCR still reads @busy.
 *
 * The clock is sampled before the register.  The task can be switched
 * out for two ticks or more between any two instructions, while the
 * TWI hardware keeps running; reading TWCR last means a transfer that
 * finished during that gap is seen as finished, not as a timeout.
 */
static int i2c_timed_out(uint32_t deadline, uint8_t mask, uint8_t busy)
{
	int expired = time_after_eq(get_jiffies(), deadline);

	return expired && (TWCR & mask) == busy;
}

/* Returns the status code with the prescaler bits masked, or -1. */
static int i2c_wait(void)
{
	uint32_t deadline = get_jiffies() + I2C_TIMEOUT_MS + 1;

	while (!(TWCR & (1 << TWINT)))
		if (i2c_timed_out(deadline, 1 << TWINT, 0))
			return -1;
	return TW_STATUS;
}

/* Disabling TWEN drops whatever the hardware was doing on the bus. */
static void i2c_reset(void)
{
	TWCR = 0;
	TWCR = 1 << TWEN;
}

/*
 * The hardware clears TWSTO once the STOP is on the bus.  A START
 * written to TWCR before that would replace the pending STOP.
 */
static void i2c_stop(void)
{
	uint32_t deadline = get_jiffies() + I2C_TIMEOUT_MS + 1;

	TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
	while (TWCR & (1 << TWSTO))
		if (i2c_timed_out(deadline, 1 << TWSTO, 1 << TWSTO)) {
			i2c_reset();
			return;
		}
}

/*
 * Send @len bytes to the 7-bit address @addr.  @len may be 0, which
 * only checks that a device answers at @addr.
 *
 * Returns 0, or -1 on NACK, lost arbitration or timeout.
 */
int i2c_write(uint8_t addr, const uint8_t *buf, uint8_t len)
{
	TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
	if (i2c_wait() != TW_START)
		goto fail;

	TWDR = (uint8_t)(addr << 1) | TW_WRITE;
	TWCR = (1 << TWINT) | (1 << TWEN);
	if (i2c_wait() != TW_MT_SLA_ACK)
		goto fail;

	while (len--) {
		TWDR = *buf++;
		TWCR = (1 << TWINT) | (1 << TWEN);
		if (i2c_wait() != TW_MT_DATA_ACK)
			goto fail;
	}

	i2c_stop();
	return 0;

fail:
	/*
	 * After a NACK the master still owns the bus and must release it.
	 * After a timeout or lost arbitration the state is unknown, and a
	 * reset is the only way back.
	 */
	if (TWCR & (1 << TWINT) && TW_STATUS != TW_MT_ARB_LOST)
		i2c_stop();
	else
		i2c_reset();
	return -1;
}
