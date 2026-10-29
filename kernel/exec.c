// SPDX-License-Identifier: GPL-2.0-only
/*
 * Running programs: exec() and the system calls of the virtual machine.
 *
 * A program runs in the task that called exec(), on that task's stack,
 * until it exits: fork(), execve() and wait() in one, since there are no
 * processes.  A program may exec() another, as the shell does, so the
 * machines form a stack of VM_PROCS; each one's code stays open as a
 * struct file outside the descriptor table and is fetched with peek().
 * All programs share the descriptors, as after fork() in Linux.
 *
 * 运行程序: exec() 与虚拟机的系统调用.
 *
 * 程序在调用 exec() 的任务中、在该任务的栈上运行, 直到退出. 由于没有进程,
 * exec() 同时承担 fork()、execve() 与 wait() 的作用. 程序可以 exec() 另一个程
 * 序, shell 即如此, 因此虚拟机构成至多 VM_PROCS 层的栈; 每个程序的代码作为描述
 * 符表之外的 struct file 保持打开, 以 peek() 取指. 所有程序共用描述符, 与
 * Linux 中 fork() 之后的情形相同.
 */

#include <leonix/errno.h>
#include <leonix/exec.h>
#include <leonix/lxfs.h>
#include <leonix/printk.h>
#include <leonix/string.h>
#include <leonix/syscall.h>
#include <leonix/tty.h>
#include <leonix/unistd.h>
#include <leonix/vm.h>

/*
 * VM_PROCS: the shell and the program it runs.  ARG_BUF holds all of
 * exec()'s argument strings.  write() copies from code memory COPY_CHUNK
 * bytes at a time.  A faulted program exits with FAULT_STATUS plus the
 * fault.
 *
 * VM_PROCS: shell 及其运行的程序. ARG_BUF 容纳 exec() 的全部参数字符串. write()
 * 从代码内存复制时每次复制 COPY_CHUNK 字节. 出错的程序以 FAULT_STATUS 加故障号
 * 退出.
 */
#define VM_PROCS	2
#define PATH_MAX	32
#define ARG_BUF		48
#define COPY_CHUNK	16
#define FAULT_STATUS	128

struct proc {
	/*
	 * first, so that a struct vm * is a struct proc *
	 * 放在首位, 使 struct vm * 也是 struct proc *
	 */
	struct vm vm;
	struct file code;
};

static struct proc procs[VM_PROCS];
static uint8_t depth;

/*
 * Scratch for paths and exec() arguments, static to keep it off the
 * stack, which holds a machine's frames for as long as the programs it
 * starts run.  Each is done with before the next program runs, since
 * vm_args() copies the arguments into the new machine.
 *
 * 路径与 exec() 参数的暂存区. 定义为 static, 以免占用栈: 一个虚拟机的栈帧在其
 * 启动的程序运行期间一直留在栈上. 下一个程序运行之前, 各暂存区都已用完, 因为
 * vm_args() 会把参数复制进新的虚拟机.
 */
static char path_buf[PATH_MAX];
static char arg_buf[ARG_BUF];
static const char *arg_ptrs[MAX_ARGS + 1];

static int16_t proc_fetch(struct vm *vm, uint16_t off)
{
	struct file *f = &((struct proc *)vm)->code;

	return f->f_op->peek(f, off);
}

/*
 * The data memory behind machine addresses @addr to @addr + @len, or NULL
 * if any of it is outside.
 *
 * 虚拟机地址 @addr 至 @addr + @len 对应的数据内存; 其中任何部分越界时返回 NULL.
 */
static uint8_t *data_ptr(struct vm *vm, uint16_t addr, uint16_t len)
{
	uint16_t off = addr - VM_DATA_BASE;

	if (addr < VM_DATA_BASE || off > vm->data_len || len > vm->data_len - off)
		return NULL;
	return vm->data + off;
}

/*
 * Copy a terminated string from the machine.  Returns 0 or -EFAULT.
 *
 * 从虚拟机复制一个带结束符的字符串. 成功时返回 0, 否则返回 -EFAULT.
 */
