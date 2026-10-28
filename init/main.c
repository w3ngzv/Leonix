// SPDX-License-Identifier: GPL-2.0-only
/*
 * main(): bring up the kernel, start the status, USB and init tasks, and
 * hand the CPU to the scheduler.  The status task shows the uptime and
 * the idle share on the LCD and reports stack use to the log.
 *
 * main(): 初始化内核, 启动状态、USB 与 init 三个任务, 然后把 CPU 交给
 * 调度器. 状态任务在 LCD 上显示运行时间与空闲比例, 并把栈用量写入日志.
 */
#include <stdint.h>

#include <leonix/irqflags.h>
#include <leonix/jiffies.h>
#include <leonix/kernel.h>
#include <leonix/lcd.h>
#include <leonix/leds.h>
#include <leonix/pgmspace.h>
#include <leonix/printk.h>
#include <leonix/sched.h>
#include <leonix/init.h>
#include <leonix/usb.h>

#define LED_ERROR_HALF_PERIOD	(HZ / 20)
#define LCD_RETRY_INTERVAL	(HZ / 2)


static uint8_t status_stack[STATUS_STACK_SIZE] __task_stack;
static uint8_t init_stack[INIT_STACK_SIZE] __task_stack;

static void stack_task(void);

/*
 * Format a line, or part of one, and write it at (@col, @row).  The
 * lcd_show_*() functions are noinline: inlined, their line buffers would
 * all sit in lcd_task()'s frame, under every printk() and I2C transfer.
 *
 * 格式化一行或一行的一部分, 写到 (@col, @row). lcd_show_*() 各函数标为
 * noinline: 若被内联, 它们的行缓冲区都会留在 lcd_task() 的栈帧中, 位于每次
 * printk() 与 I2C 传输之下.
 */
