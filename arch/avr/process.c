// SPDX-License-Identifier: GPL-2.0-only
/*
 * The first frame of a task, which kernel/sched.c asks for once per
 * task, as Linux's copy_thread() prepares a new thread.  The frame is
 * the one switch.S saves and restores; the two must change together.
 *
 * 任务的第一个栈帧, kernel/sched.c 为每个任务请求一次, 与 Linux 的
 * copy_thread() 为新线程所做的准备相同. 该栈帧即 switch.S 保存与恢复的栈帧,
 * 两者必须一起修改.
 */
#include <stdint.h>

#include <leonix/sched.h>

/*
 * r1..r31, saved below r0 and SREG in the frame
 * 栈帧中保存在 r0 与 SREG 之下的 r1..r31
 */
#define NR_SAVED_GPRS	31

/**
 * arch_task_frame() - build the frame a task is first entered from
 * @top: one past the highest byte of the stack
 * @entry: the task's function
 * @exit: where @entry returns to
 *
 * Lays out the stack so that the restore path in switch.S, followed by
 * reti, enters @entry with interrupts enabled.  From the top down:
 * @exit as @entry's return address, @entry as the address reti returns
 * to, then r0, SREG and r1..r31.  Return addresses are stored high byte
 * at the lower address, as CALL leaves them.
 *
 * 布置栈, 使 switch.S 的恢复路径与随后的 reti 在中断打开的状态下进入 @entry.
 * 自栈顶向下依次为: 作为 @entry 返回地址的 @exit, 作为 reti 返回地址的
 * @entry, 然后是 r0、SREG 与 r1..r31. 返回地址的高字节存放在低地址, 与 CALL
 * 指令留下的顺序相同.
 *
 * Context: kernel/sched.c, before the task first runs.
 * 由 kernel/sched.c 在任务首次运行之前调用.
 * Return: the stack pointer to store in the task.
 * 应存入任务的栈指针.
 */
uint8_t *arch_task_frame(uint8_t *top, void (*entry)(void),
			 void (*exit)(void))
{
	uint16_t exit_pc = (uint16_t)exit;
	uint16_t entry_pc = (uint16_t)entry;
	uint8_t *sp = top - 1;
	uint8_t i;

	*sp-- = exit_pc & 0xff;
	*sp-- = exit_pc >> 8;
	*sp-- = entry_pc & 0xff;
	*sp-- = entry_pc >> 8;
	*sp-- = 0;			/* r0 */
	/*
	 * SREG with I clear, as in every frame saved inside the interrupt;
	 * reti sets I.  Setting it here would enable interrupts one pop
	 * early.
	 *
	 * SREG 的 I 位为 0, 与中断内保存的每个栈帧相同; reti 会置位 I. 在此置位
	 * 会使中断提前一次出栈而打开.
	 */
	*sp-- = 0;
	for (i = 0; i < NR_SAVED_GPRS; i++)
		*sp-- = 0;		/* r1..r31; C 要求 r1 为 0 */
	return sp;
}
