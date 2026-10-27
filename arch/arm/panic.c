// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stop everything once kernel state cannot be trusted, and say why on
 * USART1 and the LED.
 *
 * panic() itself is in start.S, which moves SP to the top of the boot
 * stack before any C runs, as on the AVR.  A fault arrives through
 * fault_entry in start.S, which hands fault_report() the exception
 * frame.  As Linux's oops on ARM, the report gives the registers at the
 * fault: those of the frame, R4 to R11 from fault_entry, and the frame's
 * address.  The fault status registers (PM0056 4.4.10 and 4.4.11)
 * follow, with the faulting address when MMFAR or BFAR holds one
 * (4.4.12 and 4.4.13).
 *
 * 内核状态已不可信时停止一切, 并在 USART1 与 LED 上说明原因.
 *
 * panic() 本身在 start.S 中, 与 AVR 相同, 在执行任何 C 代码之前把 SP 移到启动
 * 栈的栈顶. 故障经 start.S 中的 fault_entry 到达, fault_entry 把异常栈帧交给
 * fault_report(). 与 Linux 在 ARM 上的 oops 相同, 报告给出故障时的寄存器:
 * 栈帧中的寄存器、fault_entry 保存的 R4 至 R11, 以及栈帧的地址. 其后是故障状态
 * 寄存器 (PM0056 4.4.10 与 4.4.11), MMFAR 或 BFAR 保存有故障地址时一并输出
 * (4.4.12 与 4.4.13).
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

/*
 * the exception frame, PM0056 2.3.7; 异常栈帧, 见 PM0056 2.3.7
 */
#define FRAME_R0	0
#define FRAME_R12	4
#define FRAME_LR	5
#define FRAME_PC	6
#define FRAME_PSR	7
#define FRAME_WORDS	8
#define CALLEE_WORDS	8	/* R4 to R11; R4 至 R11 */
#define EXC_RETURN_PSP	(1u << 2)
#define PANIC_LINE	64

volatile uint8_t oops_in_progress;

/*
 * IPSR at a bad interrupt, set by start.S
 * 意外中断时的 IPSR, 由 start.S 设置
 */
uint32_t panic_ipsr;

/*
 * R4 to R11 at the fault, stored by fault_entry in start.S
 * 故障时的 R4 至 R11, 由 start.S 中的 fault_entry 存入
 */
uint32_t fault_callee[CALLEE_WORDS];

/*
 * Copied out by fault_report(): panic() moves SP to the top of the boot
 * stack, which overwrites a frame on MSP.  fault_frame_at is 0 if the
 * frame could not be read.
 *
 * 由 fault_report() 复制出来: panic() 把 SP 移到启动栈的栈顶, 位于 MSP 上的
 * 栈帧随之被覆盖. 栈帧无法读取时 fault_frame_at 为 0.
 */
static uint32_t fault_frame[FRAME_WORDS];
static uint32_t fault_frame_at, fault_cfsr, fault_hfsr, fault_addr;

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
	uint8_t i;

	fault_cfsr = SCB_CFSR;
	fault_hfsr = SCB_HFSR;
	if (fault_cfsr & SCB_CFSR_MMARVALID)
		fault_addr = SCB_MMFAR;
	else if (fault_cfsr & SCB_CFSR_BFARVALID)
		fault_addr = SCB_BFAR;
	if ((const uint8_t *)frame >= __ram_start &&
	    (const uint8_t *)(frame + FRAME_WORDS) <= __ram_end) {
		for (i = 0; i < FRAME_WORDS; i++)
			fault_frame[i] = frame[i];
		fault_frame_at = (uint32_t)frame;
	}
	if (exc_return & EXC_RETURN_PSP)
		task = sched_current();
	panic(PANIC_FAULT, task);
}

/*
 * four registers to a line, named rN from @first; 每行四个寄存器, 自 @first
 * 起命名为 rN
 */
static void regs_show(char *line, const uint32_t *regs, uint8_t first)
{
	uint8_t len;

	len = snprintk(line, PANIC_LINE,
		       "r%-2u %08lx r%-2u %08lx r%-2u %08lx r%-2u %08lx\r\n",
		       first, regs[0], first + 1, regs[1],
		       first + 2, regs[2], first + 3, regs[3]);
	usart_panic_write(line, len);
}

/*
 * the registers and fault status that fault_report() kept
 * 输出 fault_report() 保存的寄存器与故障状态
 */
static void fault_show(char *line)
{
	const uint32_t *f = fault_frame;
	uint8_t len;

	if (fault_frame_at) {
		len = snprintk(line, PANIC_LINE,
			       "pc  %08lx lr  %08lx psr %08lx\r\n",
			       f[FRAME_PC], f[FRAME_LR], f[FRAME_PSR]);
		usart_panic_write(line, len);
		regs_show(line, f + FRAME_R0, 0);
	}
	regs_show(line, fault_callee, 4);
	regs_show(line, fault_callee + 4, 8);
	if (fault_frame_at)
		len = snprintk(line, PANIC_LINE, "r12 %08lx frame %08lx\r\n",
			       f[FRAME_R12], fault_frame_at);
	else
		len = snprintk(line, PANIC_LINE, "frame unreadable\r\n");
	usart_panic_write(line, len);

	if (fault_cfsr & (SCB_CFSR_MMARVALID | SCB_CFSR_BFARVALID))
		len = snprintk(line, PANIC_LINE,
			       "cfsr %08lx hfsr %08lx addr %08lx\r\n",
			       fault_cfsr, fault_hfsr, fault_addr);
	else
		len = snprintk(line, PANIC_LINE, "cfsr %08lx hfsr %08lx\r\n",
			       fault_cfsr, fault_hfsr);
	usart_panic_write(line, len);
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

	if (reason == PANIC_FAULT) {
		fault_show(line);
		return;
	}
	if (reason != PANIC_BAD_INTERRUPT)
		return;
	len = snprintk(line, sizeof(line), "exception %lu\r\n", panic_ipsr);
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