#define lcd_print_at(col, row, fmt, ...)				\
	({								\
		char __line[LCD_COLS + 1];				\
									\
		snprintk(__line, sizeof(__line), fmt, ##__VA_ARGS__);	\
		lcd_set_cursor((col), (row)) < 0 || lcd_puts(__line) < 0 \
			? -1 : 0;					\
	})

/*
 * line 0's label, written again after a replug
 * 第 0 行的标签, 重新插入后再写一次
 */
static int __attribute__((noinline)) lcd_show_banner(void)
{
	if (lcd_init() < 0)
		return -1;
	return lcd_print_at(0, 0, "Leonix idle");
}

/*
 * Report each stack whose untouched bytes reach a new low, as Linux's
 * CONFIG_DEBUG_STACK_USAGE reports a new greatest depth.  After the first
 * seconds, a report means a path deeper than any before has run.
 *
 * 某个栈未被写过的字节数降到新低时报告, 与 Linux 的
 * CONFIG_DEBUG_STACK_USAGE 报告新的最大深度相同. 开机最初几秒之后, 出现报告
 * 即说明刚刚执行了一条比以往都深的路径.
 */
static void __attribute__((noinline)) report_stack_usage(void)
{
	static uint16_t lowest[MAX_TASKS + 1] = {
		[0 ... MAX_TASKS] = SCHED_NO_TASK,
	};
	uint16_t free;
	uint8_t i;

	for (i = 0; i <= MAX_TASKS; i++) {
		free = sched_stack_free(i < MAX_TASKS ? i : SCHED_IDLE_TASK);
		if (free == SCHED_NO_TASK || free >= lowest[i])
			continue;
		lowest[i] = free;
		if (i < MAX_TASKS)
			pr_info("stack t%u %u", i, free);
		else
			pr_info("stack idle %u", free);
	}
}

/*
 * Line 0 ends with the share of ticks idle since the last update, line 1
 * with the seconds since sched_start().  Both are right-aligned, so a
 * shorter number overwrites a longer one.
 *
 * 第 0 行末尾是自上次更新以来空闲节拍的比例, 第 1 行末尾是 sched_start()
 * 以来的秒数. 两者都右对齐, 较短的数字因此能覆盖较长的数字.
 */
static int __attribute__((noinline)) lcd_show_status(uint32_t idle_ticks, uint32_t ticks)
{
	unsigned int idle = ticks ? idle_ticks * 100 / ticks : 0;

	if (lcd_print_at(LCD_COLS - 5, 0, "%4u%%", idle) < 0)
		return -1;
	return lcd_print_at(0, 1, "up%12lu s", get_jiffies() / HZ);
}

/*
 * Take the display for one refresh.  Once /dev/lcd has been written the
 * display is the device's, and task 0 keeps only the stack reports.
 *
 * 为一次刷新获取显示屏. /dev/lcd 被写入之后显示屏归该设备使用, 任务 0
 * 此后只做栈用量报告.
 */
static void lcd_take(void)
{
	if (lcd_lock() == 0)
		return;
	led_error_off();
	pr_info("lcd given to /dev/lcd");
	stack_task();
}

/*
 * A failed transfer means the backpack is gone or the bus glitched.  The
 * task then retries a full lcd_init() every LCD_RETRY_INTERVAL, since a
 * backpack that lost power comes back with an uninitialised HD44780, and
 * meanwhile flashes the error LED, see include/leonix/leds.h: a dead
 * display cannot report its own fault.
 *
 * A replug within one update period misses no ACK; lcd_check() before
 * each refresh catches it by the expander's power-on state.
 *
 * Stack reports run at every refresh and every retry, so a stack that
 * runs low while the display is offline is still reported.
 *
 * 传输失败说明转接板已断开或总线出现毛刺. 此后任务每隔 LCD_RETRY_INTERVAL
 * 重试一次完整的 lcd_init(), 因为断电后重新接上的转接板, 其 HD44780 尚未
 * 初始化; 其间闪烁错误指示 LED (见 include/leonix/leds.h), 因为失效的显示屏
 * 无法报告自身的故障.
 *
 * 一个更新周期内的拔插不会漏掉 ACK; 每次刷新前的 lcd_check() 通过扩展器的
 * 上电状态发现这种情况.
 *
 * 每次刷新与每次重试都检查栈, 因此显示屏离线期间栈用量变低同样会被报告.
 */
static void lcd_task(void)
{
	uint32_t now, next_update, next_retry, next_blink;
	uint32_t last_jiffies, last_idle, idle_now;
	unsigned long flags;
	int online, err;

	led_error_off();

	lcd_take();
	online = lcd_show_banner() == 0;
	lcd_unlock();
	if (online)
		pr_info("pcf8574 at %x", lcd_address());
	last_jiffies = next_retry = next_blink = get_jiffies();
	last_idle = sched_idle_ticks();
	next_update = last_jiffies + HZ;
	for (;;) {
		if (online) {
			sleep_until(next_update);
			/*
			 * Both counters are read at one instant, so they cover
			 * the same window.  The refresh is inside the window;
			 * the I2C driver sleeps during transfers, so most of
			 * it is idle.
			 *
			 * 两个计数在同一时刻读取, 因此覆盖同一时间段. 刷新本身
			 * 也在该时间段内; I2C 驱动在传输期间睡眠, 刷新的大部分
			 * 时间是空闲.
			 */
			local_irq_save(flags);
			now = get_jiffies();
			idle_now = sched_idle_ticks();
			local_irq_restore(flags);
			report_stack_usage();
			lcd_take();
			err = lcd_check();
			if (err == 0) {
				pr_info("lcd replugged");
				err = lcd_show_banner();
			}
			if (err >= 0)
				err = lcd_show_status(idle_now - last_idle,
						      now - last_jiffies);
			lcd_unlock();
			if (err < 0) {
				pr_warn("lcd offline");
				online = 0;
				next_retry = next_blink = get_jiffies();
				continue;
			}
			last_idle = idle_now;
			last_jiffies = now;
			next_update += HZ;
			continue;
		}

		now = get_jiffies();
		if (time_after_eq(now, next_blink)) {
			led_error_toggle();
			next_blink = now + LED_ERROR_HALF_PERIOD;
		}
		if (time_after_eq(now, next_retry)) {
			report_stack_usage();
			lcd_take();
			err = lcd_show_banner();
			lcd_unlock();
			if (err == 0) {
				pr_info("lcd online");
				online = 1;
				led_error_off();
				last_jiffies = get_jiffies();
				last_idle = sched_idle_ticks();
				next_update = last_jiffies + HZ;
				continue;
			}
			next_retry = get_jiffies() + LCD_RETRY_INTERVAL;
		}
		sleep_until(next_blink);
	}
}

/*
 * Without CONFIG_HD44780, task 0 keeps only the stack reports.  Its stack
 * stays the LCD task's size: at 128 and 144 bytes it overflowed on the
 * board, a printk() under an interrupt frame going past what a short run
 * had measured.
 *
 * 没有 CONFIG_HD44780 时, 任务 0 只做栈用量报告. 其栈仍取 LCD 任务的大小:
 * 128 与 144 字节时都曾在板上溢出, 原因是 printk() 叠加中断栈帧, 超出了
 * 短时间运行中测得的用量.
 */
static void stack_task(void)
{
	uint32_t next = get_jiffies();

	for (;;) {
		report_stack_usage();
		next += HZ;
		sleep_until(next);
	}
}

extern const initcall_t __initcall_start[], __initcall_end[];

/**
 * do_initcalls() - call every initcall, level 1 first
 *
 * Linux's do_initcalls() without the per-level loop: the linker script
 * has sorted the table by level.  A failure is logged with the low 16
 * bits of the function's address, to look up in leonix.map: the word
 * address on AVR, the offset into the 64 KiB of Flash on the STM32F103,
 * with bit 0 set for Thumb.  The full address would push the error code
 * past the LOG_TEXT - 1 characters of a record.
 *
 * 依次调用所有 initcall, 级别 1 最先. 与 Linux 的 do_initcalls() 相同,
 * 但没有按级别的循环: 链接脚本已按级别排好了表. 失败时以函数地址的低 16 位
 * 记入日志, 可在 leonix.map 中查找: 在 AVR 上即字地址, 在 STM32F103 上即在
 * 64 KiB Flash 中的偏移, Thumb 函数的第 0 位为 1. 完整的地址会把错误码挤出
 * 一条记录的 LOG_TEXT - 1 个字符.
 *
 * Context: main(), before sched_start().  由 main() 在 sched_start()
 * 之前调用.
 */
void do_initcalls(void)
{
	const initcall_t *call;
	initcall_t fn;
	int ret;

	for (call = __initcall_start; call < __initcall_end; call++) {
		fn = (initcall_t)pgm_read_ptr(call);
		ret = fn();
		if (ret < 0)
			pr_err("init %x %d", (uint16_t)(uintptr_t)fn, ret);
	}
}

int main(void)
{
	time_init();
	pr_info("Leonix booted");
	leds_init();
	do_initcalls();
	task_create(IS_ENABLED(CONFIG_HD44780) ? lcd_task : stack_task,
		    status_stack, sizeof(status_stack));
	if (IS_ENABLED(CONFIG_USB_CDC_ACM) && usb_init() < 0)
		pr_err("usb init fail");
	else if (init_start(init_stack, sizeof(init_stack)) < 0)
		pr_err("no init");
	sched_start();
}