static int copy_string(struct vm *vm, uint16_t addr, char *buf, uint8_t size)
{
	int16_t c;
	uint8_t i;

	for (i = 0; i < size; i++) {
		c = vm_load_byte(vm, addr + i);
		if (c < 0)
			return -EFAULT;
		buf[i] = c;
		if (!c)
			return 0;
	}
	return -EFAULT;
}

/*
 * write() from anywhere in the machine; string constants are in code.
 * noinline, so that the copy buffer is not on the stack under every
 * other system call.
 *
 * 从虚拟机的任意地址 write(), 字符串常量位于代码区. 标为 noinline, 使复制缓冲区
 * 不出现在其他每个系统调用之下的栈上.
 */
static int __attribute__((noinline)) sys_write(struct vm *vm, int fd, uint16_t addr, uint16_t count)
{
	char chunk[COPY_CHUNK];
	uint8_t *p = data_ptr(vm, addr, count);
	uint16_t done = 0;
	uint8_t n, i;
	int16_t c;
	int r;

	if (p)
		return write(fd, (char *)p, count);
	while (done < count) {
		n = count - done < COPY_CHUNK ? count - done : COPY_CHUNK;
		for (i = 0; i < n; i++) {
			c = vm_load_byte(vm, addr + done + i);
			if (c < 0)
				return -EFAULT;
			chunk[i] = c;
		}
		r = write(fd, chunk, n);
		if (r < 0)
			return r;
		done += n;
	}
	return done;
}

/*
 * exec(argc, argv) from a program.  The strings are copied out of the
 * caller's memory first: the new machine lays them out in its own.
 *
 * 程序发出的 exec(argc, argv). 先把字符串从调用者的内存中复制出来, 新的虚拟机
 * 再把字符串布置到自己的内存中.
 */
static int sys_exec(struct vm *vm, int argc, uint16_t argv_addr)
{
	char *buf = arg_buf;
	const char **argv = arg_ptrs;
	uint8_t used = 0;
	int16_t lo, hi;
	int i;

	if (argc < 1 || argc > MAX_ARGS)
		return -E2BIG;
	for (i = 0; i < argc; i++) {
		lo = vm_load_byte(vm, argv_addr + i * 2);
		hi = vm_load_byte(vm, argv_addr + i * 2 + 1);
		if (lo < 0 || hi < 0)
			return -EFAULT;
		if (copy_string(vm, lo | hi << 8, buf + used, ARG_BUF - used))
			return -E2BIG;
		argv[i] = buf + used;
		used += strlen(buf + used) + 1;
	}
	argv[argc] = NULL;
	return exec(argc, argv);
}

static uint8_t vm_sys(struct vm *vm, uint8_t nr)
{
	char *path = path_buf;
	int16_t a, b, c;
	uint8_t *p;
	int r;

	switch (nr) {
	case SYS_exit:
		vm->status = vm_pop(vm);
		return VM_EXITED;
	case SYS_read:
	case SYS_readdir:
		c = vm_pop(vm);
		b = vm_pop(vm);
		a = vm_pop(vm);
		p = data_ptr(vm, b, c);
		if (!p || c < 0 || c > UINT8_MAX)
			r = -EFAULT;
		else if (nr == SYS_read)
			r = read(a, (char *)p, c);
		else
			r = readdir(a, (char *)p, c);
		break;
	case SYS_write:
		c = vm_pop(vm);
		b = vm_pop(vm);
		a = vm_pop(vm);
		r = c < 0 ? -EFAULT : sys_write(vm, a, b, c);
		break;
	case SYS_open:
		b = vm_pop(vm);
		a = vm_pop(vm);
		r = copy_string(vm, a, path, PATH_MAX);
		if (!r)
			r = open(path, b);
		break;
	case SYS_close:
		r = close(vm_pop(vm));
		break;
	case SYS_unlink:
		r = copy_string(vm, vm_pop(vm), path, PATH_MAX);
		if (!r)
			r = unlink(path);
		break;
	case SYS_exec:
		b = vm_pop(vm);
		a = vm_pop(vm);
		r = sys_exec(vm, a, b);
		break;
	case SYS_dup2:
		b = vm_pop(vm);
		a = vm_pop(vm);
		r = dup2(a, b);
		break;
	case SYS_statfs:
		r = lxfs_free();
		break;
	default:
		return VM_BAD_SYSCALL;
	}
	vm_push(vm, r);
	return vm->fault;
}

