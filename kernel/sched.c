// SPDX-License-Identifier: GPL-2.0-only
/*
 * Round-robin scheduler with sleeping.
 *
 * A task runs until the next tick or until it calls sleep_until(),
 * whichever comes first.  A sleeping task is skipped until its wake-up
 * time; when every task sleeps, the idle task puts the CPU into Idle
 * mode until the next interrupt.
 *
 * 带睡眠的轮转调度器. 任务一直运行到下一个节拍或调用 sleep_until(), 以先到者为
 * 准. 睡眠的任务在唤醒时间之前被跳过; 所有任务都在睡眠时, 空闲任务使 CPU 进入
 * Idle 模式, 直到下一个中断.
 */
#include <leonix/irqflags.h>
#include <leonix/jiffies.h>
#include <leonix/panic.h>
#include <leonix/sched.h>
#include <leonix/wait.h>

/*
 * Written at the lowest two bytes of every stack.  The stack grows down,
 * so these are the last bytes an overflow reaches before it leaves the
 * stack.  The value is the top half of Linux's STACK_END_MAGIC.
 *
 * 写在每个栈最低的两个字节. 栈向下增长, 溢出在越出栈之前最后到达的就是这两个字
 * 节. 取值为 Linux STACK_END_MAGIC 的高半部分.
 */
#define STACK_CANARY_LO	0xac
#define STACK_CANARY_HI	0x57

/*
 * Every stack byte above the canary starts as STACK_FILL, so the lowest
 * byte that no longer holds it marks the deepest the stack has been.  0
 * would not do: the switch frame pushes r1, always 0, and many registers
 * hold 0.  A pushed 0xa5 can still make the count err high.
 *
 * 金丝雀之上的每个栈字节初始为 STACK_FILL, 不再是该值的最低字节即栈曾到达的最
 * 深处. 0 不适用: 切换栈帧压入恒为 0 的 r1, 许多寄存器的值也是 0. 压入的 0xa5
 * 仍可能使计数偏高.
 */
#define STACK_FILL	0xa5
#define STACK_CANARY_SIZE	2

static struct task tasks[MAX_TASKS];
static uint8_t nr_tasks;
static uint8_t current_index;

static struct task idle_task;
static uint8_t idle_stack[IDLE_STACK_SIZE];
static uint32_t idle_ticks;

struct task *current;

/*
 * An entry function that returns lands here, see task_init().  The task
 * leaves the run queue for good; its stack is never reused.
 *
 * 入口函数返回后到达此处, 见 task_init(). 该任务永久离开运行队列, 其栈不再被使
 * 用.
 */
static void task_exit(void)
{
	current->state = TASK_DEAD;
	arch_yield();
	for (;;)
		;
}

/*
 * Fill @stack and lay out the frame from which the arch's restore path
 * enters @entry, with task_exit() as its return address.
 *
 * 填充 @stack, 并布置一个栈帧, 架构的恢复路径由该栈帧进入 @entry, 以
 * task_exit() 为其返回地址.
 */
static void task_init(struct task *t, void (*entry)(void),
		      uint8_t *stack, uint16_t size)
{
	uint16_t n;

	for (n = STACK_CANARY_SIZE; n < size; n++)
		stack[n] = STACK_FILL;
	stack[0] = STACK_CANARY_LO;
	stack[1] = STACK_CANARY_HI;

	t->sp = arch_task_frame(stack + size, entry, task_exit);
	t->stack = stack;
	t->state = TASK_RUNNABLE;
}

/**
 * sched_stack_free() - stack bytes a task has never written
 * @index: the task, or SCHED_IDLE_TASK
 *
 * Counts from the canary up.  The scan stops at the first byte that
 * differs from STACK_FILL, at the latest at the initial frame near the
 * top.  It may read a stack in use, so the result can be a few bytes
 * stale.
 *
 * 任务 @index (SCHED_IDLE_TASK 表示空闲任务) 的栈中, 金丝雀之上从未被写过的字
 * 节数. 扫描在第一个不等于 STACK_FILL 的字节处停止, 最迟停在栈顶附近的初始栈
 * 帧. 扫描可能读到正在使用的栈, 结果因此可能滞后几个字节.
 *
 * Return: the byte count, or SCHED_NO_TASK for an index with no task.
 * 返回字节数; 该编号没有任务时返回 SCHED_NO_TASK.
 */
uint16_t sched_stack_free(uint8_t index)
{
	const struct task *t;
	const uint8_t *p;

	if (index == SCHED_IDLE_TASK)
		t = &idle_task;
	else if (index < nr_tasks)
		t = &tasks[index];
	else
		return SCHED_NO_TASK;

	for (p = t->stack + STACK_CANARY_SIZE; *p == STACK_FILL; p++)
		;
	return p - (t->stack + STACK_CANARY_SIZE);
}

