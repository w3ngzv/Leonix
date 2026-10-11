// SPDX-License-Identifier: GPL-2.0-only
/*
 * Running programs: exec() and the system calls of the virtual machine.
 *
 * A program runs in the task that called exec(), on that task's stack,
 * until it exits: fork(), execve() and wait() in one, since there are no
 * processes.  A program may exec() another, as the shell does, so the
 * machines are a stack of VM_PROCS; the code of each stays open as a
 * struct file outside the descriptor table and is fetched with peek().
 * Descriptors are shared by all programs, as after fork() in Linux.
 */
#include <string.h>

#include <leonix/errno.h>
#include <leonix/exec.h>
#include <leonix/lxfs.h>
#include <leonix/printk.h>
#include <leonix/syscall.h>
#include <leonix/unistd.h>
#include <leonix/vm.h>

#define VM_PROCS	2	/* the shell and the program it runs */
#define PATH_MAX	32
#define ARG_BUF		48	/* exec() argument strings, all together */
#define COPY_CHUNK	16	/* write() from code memory, a piece at a time */
#define FAULT_STATUS	128	/* exit status of a faulted program, plus fault */

struct proc {
	struct vm vm;		/* first, so that a struct vm * is a struct proc * */
	struct file code;
};

static struct proc procs[VM_PROCS];
static uint8_t depth;

/*
 * Scratch for paths and exec() arguments.  They are static to keep them
 * off the stack, which holds a machine's frames for as long as the
 * programs it starts run; each is done with before the next program
 * runs, since vm_args() copies the arguments into the new machine.
 */
static char path_buf[PATH_MAX];
static char arg_buf[ARG_BUF];
static char *arg_ptrs[MAX_ARGS + 1];

static int16_t proc_fetch(struct vm *vm, uint16_t off)
{
	struct file *f = &((struct proc *)vm)->code;

	return f->f_op->peek(f, off);
}

/*
 * The data memory behind machine addresses @addr to @addr + @len, or NULL
 * if any of it is outside.
 */
static uint8_t *data_ptr(struct vm *vm, uint16_t addr, uint16_t len)
{
	uint16_t off = addr - VM_DATA_BASE;

	if (addr < VM_DATA_BASE || off > vm->data_len || len > vm->data_len - off)
		return NULL;
	return vm->data + off;
}

/* Copy a terminated string from the machine.  Returns 0 or -EFAULT. */
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
 * Out of line, so that the copy buffer is not on the stack under every
 * other system call.
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
 */
static int sys_exec(struct vm *vm, int argc, uint16_t argv_addr)
{
	char *buf = arg_buf;
	char **argv = arg_ptrs;
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

/*
 * Run program @argv[0] with its arguments and return its exit status:
 * 128 plus the fault if it faulted, or a negative errno if it could not
 * be started.  A name without '/' is looked for in /bin.
 */
int exec(int argc, char **argv)
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

	depth++;
	fault = vm_run(&p->vm);
	depth--;
	if (fault) {
		printk("vm fault %u", fault);
		err = FAULT_STATUS + fault;
	} else {
		err = p->vm.status;
	}
out:
	if (p->code.f_op->release)
		p->code.f_op->release(&p->code);
	return err;
}
