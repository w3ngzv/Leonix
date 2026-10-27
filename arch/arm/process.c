// SPDX-License-Identifier: GPL-2.0-only
/*
 * The first frame of a task, and the way a task gives up the CPU.  The
 * frame is the one switch.S saves and restores; the two must change
 * together.
 *
 * 任务的第一个栈帧, 以及任务让出 CPU 的方式. 该栈帧即 switch.S 保存与恢复的
 * 栈帧, 两者必须一起修改.
 */
#include <stdint.h>
#include <string.h>

#include <asm/stm32f1.h>

#include <leonix/sched.h>

/*
 * r4-r11 from switch.S, then r0-r3, r12, lr, pc and xPSR as the
 * exception entry stacks them (PM0056 2.3.7)
 * switch.S 压入的 r4-r11, 其上是异常进入时压入的 r0-r3、r12、lr、pc 与 xPSR
 * (PM0056 2.3.7)
 */
#define FRAME_WORDS	16
#define FRAME_LR	13
#define FRAME_PC	14
#define FRAME_XPSR	15

/*
 * the Thumb bit, which must be set in every stacked xPSR (PM0056 2.1.3)
 * Thumb 位, 每个压栈的 xPSR 都必须置位该位 (PM0056 2.1.3)
 */
#define XPSR_T		(1u << 24)

#define FRAME_ALIGN	8u

/**
 * arch_task_frame() - build the frame a task is first entered from
 * @top: one past the highest byte of the stack
 * @entry: the task's function
 * @exit: where @entry returns to
 *
 * The frame is what the exception return in switch.S expects: the
 * restored PC is @entry with bit 0 clear, as the core stacks it, LR is
 * @exit, and xPSR has only the Thumb bit set.  The top is aligned down
 * to 8 bytes, so bit 9 of the stacked xPSR, which records the padding
 * (PM0056 2.3.7), stays 0.
 *
 * 栈帧即 switch.S 中的异常返回所期待的内容: 恢复的 PC 为第 0 位清零的
 * @entry, 与内核压栈的形式相同; LR 为 @exit; xPSR 只置位 Thumb 位. 栈顶向下
 * 对齐到 8 字节, 因此压栈 xPSR 中记录填充的第 9 位 (PM0056 2.3.7) 保持为 0.
 *
 * Context: kernel/sched.c, before the task first runs.
 * 由 kernel/sched.c 在任务首次运行之前调用.
 * Return: the stack pointer to store in the task.
 * 应存入任务的栈指针.
 */
uint8_t *arch_task_frame(uint8_t *top, void (*entry)(void),
			 void (*exit)(void))
{
	uint32_t *sp = (uint32_t *)((uintptr_t)top & ~(uintptr_t)(FRAME_ALIGN - 1));

	sp -= FRAME_WORDS;
	memset(sp, 0, FRAME_WORDS * sizeof(*sp));
	sp[FRAME_LR] = (uint32_t)exit;
	sp[FRAME_PC] = (uint32_t)entry & ~1u;
	sp[FRAME_XPSR] = XPSR_T;
	return (uint8_t *)sp;
}

/**
 * arch_yield() - give up the CPU before the tick is over
 *
 * Pends PendSV, whose handler in switch.S saves the task and calls
 * schedule(), then enables interrupts, which lets PendSV run.  As on
 * the AVR, the caller may have interrupts disabled and gets them back
 * enabled.
 *
 * 在节拍结束之前让出 CPU. 挂起 PendSV, 其在 switch.S 中的处理程序保存任务并调用
 * schedule(); 然后打开中断, PendSV 随即执行. 与 AVR 相同, 调用者可以在关中断
 * 状态下调用, 返回时中断已打开.
 *
 * Context: a task, after sched_start().
 * 在 sched_start() 之后由任务调用.
 */
void arch_yield(void)
{
	SCB_ICSR = SCB_ICSR_PENDSVSET;
	__asm__ __volatile__("dsb\n\tcpsie i\n\tisb" ::: "memory");
}