/**
 * sched_task_state() - a task's state
 * @index: the task
 *
 * 任务 @index 的状态.
 *
 * Return: an enum task_state, or SCHED_NO_STATE for an index with no
 * task.
 * 返回 enum task_state 中的值; 该编号没有任务时返回 SCHED_NO_STATE.
 */
uint8_t sched_task_state(uint8_t index)
{
	unsigned long flags;
	uint8_t state;

	if (index >= nr_tasks)
		return SCHED_NO_STATE;
	local_irq_save(flags);
	state = tasks[index].state;
	local_irq_restore(flags);
	return state;
}

/**
 * task_create() - add a task
 * @entry: the function the task runs; its return ends the task
 * @stack: the task's stack
 * @size: bytes in @stack, at least TASK_STACK_RESERVE
 *
 * 添加一个运行 @entry 的任务, @entry 返回即结束该任务. @stack 是任务的栈, @size
 * 是其字节数, 不得小于 TASK_STACK_RESERVE.
 *
 * Context: before sched_start().
 * 在 sched_start() 之前调用.
 * Return: 0, or -1 if the task table is full or @size is too small.
 * 成功时返回 0, 任务表已满或 @size 过小时返回 -1.
 */
int task_create(void (*entry)(void), uint8_t *stack, uint16_t size)
{
	if (nr_tasks == MAX_TASKS || size < TASK_STACK_RESERVE)
		return -1;

	task_init(&tasks[nr_tasks], entry, stack, size);
	nr_tasks++;
	return 0;
}

/*
 * The outgoing task has just been saved on its own stack, so this is
 * the deepest that stack has been since the last check, give or take
 * what the task used and released in between.
 *
 * 切出的任务刚刚保存在自己的栈上, 因此此时是该栈自上次检查以来最深的时刻, 只差
 * 任务在两次检查之间用过又释放的部分.
 */
static void check_stack(const struct task *t)
{
	if (t->stack[0] != STACK_CANARY_LO || t->stack[1] != STACK_CANARY_HI)
		panic(PANIC_STACK_OVERFLOW,
		      t == &idle_task ? PANIC_IDLE_TASK : t - tasks);
}

/**
 * schedule() - pick the task to run next
 *
 * Picks the next runnable task after the current one, waking any sleeper
 * whose time has come on the way.  The current task is looked at last,
 * so it keeps the CPU only when no other task can run.
 *
 * 选出当前任务之后的下一个可运行任务, 途中唤醒到期的睡眠任务. 当前任务最后才被
 * 考虑, 因此只有在没有其他任务可运行时, 当前任务才继续占用 CPU.
 *
 * Context: interrupts disabled, from the tick or arch_yield(), with the
 * outgoing task already saved.
 * 由节拍中断或 arch_yield() 在关中断且切出的任务已保存的状态下调用.
 */
void schedule(void)
{
	uint32_t now = get_jiffies();
	struct task *t;
	uint8_t i = current_index;
	uint8_t n;

	check_stack(current);

	for (n = 0; n < nr_tasks; n++) {
		if (++i == nr_tasks)
			i = 0;
		t = &tasks[i];
		if (t->state == TASK_SLEEPING && time_after_eq(now, t->wake_at))
			t->state = TASK_RUNNABLE;
		if (t->state == TASK_RUNNABLE) {
			current_index = i;
			current = t;
			return;
		}
	}
	current = &idle_task;
}

/**
 * scheduler_tick() - count an idle tick and reschedule
 *
 * The tick lands on whichever task is running at that instant, so the
 * ticks that land on the idle task sample how much of the time the CPU
 * had nothing to do.
 *
 * 节拍落在当时正在运行的任务上, 因此落在空闲任务上的节拍数是对 CPU 空闲时间的
 * 抽样.
 *
 * Context: the tick interrupt.
 * 在节拍中断中调用.
 */
void scheduler_tick(void)
{
	if (current == &idle_task)
		idle_ticks++;
	schedule();
}

/**
 * sched_idle_ticks() - ticks that found the CPU idle
 *
 * Return: the count since sched_start().
 * 返回 sched_start() 以来 CPU 空闲的节拍数.
 */
uint32_t sched_idle_ticks(void)
{
	unsigned long flags;
	uint32_t ticks;

	local_irq_save(flags);
	ticks = idle_ticks;
	local_irq_restore(flags);
	return ticks;
}

