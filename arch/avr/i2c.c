// SPDX-License-Identifier: GPL-2.0-only
/*
 * TWI master, transmit only, polled.
 *
 * Only one task may use the bus; there is no locking.  i2c_write() waits
 * on jiffies for its timeout, so it must run with the tick going, that
 * is from a task and not from main() before sched_start().
 *
 * SDA is PD1 and SCL is PD0, D2 and D3 on the Leonardo header.
 *
 * The bus can be unplugged and plugged back while running.  A failed
 * transfer ends in i2c_reset(), which also frees a slave left holding
 * SDA low, so the next i2c_write() starts from an idle bus.
 */
#include <avr/io.h>
#include <util/delay.h>
#include <util/twi.h>

#include <leonix/i2c.h>
#include <leonix/jiffies.h>
#include <leonix/panic.h>

/* PCF8574 is a standard-mode part, 100 kHz at most. */
#define I2C_SCL_HZ	100000UL
#define I2C_TWBR	((F_CPU / I2C_SCL_HZ - 16) / 2)

/* One byte takes 90 us at 100 kHz; anything near 2 ms means a stuck bus. */
#define I2C_TIMEOUT_MS	2

/* Polling step of a timeout after panic(), see i2c_timed_out(). */
#define I2C_SPIN_US	10

/*
 * UM10204 3.1.16: a slave stuck holding SDA low lets go within nine
 * clocks.  Each half clock is 5 us, above the 4.7 us tLOW and 4.0 us
 * tHIGH of standard mode (UM10204 table 11).
 */
#define I2C_CLEAR_CLOCKS	9
#define I2C_HALF_CLOCK_US	5

_Static_assert(I2C_TWBR >= 10, "datasheet 20.5.2: TWBR below 10 corrupts master output");
_Static_assert(I2C_TWBR <= 255, "TWBR is 8 bits, needs a TWI prescaler");

static void i2c_reset(void);

/*
 * The internal pull-ups (20 to 50 kOhm, datasheet table 29-1) are weak
 * next to the backpack's own, but they keep both lines high while the
 * backpack is unplugged.  A floating bus can read as busy and leave
 * every START to time out; a pulled-up one answers with a fast NACK.
 *
 * Every PORTD and DDRD change in this file touches a single bit, which
 * avr-gcc emits as one sbi or cbi.  blink_tx writes PIND on the same
 * port from another task, and an in/ori/out sequence here could be
 * switched out halfway and write back a stale PORTD5.
 */
void i2c_init(void)
{
	PORTD |= 1 << PORTD0;
	PORTD |= 1 << PORTD1;
	TWSR = 0;			/* prescaler 1 */
	TWBR = I2C_TWBR;
	i2c_reset();
}

/*
 * A wait has a jiffies deadline and a spin budget.  Normally the
 * deadline counts: the task can be switched out for two ticks or more
 * between any two instructions, so only the clock measures time.  After
 * panic() the tick is off and jiffies stands still, but nothing switches
 * the CPU away either, so counting I2C_SPIN_US steps measures it.
 */
struct i2c_deadline {
	uint32_t jiffies;
	uint16_t spins;
};

static void i2c_deadline_start(struct i2c_deadline *d)
{
	d->jiffies = get_jiffies() + I2C_TIMEOUT_MS + 1;
	d->spins = I2C_TIMEOUT_MS * 1000UL / I2C_SPIN_US;
}

/*
 * True once @d has passed and @mask in TWCR still reads @busy.
 *
 * The clock is sampled before the register.  A transfer that finished
 * while the task was switched out is then seen as finished when TWCR is
 * read, not as a timeout.
 */
static int i2c_timed_out(struct i2c_deadline *d, uint8_t mask, uint8_t busy)
{
	int expired;

	if (oops_in_progress) {
		_delay_us(I2C_SPIN_US);
		expired = d->spins-- == 0;
	} else {
		expired = time_after_eq(get_jiffies(), d->jiffies);
	}
	return expired && (TWCR & mask) == busy;
}

/* Returns the status code with the prescaler bits masked, or -1. */
static int i2c_wait(void)
{
	struct i2c_deadline deadline;

	i2c_deadline_start(&deadline);
	while (!(TWCR & (1 << TWINT)))
		if (i2c_timed_out(&deadline, 1 << TWINT, 0))
			return -1;
	return TW_STATUS;
}

/*
 * Open-drain by hand while the TWI is off: drive low with PORT 0 and
 * DDR 1, release with DDR 0 and PORT 1.  The order never passes through
 * DDR 1 with PORT 1, which would drive the line high against a slave.
 */
static void scl_low(void)
{
	PORTD &= ~(1 << PORTD0);
	DDRD |= 1 << DDD0;
}

static void scl_release(void)
{
	DDRD &= ~(1 << DDD0);
	PORTD |= 1 << PORTD0;
}

static void sda_low(void)
{
	PORTD &= ~(1 << PORTD1);
	DDRD |= 1 << DDD1;
}

static void sda_release(void)
{
	DDRD &= ~(1 << DDD1);
	PORTD |= 1 << PORTD1;
}

/*
 * Disabling TWEN drops whatever the hardware was doing and hands the
 * pins back to PORTD.  If a slave still holds SDA, clock it out, then
 * put a STOP on the bus so every slave returns to idle.
 *
 * Being switched out between edges only stretches a half clock, which
 * standard mode allows; it has no lower clock limit.
 */
static void i2c_reset(void)
{
	uint8_t i;

	TWCR = 0;

	for (i = 0; i < I2C_CLEAR_CLOCKS && !(PIND & (1 << PIND1)); i++) {
		scl_low();
		_delay_us(I2C_HALF_CLOCK_US);
		scl_release();
		_delay_us(I2C_HALF_CLOCK_US);
	}

	/* STOP: SDA rises while SCL is high. */
	scl_low();
	sda_low();
	_delay_us(I2C_HALF_CLOCK_US);
	scl_release();
	_delay_us(I2C_HALF_CLOCK_US);
	sda_release();
	_delay_us(I2C_HALF_CLOCK_US);

	TWCR = 1 << TWEN;
}

/*
 * The hardware clears TWSTO once the STOP is on the bus.  A START
 * written to TWCR before that would replace the pending STOP.
 */
static void i2c_stop(void)
{
	struct i2c_deadline deadline;

	i2c_deadline_start(&deadline);
	TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
	while (TWCR & (1 << TWSTO))
		if (i2c_timed_out(&deadline, 1 << TWSTO, 1 << TWSTO)) {
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
