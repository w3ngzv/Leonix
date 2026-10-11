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
 */

#define VM_MAGIC0	'L'
#define VM_MAGIC1	'X'
#define VM_VERSION	1
#define VM_HEADER	6	/* magic, version, flags, data size */

#define VM_DATA_BASE	0x8000
#define VM_DSTACK	16	/* cells */
#define VM_RSTACK	8	/* return addresses */
#define VM_DATA_MAX	160	/* bytes, arguments included */

/* Opcodes; the immediate operand, if any, follows little-endian. */
enum {
	OP_NOP = 0x00, OP_LIT8, OP_LIT16, OP_DUP, OP_DROP, OP_SWAP, OP_OVER,
	OP_ROT, OP_PICK,
	OP_ADD = 0x10, OP_SUB, OP_MUL, OP_DIVU, OP_MODU, OP_AND, OP_OR,
	OP_XOR, OP_SHL, OP_SHR, OP_NOT, OP_NEG,
	OP_MULDIV,		/* a b c -- a * b / c, unsigned, 32-bit product */
	OP_EQ = 0x20, OP_LT, OP_ULT, OP_EQZ,
	OP_LDB = 0x30, OP_STB, OP_LDW, OP_STW,
	OP_JMP = 0x40, OP_JZ, OP_JNZ, OP_CALL, OP_RET, OP_TOR, OP_FROMR,
	OP_RFETCH,
	OP_SYS = 0x50,
};

/* Why a program stopped other than by exit(). */
enum vm_fault {
	VM_OK,
	VM_BAD_OPCODE,
	VM_STACK,		/* data or return stack over- or underflow */
	VM_BAD_ADDRESS,		/* outside code, or a store into code */
	VM_DIVIDE,		/* by zero */
	VM_BAD_SYSCALL,
	VM_EXITED = 0xff,	/* from vm_ops.sys() only */
};

struct vm;

/*
 * Supplied by whoever runs the machine.  fetch() returns code byte @off,
 * or -1 past the end.  sys() carries out system call @nr, popping its
 * arguments and pushing its result with vm_pop() and vm_push(); it
 * returns 0 to go on, VM_EXITED once the program has exited, or a
 * vm_fault.
 */
struct vm_ops {
	int16_t (*fetch)(struct vm *vm, uint16_t off);
	uint8_t (*sys)(struct vm *vm, uint8_t nr);
};

struct vm {
	const struct vm_ops *ops;
	void *priv;			/* the runner's own use */
	uint16_t code_len;
	uint16_t pc;
	uint8_t sp, rsp;
	uint8_t fault;
	int16_t status;			/* set by exit() */
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
int vm_args(struct vm *vm, int argc, char *const argv[]);

#endif /* _LEONIX_VM_H */
