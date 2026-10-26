// SPDX-License-Identifier: GPL-2.0-only
/*
 * The interpreter for user programs, see include/leonix/vm.h.
 *
 * Code is not copied into SRAM: every byte is fetched from the file
 * through vm_ops.fetch(), so a program costs the machine state and its
 * data memory alone.  The file has no AVR dependency, which lets
 * tests/test_vm.c run it on the host.
 *
 * 用户程序的解释器, 见 include/leonix/vm.h.
 *
 * 代码不复制进 SRAM: 每个字节都经由 vm_ops.fetch() 从文件中取出, 因此一个程序
 * 只占用虚拟机状态与其数据内存. 本文件不依赖 AVR, tests/test_vm.c 因此可以在主
 * 机上运行该解释器.
 */
#include <string.h>

#include <leonix/vm.h>

/**
 * vm_push() - push a cell onto the data stack
 * @vm: the machine
 * @value: the cell
 *
 * 把 @value 压入数据栈. 栈满时把 vm->fault 设为 VM_STACK.
 */
void vm_push(struct vm *vm, int16_t value)
{
	if (vm->sp == VM_DSTACK) {
		vm->fault = VM_STACK;
		return;
	}
	vm->ds[vm->sp++] = value;
}

/**
 * vm_pop() - pop a cell off the data stack
 * @vm: the machine
 *
 * 从数据栈弹出一个单元. 栈空时把 vm->fault 设为 VM_STACK.
 *
 * Return: the cell, or 0 with vm->fault set to VM_STACK.
 * 返回弹出的单元; 栈空时返回 0.
 */
int16_t vm_pop(struct vm *vm)
{
	if (!vm->sp) {
		vm->fault = VM_STACK;
		return 0;
	}
	return vm->ds[--vm->sp];
}

/**
 * vm_load_byte() - read a byte of code or data
 * @vm: the machine
 * @addr: machine address
 *
 * 读取虚拟机地址 @addr 处的代码或数据字节.
 *
 * Return: the byte, or -1 out of bounds.
 * 返回该字节; 越界时返回 -1.
 */
int16_t vm_load_byte(struct vm *vm, uint16_t addr)
{
	if (addr >= VM_DATA_BASE) {
		addr -= VM_DATA_BASE;
		return addr < vm->data_len ? vm->data[addr] : -1;
	}
	if (addr >= vm->code_len)
		return -1;
	return vm->ops->fetch(vm, VM_HEADER + addr);
}

/*
 * store @value at @addr: 0, or -1 outside the data memory
 * 把 @value 存到 @addr: 成功时返回 0, 地址在数据内存之外时返回 -1
 */
static int vm_store_byte(struct vm *vm, uint16_t addr, uint8_t value)
{
	if (addr < VM_DATA_BASE || addr - VM_DATA_BASE >= vm->data_len)
		return -1;
	vm->data[addr - VM_DATA_BASE] = value;
	return 0;
}

/*
 * the next code byte, faulting past the end of the code
 * 下一个代码字节; 越过代码末尾时置故障
 */
static uint8_t next(struct vm *vm)
{
	int16_t b = vm->pc < vm->code_len ?
		    vm->ops->fetch(vm, VM_HEADER + vm->pc) : -1;

	if (b < 0) {
		vm->fault = VM_BAD_ADDRESS;
		return OP_NOP;
	}
	vm->pc++;
	return b;
}

static uint16_t next16(struct vm *vm)
{
	uint8_t lo = next(vm);

	return lo | (uint16_t)next(vm) << 8;
}

static int16_t load_word(struct vm *vm, uint16_t addr)
{
	int16_t lo = vm_load_byte(vm, addr);
	int16_t hi = vm_load_byte(vm, addr + 1);

	if (lo < 0 || hi < 0) {
		vm->fault = VM_BAD_ADDRESS;
		return 0;
	}
	return lo | hi << 8;
}

static void store(struct vm *vm, uint16_t addr, uint16_t value, uint8_t len)
{
	if (vm_store_byte(vm, addr, value) < 0 ||
	    (len == 2 && vm_store_byte(vm, addr + 1, value >> 8) < 0))
		vm->fault = VM_BAD_ADDRESS;
}

static void rpush(struct vm *vm, uint16_t value)
{
	if (vm->rsp == VM_RSTACK)
		vm->fault = VM_STACK;
	else
		vm->rs[vm->rsp++] = value;
}

