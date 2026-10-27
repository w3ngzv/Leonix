// SPDX-License-Identifier: GPL-2.0-only
/*
 * The task that runs user space: /bin/sh on the console, started again
 * whenever it exits, as Linux runs a shell given init=/bin/sh.
 *
 * 运行用户空间的任务: 在控制台上运行 /bin/sh, 每次退出后重新启动, 与 Linux
 * 在 init=/bin/sh 下运行 shell 的方式相同.
 */
#include <leonix/pgmspace.h>
#include <stddef.h>
#include <string.h>

#include <leonix/exec.h>
#include <leonix/init.h>
#include <leonix/jiffies.h>
#include <leonix/printk.h>
#include <leonix/sched.h>
#include <leonix/unistd.h>

/*
 * pause before restarting sh, so that one failing at once does not spin
 * 重启 sh 之前的间隔, 使启动即失败的 sh 不会空转
 */
#define INIT_RETRY_MS	1000

static void init_task(void)
{
	static const char *const argv[] = { "sh", NULL };
	int status;

	for (;;) {
		status = exec(1, argv);
		printk("sh exited %d", status);
		msleep(INIT_RETRY_MS);
	}
}

/**
 * init_start() - open the console and start the task that runs sh
 * @stack: the task's stack
 * @size: bytes in @stack
 *
 * /dev/ttyACM0 becomes standard input and output.
 *
 * 打开 /dev/ttyACM0 作为标准输入与标准输出, 再以 @stack (@size 字节) 为栈
 * 创建运行 sh 的任务.
 *
 * Context: before sched_start(), after the USB driver has registered
 * the console.  在 sched_start() 之前、USB 驱动注册控制台之后调用.
 * Return: 0, or -1 if there is no console or no room for the task.
 * 成功时返回 0, 没有控制台或任务表已满时返回 -1.
 */
int init_start(uint8_t *stack, uint16_t size)
{
	char path[sizeof("/dev/ttyACM0")];

	strcpy_P(path, PSTR("/dev/ttyACM0"));
	if (open(path, O_RDWR) != STDIN_FILENO ||
	    dup2(STDIN_FILENO, STDOUT_FILENO) != STDOUT_FILENO)
		return -1;
	return task_create(init_task, stack, size);
}