static const struct vm_ops proc_ops = {
	.fetch = proc_fetch,
	.sys = vm_sys,
};

/**
 * exec() - run a program and wait for it to exit
 * @argc: entries in @argv before the NULL
 * @argv: the program's name, then its arguments, then NULL
 *
 * A name without '/' is looked for in /bin.
 *
 * 运行程序 @argv[0] 并等待其退出, @argv 的其余各项是参数, 以 NULL 结尾. 不含
 * '/' 的名字在 /bin 中查找.
 *
 * Context: a task; the program runs on the task's stack.
 * 在任务中调用, 程序在该任务的栈上运行.
 * Return: the exit status, FAULT_STATUS plus the fault if the program
 * faulted, or a negative errno if it could not be started.
 * 返回程序的退出状态; 程序出错时返回 FAULT_STATUS 加故障号; 无法启动时返回负的
 * errno.
 */
int exec(int argc, const char *const argv[])
{
	char *path = path_buf;
	struct proc *p;
	uint8_t fault;
	int err;

	if (depth == VM_PROCS)
		return -ENOMEM;
	if (strchr(argv[0], '/')) {
		strlcpy(path, argv[0], PATH_MAX);
	} else {
		strcpy(path, "/bin/");
		strlcat(path, argv[0], PATH_MAX);
	}

	p = &procs[depth];
	err = vfs_open(&p->code, path, O_RDONLY);
	if (err < 0)
		return err;
	p->vm.ops = &proc_ops;
	if (!p->code.f_op->peek || vm_load(&p->vm, p->code.f_size)) {
		err = -ENOEXEC;
		goto out;
	}
	if (vm_args(&p->vm, argc, argv)) {
		err = -E2BIG;
		goto out;
	}

	/*
	 * TODO: the program runs on the caller's task and the caller waits,
	 * so there are no processes: no fork(), no wait(), nothing runs
	 * beside the shell, and pipes have nothing to connect.  See
	 * Documentation/notes/vm.rst.
	 *
	 * 待办: 程序在调用者的任务上运行, 调用者等待其结束, 因此没有进程: 没有
	 * fork() 与 wait(), 没有与 shell 并发的程序, 管道也无从连接. 见
	 * Documentation/notes/vm.rst.
	 */
	depth++;
	fault = vm_run(&p->vm);
	depth--;
	/*
	 * TODO: close the descriptors the program left open, as Linux does
	 * at exit; each one forgotten holds one of the MAX_FDS slots.
	 *
	 * 待办: 与 Linux 在进程退出时相同, 关闭程序未关闭的描述符; 每漏关一个就
	 * 占用 MAX_FDS 项中的一项.
	 */
	if (fault == VM_INTERRUPTED) {
		tty_intr_flush();
		err = FAULT_STATUS + fault;
	} else if (fault) {
		pr_info("vm fault %u", fault);
		err = FAULT_STATUS + fault;
	} else {
		err = p->vm.status;
	}
out:
	if (p->code.f_op->release)
		p->code.f_op->release(&p->code);
	return err;
}

/**
 * exec_interrupt() - stop the programs the shell runs, on Ctrl-C
 *
 * procs[0] is the program init runs, the shell, and is left running, as
 * a shell in Linux ignores SIGINT.  Every machine above it stops before
 * its next instruction, or when the system call it is in returns.
 *
 * 停止 shell 所运行的程序, 由 Ctrl-C 触发. procs[0] 是 init 运行的程序, 即
 * shell, 不被停止, 与 Linux 中 shell 忽略 SIGINT 相同. 其上的每台虚拟机在下
 * 一条指令之前停止, 或在所处的系统调用返回时停止.
 *
 * Context: any task, or an interrupt.  可在任意任务或中断中调用.
 */
void exec_interrupt(void)
{
	uint8_t i;

	for (i = 1; i < depth; i++)
		procs[i].vm.fault = VM_INTERRUPTED;
}