static uint16_t rpop(struct vm *vm)
{
	if (!vm->rsp) {
		vm->fault = VM_STACK;
		return 0;
	}
	return vm->rs[--vm->rsp];
}

/**
 * vm_load() - check a program's header and set the machine up to run it
 * @vm: the machine, with vm->ops set
 * @file_len: bytes in the program's file
 *
 * 检查长度为 @file_len 字节的程序的文件头, 并使虚拟机从程序的第一条指令开始运
 * 行.
 *
 * Return: 0, or -1 for a file that is not a program or wants more data
 * memory than there is.
 * 成功时返回 0; 文件不是程序或所需数据内存超出容量时返回 -1.
 */
int vm_load(struct vm *vm, uint16_t file_len)
{
	uint8_t hdr[VM_HEADER];
	uint8_t i;
	int16_t b;

	if (file_len < VM_HEADER)
		return -1;
	for (i = 0; i < VM_HEADER; i++) {
		b = vm->ops->fetch(vm, i);
		if (b < 0)
			return -1;
		hdr[i] = b;
	}
	vm->data_len = hdr[4] | (uint16_t)hdr[5] << 8;
	if (hdr[0] != VM_MAGIC0 || hdr[1] != VM_MAGIC1 ||
	    hdr[2] != VM_VERSION || vm->data_len > VM_DATA_MAX)
		return -1;
	vm->code_len = file_len - VM_HEADER;
	vm->pc = 0;
	vm->sp = vm->rsp = 0;
	vm->fault = VM_OK;
	vm->status = 0;
	memset(vm->data, 0, sizeof(vm->data));
	return 0;
}

/**
 * vm_args() - give a loaded program its arguments
 * @vm: the machine, after vm_load()
 * @argc: entries in @argv before the NULL
 * @argv: the strings
 *
 * The strings and an argv array go at the end of the data memory, as
 * Linux lays them on a new process's stack; argc and argv are pushed for
 * the program's first instruction.
 *
 * 把参数字符串与 argv 数组复制到数据内存末尾, 与 Linux 把参数布置在新进程栈上的
 * 做法相同; 再压入 argc 与 argv, 供程序的第一条指令使用.
 *
 * Return: 0, or -1 if they do not fit.
 * 成功时返回 0, 放不下时返回 -1.
 */
int vm_args(struct vm *vm, int argc, const char *const argv[])
{
	uint16_t table = vm->data_len;
	uint16_t strings = table + (argc + 1) * 2;
	uint16_t addr;
	size_t len;
	int i;

	if (strings > VM_DATA_MAX)
		return -1;
	vm->data_len = VM_DATA_MAX;
	for (i = 0; i < argc; i++) {
		len = strlen(argv[i]) + 1;
		if (strings + len > VM_DATA_MAX)
			return -1;
		memcpy(vm->data + strings, argv[i], len);
		addr = VM_DATA_BASE + strings;
		vm->data[table + i * 2] = addr;
		vm->data[table + i * 2 + 1] = addr >> 8;
		strings += len;
	}
	vm->data[table + argc * 2] = 0;
	vm->data[table + argc * 2 + 1] = 0;
	vm_push(vm, argc);
	vm_push(vm, VM_DATA_BASE + table);
	return 0;
}

/**
 * vm_run() - run a program until it exits or faults
 * @vm: the machine, after vm_load() and vm_args()
 *
 * 运行程序, 直到程序退出或出错.
 *
 * Return: VM_OK after exit(), with the status in vm->status, or the
 * fault.
 * 程序调用 exit() 后返回 VM_OK, 退出状态在 vm->status 中; 出错时返回故障号.
 */
