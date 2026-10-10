// SPDX-License-Identifier: GPL-2.0-only
/*
 * The kernel log: a ring of fixed-size records in SRAM, the way Linux
 * keeps its log_buf, but small enough for 2.5 KiB.  There is no console
 * output here; whoever wants the log reads it with log_last().
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
}

/*
 * Copy the newest record into @rec.  Returns the number of records
 * written since boot, the newest one's sequence number, or 0 when the
 * log is still empty and @rec is left alone.
 */
uint16_t log_last(struct log_record *rec)
{
	uint16_t seq;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		seq = log_seq;
		if (seq)
			*rec = log_buf[log_next ? log_next - 1 : LOG_RECORDS - 1];
	}
	return seq;
}