/**
 * sleep_until() - sleep until a tick count
 * @deadline: jiffies to wake at
 *
 * An absolute deadline lets a periodic task add its period each round
 * without drifting.  Interrupts are off while the task is marked, so the
 * tick cannot see a half-written wake_at.
 *
 * 睡眠到 jiffies 达到 @deadline. 采用绝对时刻, 周期任务每轮加上周期即可, 不会
 * 漂移. 标记任务期间关中断, 节拍中断因此不会看到写了一半的 wake_at.
 *
 * Context: a task, after sched_start().  Returns with interrupts enabled
 * whatever the caller had, since arch_yield() returns through reti.
 * 在 sched_start() 之后由任务调用. 无论调用者原先的状态如何, 返回时中断都已打
 * 开, 因为 arch_yield() 经由 reti 返回.
 */
void sleep_until(uint32_t deadline)
{
	local_irq_disable();
	current->wake_at = deadline;
	current->state = TASK_SLEEPING;
	arch_yield();
}

_Static_assert(MAX_TASKS <= 8, "struct wait_queue holds one bit per task in a byte");

/**
 * __wait() - wait on a queue
 * @wq: the queue
 * @deadline: jiffies to give up at, if @timed
 * @timed: whether @deadline applies
 *
 * Puts the current task on @wq and gives up the CPU.  The task leaves
 * @wq before returning, since a later wake_up() on @wq must not end some
 * other sleep of this task.
 *
 * 把当前任务放入 @wq 并让出 CPU; @timed 为真时, jiffies 达到 @deadline 也会唤醒
 * 该任务. 返回之前任务离开 @wq, 因为之后对 @wq 的 wake_up() 不应结束该任务的其
 * 他睡眠.
 *
 * Context: wait_event() and wait_event_timeout(), with interrupts
 * disabled and the condition found false.  Returns with interrupts
 * enabled.
 * 由 wait_event() 与 wait_event_timeout() 在关中断且条件不成立时调用. 返回时中
 * 断已打开.
 */
void __wait(struct wait_queue *wq, uint32_t deadline, uint8_t timed)
{
	uint8_t bit = 1 << current_index;

	wq->waiters |= bit;
	current->wake_at = deadline;
	current->state = timed ? TASK_SLEEPING : TASK_BLOCKED;
	arch_yield();

	local_irq_disable();
	wq->waiters &= ~bit;
	local_irq_enable();
}

/**
 * wake_up() - make every task on a queue runnable
 * @wq: the queue
 *
 * A task already woken by its deadline has left TASK_SLEEPING and is not
 * touched.
 *
 * 使 @wq 上的每个任务变为可运行. 已因到期而唤醒的任务已离开 TASK_SLEEPING, 不受
 * 影响.
 *
 * Context: a task or an interrupt handler.
 * 任务或中断处理程序.
 * Return: the number of tasks woken, by which an interrupt handler can
 * tell whether a switch is worth making.
 * 返回被唤醒的任务数, 中断处理程序据此判断是否值得切换任务.
 */
uint8_t wake_up(struct wait_queue *wq)
{
	unsigned long flags;
	uint8_t woken = 0;
	uint8_t i;

	local_irq_save(flags);
	for (i = 0; i < nr_tasks; i++) {
		if (!(wq->waiters & (1 << i)))
			continue;
		if (tasks[i].state == TASK_SLEEPING ||
		    tasks[i].state == TASK_BLOCKED) {
			tasks[i].state = TASK_RUNNABLE;
			woken++;
		}
	}
	wq->waiters = 0;
	local_irq_restore(flags);
	return woken;
}

/**
 * msleep() - sleep for at least a number of milliseconds
 * @ms: milliseconds
 *
 * The current tick may be about to end, so one more is added.
 *
 * 睡眠至少 @ms 毫秒. 当前节拍可能即将结束, 因此多加一个节拍.
 *
 * Context: a task, after sched_start().
 * 在 sched_start() 之后由任务调用.
 */
void msleep(uint16_t ms)
{
	sleep_until(get_jiffies() + ms + 1);
}

/*
 * Runs only when every other task sleeps or is dead.  arch_cpu_idle()
 * stops the CPU until the next interrupt, which may switch to a woken
 * task.
 *
 * 只在其他所有任务都在睡眠或已结束时运行. arch_cpu_idle() 使 CPU 停止到下一个
 * 中断, 该中断可能切换到被唤醒的任务.
 */
static void idle(void)
{
	for (;;)
		arch_cpu_idle();
}

/**
 * sched_start() - start the first task
 *
 * Leaves the boot stack for good: main() never runs again.
 *
 * 启动第一个任务. 从此离开启动栈, main() 不再运行.
 *
 * Context: main(), once.
 * 由 main() 调用一次.
 */
void sched_start(void)
{
	local_irq_disable();
	arch_cpu_idle_prepare();
	task_init(&idle_task, idle, idle_stack, sizeof(idle_stack));

	current_index = 0;
	current = nr_tasks ? &tasks[0] : &idle_task;
	arch_start_first_task();
}
