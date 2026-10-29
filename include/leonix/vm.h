/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_VM_H
#define _LEONIX_VM_H

#include <stdint.h>

/*
 * The Leonix virtual machine, which runs every user program.
 *
 * A program is a file: a 6-byte header, then code.  The machine is a
 * 16-bit stack machine.  Its addresses below VM_DATA_BASE are the code,
 * read-only; from VM_DATA_BASE up they are the program's data memory,
 * read-write.  Every load and store is checked against those bounds,
 * and the only way out of the machine is the SYS instruction, which is
 * what keeps a program from the kernel's memory and the hardware.
 * Documentation/notes/vm.rst has the instruction set.
 *
 * Leonix 虚拟机, 运行所有用户程序.
 *
 * 程序是一个文件: 6 字节的文件头, 然后是代码. 该虚拟机是 16 位栈式机器. 低于
 * VM_DATA_BASE 的地址是代码, 只读; 自 VM_DATA_BASE 起是程序的数据内存, 可读写.
 * 每次读写都按这些边界检查, 离开虚拟机的唯一途径是 SYS 指令, 程序因此无法触及内
 * 核内存与硬件. 指令集见 Documentation/notes/vm.rst.
 */

#define VM_MAGIC0	'L'
#define VM_MAGIC1	'X'
#define VM_VERSION	1
/*
 * VM_HEADER: magic, version, flags, data size.  VM_DSTACK counts cells,
 * VM_RSTACK return addresses, VM_DATA_MAX bytes, arguments included.
 *
 * VM_HEADER: 魔数、版本、标志、数据大小. VM_DSTACK 以单元计, VM_RSTACK 以返回
 * 地址计, VM_DATA_MAX 以字节计, 含参数.
 */
#define VM_HEADER	6

#define VM_DATA_BASE	0x8000
#define VM_DSTACK	16
#define VM_RSTACK	8
#define VM_DATA_MAX	160

/*
 * opcodes; the immediate operand, if any, follows little-endian
 * 操作码; 立即数操作数 (若有) 以小端序紧随其后
 */
enum {
	OP_NOP = 0x00, OP_LIT8, OP_LIT16, OP_DUP, OP_DROP, OP_SWAP, OP_OVER,
	OP_ROT, OP_PICK,
	OP_ADD = 0x10, OP_SUB, OP_MUL, OP_DIVU, OP_MODU, OP_AND, OP_OR,
	OP_XOR, OP_SHL, OP_SHR, OP_NOT, OP_NEG,
	/* a b c -- a * b / c, unsigned, 32-bit product; 无符号, 32 位乘积 */
	OP_MULDIV,
	OP_EQ = 0x20, OP_LT, OP_ULT, OP_EQZ,
	OP_LDB = 0x30, OP_STB, OP_LDW, OP_STW,
	OP_JMP = 0x40, OP_JZ, OP_JNZ, OP_CALL, OP_RET, OP_TOR, OP_FROMR,
	OP_RFETCH,
	OP_SYS = 0x50,
};

/*
 * why a program stopped other than by exit()
 * 程序因 exit() 以外的原因停止时的原因
 */
enum vm_fault {
	VM_OK,
	VM_BAD_OPCODE,
	/* data or return stack over- or underflow; 数据栈或返回栈溢出 */
	VM_STACK,
	/* outside code, or a store into code; 越出代码区, 或写入代码区 */
	VM_BAD_ADDRESS,
	VM_DIVIDE,		/* by zero; 除以零 */
	VM_BAD_SYSCALL,
	/*
	 * Ctrl-C; set from outside the interpreter, see exec_interrupt()
	 * Ctrl-C; 由解释器之外设置, 见 exec_interrupt()
	 */
	VM_INTERRUPTED,
	VM_EXITED = 0xff,	/* from vm_ops.sys() only; 只由 sys() 返回 */
};

struct vm;

/*
 * Supplied by whoever runs the machine.  fetch() returns code byte @off,
 * or -1 past the end.  sys() carries out system call @nr, popping its
 * arguments and pushing its result with vm_pop() and vm_push(); it
 * returns 0 to go on, VM_EXITED once the program has exited, or a
 * vm_fault.
 *
 * 由运行虚拟机的一方提供. fetch() 返回代码字节 @off, 超出末尾时返回 -1. sys()
 * 执行系统调用 @nr, 以 vm_pop() 与 vm_push() 弹出参数并压入结果; 返回 0 表示继
 * 续, 程序已退出时返回 VM_EXITED, 否则返回 vm_fault 中的值.
 */
struct vm_ops {
	int16_t (*fetch)(struct vm *vm, uint16_t off);
	uint8_t (*sys)(struct vm *vm, uint8_t nr);
};

struct vm {
	const struct vm_ops *ops;
	void *priv;			/* the runner's own use; 运行方自用 */
	uint16_t code_len;
	uint16_t pc;
	uint8_t sp, rsp;
	/*
	 * volatile: another task or an interrupt may set it while vm_run()
	 * loops; 其他任务或中断可能在 vm_run() 循环期间设置该字段
	 */
	volatile uint8_t fault;
	int16_t status;			/* set by exit(); 由 exit() 设置 */
	uint16_t data_len;
	int16_t ds[VM_DSTACK];
	uint16_t rs[VM_RSTACK];
	uint8_t data[VM_DATA_MAX];
};

int vm_load(struct vm *vm, uint16_t file_len);
uint8_t vm_run(struct vm *vm);
int16_t vm_pop(struct vm *vm);
void vm_push(struct vm *vm, int16_t value);
int16_t vm_load_byte(struct vm *vm, uint16_t addr);
int vm_args(struct vm *vm, int argc, const char *const argv[]);

#endif /* _LEONIX_VM_H */
