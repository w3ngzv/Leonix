// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stop everything once kernel state cannot be trusted, and say why on
 * USART1 and the LED.
 *
 * panic() itself is in start.S, which moves SP to the top of the boot
 * stack before any C runs, as on the AVR.  A fault arrives through
 * fault_entry in start.S, which hands fault_report() the exception
 * frame; the stacked PC and the fault status registers (PM0056 4.4.10
 * and 4.4.11) are printed with the reason.
 *
 * 内核状态已不可信时停止一切, 并在 USART1 与 LED 上说明原因.
 *
 * panic() 本身在 start.S 中, 与 AVR 相同, 在执行任何 C 代码之前把 SP 移到启动
 * 栈的栈顶. 故障经 start.S 中的 fault_entry 到达, fault_entry 把异常栈帧交给
 * fault_report(); 压栈的 PC 与故障状态寄存器 (PM0056 4.4.10 与 4.4.11) 随原因
 * 一起输出.
 */
#include <stdint.h>

#include <asm/sections.h>
#include <asm/stm32f1.h>
#include <asm/usart.h>

#include <leonix/kernel.h>
#include <leonix/leds.h>
#include <leonix/panic.h>
#include <leonix/pgmspace.h>
#include <leonix/sched.h>

/*
 * the LED pin, see leds.c; it lights on low
 * LED 所在引脚, 见 leds.c; 低电平点亮
 */
#define LED_PIN		13

#define FRAME_PC	6	/* in the exception frame; 在异常栈帧中 */
#define FRAME_WORDS	8
#define EXC_RETURN_PSP	(1u << 2)
#define PANIC_LINE	48

volatile uint8_t oops_in_progress;

/*
 * IPSR at a bad interrupt, set by start.S
 * 意外中断时的 IPSR, 由 start.S 设置
 */
uint32_t panic_ipsr;

static uint32_t fault_pc, fault_cfsr, fault_hfsr;

static const char *reason_name(uint8_t reason)
{
	switch (reason) {
	case PANIC_STACK_OVERFLOW:
		return PSTR("stack");
	case PANIC_BAD_INTERRUPT:
		return PSTR("bad irq");
	case PANIC_FAULT:
		return PSTR("fault");
	default:
		return PSTR("?");
	}
}

/**
 * fault_report() - record a fault and panic
 * @frame: the exception frame, on MSP or PSP
 * @exc_return: EXC_RETURN from LR
 *
 * A stacking fault can leave @frame outside SRAM, so it is read only
 * when it lies wholly inside.  A fault in Thread mode is blamed on the
 * running task.
 *
 * 记录故障并调用 panic(). 压栈故障可能使 @frame 落在 SRAM 之外, 因此只在其完全
 * 位于 SRAM 之内时读取. Thread 模式下的故障归咎于正在运行的任务.
 *
 * Context: fault_entry in start.S.
 * 由 start.S 中的 fault_entry 调用.
 */
void fault_report(const uint32_t *frame, uint32_t exc_return)
{
	uint8_t task = PANIC_NO_TASK;

	fault_cfsr = SCB_CFSR;
	fault_hfsr = SCB_HFSR;
	if ((const uint8_t *)frame >= __ram_start &&
	    (const uint8_t *)(frame + FRAME_WORDS) <= __ram_end)
		fault_pc = frame[FRAME_PC];
	if (exc_return & EXC_RETURN_PSP)
		task = sched_current();
	panic(PANIC_FAULT, task);
}

static void panic_show(uint8_t reason, uint8_t task)
{
	char line[PANIC_LINE];
	uint8_t len;

	if (task == PANIC_IDLE_TASK)
		len = snprintk(line, sizeof(line), "\r\nPANIC %S task idle\r\n",
			       reason_name(reason));
	else if (task == PANIC_NO_TASK)
		len = snprintk(line, sizeof(line), "\r\nPANIC %S task -\r\n",
			       reason_name(reason));
	else
		len = snprintk(line, sizeof(line), "\r\nPANIC %S task %u\r\n",
			       reason_name(reason), task);
	usart_panic_write(line, len);

	if (reason == PANIC_FAULT)
		len = snprintk(line, sizeof(line),
			       "pc %08lx cfsr %08lx hfsr %08lx\r\n",
			       fault_pc, fault_cfsr, fault_hfsr);
	else if (reason == PANIC_BAD_INTERRUPT)
		len = snprintk(line, sizeof(line), "exception %lu\r\n",
			       panic_ipsr);
	else
		return;
	usart_panic_write(line, len);
}

/**
 * panic_halt() - light the LED, report on USART1, and stop
 * @reason: PANIC_STACK_OVERFLOW, PANIC_BAD_INTERRUPT or PANIC_FAULT
 * @task: the task to blame, PANIC_NO_TASK or PANIC_IDLE_TASK
 *
 * The LED lit and steady is a pattern no task produces.
 *
 * 点亮 LED, 在 USART1 上报告, 然后停机. LED 常亮是任何任务都不会产生的状态.
 * @reason 是停机原因, @task 是出错的任务, 或 PANIC_NO_TASK、PANIC_IDLE_TASK.
 *
 * Context: jumped to from panic() in start.S, on the boot stack, with
 * interrupts disabled.  由 start.S 中的 panic() 跳转进入, 运行在启动栈上,
 * 中断处于关闭状态.
 */
void panic_halt(uint8_t reason, uint8_t task)
{
	uint8_t nested = oops_in_progress;

	oops_in_progress = 1;
	leds_init();
	GPIO_BRR(GPIOC_BASE) = 1u << LED_PIN;

	/*
	 * a fault while reporting the first one: keep the LED only
	 * 报告第一次故障时再次出错: 只保留 LED
	 */
	if (!nested)
		panic_show(reason, task);
	for (;;)
		;
}