uint8_t vm_run(struct vm *vm)
{
	int16_t a, b;
	uint16_t target;
	uint8_t op, r;

	while (!vm->fault) {
		op = next(vm);
		switch (op) {
		case OP_NOP:
			break;
		case OP_LIT8:
			vm_push(vm, (int8_t)next(vm));
			break;
		case OP_LIT16:
			vm_push(vm, next16(vm));
			break;
		case OP_DUP:
			a = vm_pop(vm);
			vm_push(vm, a);
			vm_push(vm, a);
			break;
		case OP_DROP:
			vm_pop(vm);
			break;
		case OP_SWAP:
			b = vm_pop(vm);
			a = vm_pop(vm);
			vm_push(vm, b);
			vm_push(vm, a);
			break;
		case OP_OVER:
			b = vm_pop(vm);
			a = vm_pop(vm);
			vm_push(vm, a);
			vm_push(vm, b);
			vm_push(vm, a);
			break;
		case OP_ROT:			/* a b c -- b c a */
			b = vm_pop(vm);
			a = vm_pop(vm);
			target = vm_pop(vm);
			vm_push(vm, a);
			vm_push(vm, b);
			vm_push(vm, target);
			break;
		case OP_PICK:			/* 0 pick is dup; 等同 dup */
			r = next(vm);
			if (r >= vm->sp)
				vm->fault = VM_STACK;
			else
				vm_push(vm, vm->ds[vm->sp - 1 - r]);
			break;
		case OP_ADD ... OP_SHR:
		case OP_EQ ... OP_ULT:
			b = vm_pop(vm);
			a = vm_pop(vm);
			switch (op) {
			case OP_ADD:
				a += b;
				break;
			case OP_SUB:
				a -= b;
				break;
			case OP_MUL:
				a *= b;
				break;
			case OP_DIVU:
			case OP_MODU:
				if (!b) {
					vm->fault = VM_DIVIDE;
					break;
				}
				a = op == OP_DIVU ? (uint16_t)a / (uint16_t)b
						  : (uint16_t)a % (uint16_t)b;
				break;
			case OP_AND:
				a &= b;
				break;
			case OP_OR:
				a |= b;
				break;
			case OP_XOR:
				a ^= b;
				break;
			case OP_SHL:
				a = (uint16_t)a << (b & 15);
				break;
			case OP_SHR:
				a = (uint16_t)a >> (b & 15);
				break;
			case OP_EQ:
				a = a == b;
				break;
			case OP_LT:
				a = a < b;
				break;
			case OP_ULT:
				a = (uint16_t)a < (uint16_t)b;
				break;
			}
			vm_push(vm, a);
			break;
		/* Forth's star-slash; 即 Forth 的 star-slash 运算 */
		case OP_MULDIV:
			target = vm_pop(vm);
			b = vm_pop(vm);
			a = vm_pop(vm);
			if (!target)
				vm->fault = VM_DIVIDE;
			else
				vm_push(vm, (uint32_t)(uint16_t)a * (uint16_t)b /
					    target);
			break;
		case OP_NOT:
			vm_push(vm, ~vm_pop(vm));
			break;
		case OP_NEG:
			vm_push(vm, -vm_pop(vm));
			break;
		case OP_EQZ:
			vm_push(vm, !vm_pop(vm));
			break;
		case OP_LDB:
			a = vm_load_byte(vm, vm_pop(vm));
			if (a < 0)
				vm->fault = VM_BAD_ADDRESS;
			vm_push(vm, a);
			break;
		case OP_LDW:
			vm_push(vm, load_word(vm, vm_pop(vm)));
			break;
		case OP_STB:			/* value addr -- */
		case OP_STW:
			target = vm_pop(vm);
			a = vm_pop(vm);
			store(vm, target, a, op == OP_STW ? 2 : 1);
			break;
		case OP_JMP:
			vm->pc = next16(vm);
			break;
		case OP_JZ:
		case OP_JNZ:
			target = next16(vm);
			a = vm_pop(vm);
			if (!a == (op == OP_JZ))
				vm->pc = target;
			break;
		case OP_CALL:
			target = next16(vm);
			rpush(vm, vm->pc);
			vm->pc = target;
			break;
		case OP_RET:
			vm->pc = rpop(vm);
			break;
		case OP_TOR:
			rpush(vm, vm_pop(vm));
			break;
		case OP_FROMR:
			vm_push(vm, rpop(vm));
			break;
		case OP_RFETCH:
			if (!vm->rsp)
				vm->fault = VM_STACK;
			else
				vm_push(vm, vm->rs[vm->rsp - 1]);
			break;
		case OP_SYS:
			r = next(vm);
			if (vm->fault)
				break;
			r = vm->ops->sys(vm, r);
			if (r == VM_EXITED)
				return VM_OK;
			if (r)
				vm->fault = r;
			break;
		default:
			vm->fault = VM_BAD_OPCODE;
			break;
		}
	}
	return vm->fault;
}
