// SPDX-License-Identifier: GPL-2.0-only
/*
 * System tick from SysTick, counting the processor clock down from
 * STK_LOAD to 0 and reloading (PM0056 4.5).  One period is STK_LOAD + 1
 * clocks, 72000 at 72 MHz, 1 ms.
 *
 * 由 SysTick 产生系统节拍. SysTick 以处理器时钟从 STK_LOAD 递减到 0 后重新装入
 * (PM0056 4.5). 一个周期为 STK_LOAD + 1 个时钟, 72 MHz 下为 72000 个, 即 1 ms.
 */
#include <stdint.h>

#include <asm/setup.h>
#include <asm/stm32f1.h>

#include <leonix/jiffies.h>
#include <leonix/sched.h>

/* STK_LOAD is 24 bits wide (PM0056 4.5.2); STK_LOAD 为 24 位 */
#define STK_LOAD_MAX	0xffffffu

_Static_assert(72000000u / HZ - 1 <= STK_LOAD_MAX,
	       "STK_LOAD cannot hold the tick period at 72 MHz");

static volatile uint32_t jiffies;

/**
 * timer_interrupt() - count a tick and let the scheduler run
 *
 * 节拍计数加一, 再交给调度器.
 *
 * Context: the SysTick handler in switch.S, after the interrupted task
 * is saved.  由 switch.S 中的 SysTick 处理程序在保存被中断的任务之后调用.
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
 *
 * Context: main(), after setup_arch() has set cpu_hz.
 * 由 main() 在 setup_arch() 设置 cpu_hz 之后调用.
 */
void time_init(void)
{
	STK_CTRL = 0;
	STK_LOAD = cpu_hz / HZ - 1;
	STK_VAL = 0;
	STK_CTRL = STK_CTRL_CLKSOURCE | STK_CTRL_TICKINT | STK_CTRL_ENABLE;
}

/**
 * get_jiffies() - ticks since boot
 *
 * The 32-bit count is read by one LDR, and an interrupt is taken only
 * between instructions, so no lock is needed, unlike the four loads on
 * the AVR.
 *
 * 开机以来的节拍数. 32 位计数由一条 LDR 读出, 而中断只在指令之间响应, 因此
 * 不需要关中断, 这一点与 AVR 上的四次读取不同.
 *
 * Return: the tick count, which wraps; compare with time_after_eq().
 * 节拍数, 会回绕, 比较时使用 time_after_eq().
 */
uint32_t get_jiffies(void)
{
	return jiffies;
}
