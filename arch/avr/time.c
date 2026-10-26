// SPDX-License-Identifier: GPL-2.0-only
/*
 * System tick from Timer1 in CTC mode (mode 4, TOP = OCR1A).
 *
 * The timer counts from 0 to TIMER1_TOP inclusive, so one period is
 * TIMER1_TOP + 1 timer clocks: 16 MHz / 64 / 1000 = 250 clocks, 1 ms.
 *
 * 由 Timer1 的 CTC 模式 (模式 4, TOP = OCR1A) 产生系统节拍. 定时器从 0 计到
 * TIMER1_TOP (含), 一个周期为 TIMER1_TOP + 1 个定时器时钟: 16 MHz / 64 /
 * 1000 = 250 个时钟, 即 1 ms.
 */
#include <avr/io.h>
#include <util/atomic.h>

#include <leonix/jiffies.h>
#include <leonix/sched.h>

/* UL: TIMER1_PRESCALE * HZ 超出 16 位 int 的范围 */
#define TIMER1_PRESCALE	64UL
#define TIMER1_TOP	(F_CPU / TIMER1_PRESCALE / HZ - 1)

_Static_assert(F_CPU % (TIMER1_PRESCALE * HZ) == 0,
	       "tick period is not a whole number of timer clocks");
_Static_assert(TIMER1_TOP <= UINT16_MAX, "OCR1A cannot hold TIMER1_TOP");

static volatile uint32_t jiffies;

/**
 * timer_interrupt() - count a tick and let the scheduler run
 *
 * 节拍计数加一, 再交给调度器.
 *
 * Context: called from the Timer1 compare match vector in switch.S,
 * after the interrupted task is saved; interrupts disabled.
 * 由 switch.S 中的 Timer1 比较匹配向量在保存被中断的任务之后调用,
 * 中断处于关闭状态.
 */
void timer_interrupt(void)
{
	jiffies++;
	scheduler_tick();
}

/**
 * time_init() - start the tick
 *
 * 启动系统节拍.
 */
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

/**
 * get_jiffies() - ticks since boot
 *
 * The 32-bit load takes four instructions, which the tick could split,
 * so it runs with interrupts off.
 *
 * 开机以来的节拍数. 读取 32 位值需要四条指令, 节拍中断可能落在其间, 因此
 * 读取时关中断.
 *
 * Return: the tick count, which wraps; compare with time_after_eq().
 * 节拍数, 会回绕, 比较时使用 time_after_eq().
 */
uint32_t get_jiffies(void)
{
	uint32_t now;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		now = jiffies;
	}
	return now;
}

