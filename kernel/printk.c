// SPDX-License-Identifier: GPL-2.0-only
/*
 * The kernel log: a ring of fixed-size records in SRAM, the way Linux
 * keeps its log_buf, but small enough for 2.5 KiB.  A console
 * registered with register_console() is told of each record and copies
 * it out with log_read().
 *
 * printk() formats on the caller's stack and copies the record in with
 * interrupts disabled, so it may be called from a task, from an
 * interrupt handler, and before sched_start().  Each call costs up to
 * 82 bytes of stack below the caller: keep it out of deep call chains
 * such as the I2C transfer path.
 */
#include <stdarg.h>
#include <string.h>
#include <util/atomic.h>

#include <leonix/jiffies.h>
#include <leonix/printk.h>

static struct log_record log_buf[LOG_RECORDS];
static uint8_t log_next;	/* slot the next record goes into */
static uint16_t log_seq;	/* records written since boot */

/*
 * Called after every new record.  A console copies records out with
 * log_read() from its own task; the call only tells it there is more.
 */
static void (*console_kick)(void);

void printk_P(const char *fmt, ...)
{
	struct log_record rec;
	va_list ap;

	va_start(ap, fmt);
	vsnprintk_P(rec.text, sizeof(rec.text), fmt, ap);
	va_end(ap);
	rec.secs = get_jiffies() / HZ;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		log_buf[log_next] = rec;
		if (++log_next == LOG_RECORDS)
			log_next = 0;
		log_seq++;
	}
	if (console_kick)
		console_kick();
}

/* Set before sched_start(); printk() may run in an interrupt. */
void register_console(void (*kick)(void))
{
	console_kick = kick;
}

/* The sequence number of the newest record, 0 while the log is empty. */
uint16_t log_newest(void)
{
	uint16_t seq;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		seq = log_seq;
	}
	return seq;
}

/*
 * Copy record @seq, counted from 1, into @rec.  Returns -1 if @seq is
 * not in the log, either not written yet or already overwritten.
 */
int log_read(uint16_t seq, struct log_record *rec)
{
	int ret = -1;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		if (seq && seq <= log_seq && log_seq - seq < LOG_RECORDS) {
			*rec = log_buf[(seq - 1) % LOG_RECORDS];
			ret = 0;
		}
	}
	return ret;
}
