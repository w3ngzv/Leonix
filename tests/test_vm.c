// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host test of kernel/vm.c, run by "make check".  The programs in
 * tests/vm/ are assembled by tools/lxasm.py first; this runs them with a
 * file in memory and a write() that captures the output.
 */
#include <stdio.h>
#include <string.h>

#include <leonix/vm.h>

static uint8_t image[1024];
static uint16_t image_len;
static char out[64];
static uint8_t out_len;
static int fails;

static int16_t fetch(struct vm *vm, uint16_t off)
{
	(void)vm;
	return off < image_len ? image[off] : -1;
}

/* exit and write are all the test programs use. */
static uint8_t sys(struct vm *vm, uint8_t nr)
{
	int16_t fd, buf, count, b;
	int16_t i;

	switch (nr) {
	case 0:
		vm->status = vm_pop(vm);
		return VM_EXITED;
	case 2:
		count = vm_pop(vm);
		buf = vm_pop(vm);
		fd = vm_pop(vm);
		(void)fd;
		for (i = 0; i < count; i++) {
			b = vm_load_byte(vm, buf + i);
			if (b < 0)
				return VM_BAD_ADDRESS;
			if (out_len < sizeof(out))
				out[out_len++] = b;
		}
		vm_push(vm, count);
		return 0;
	}
	return VM_BAD_SYSCALL;
}

static const struct vm_ops ops = { fetch, sys };

static int load(const char *path)
{
	FILE *f = fopen(path, "rb");

	if (!f)
		return -1;
	image_len = fread(image, 1, sizeof(image), f);
	fclose(f);
	return 0;
}

static uint8_t run(const char *path, int argc, char *const argv[], struct vm *vm)
{
	memset(vm, 0, sizeof(*vm));
	vm->ops = &ops;
	out_len = 0;
	if (load(path) || vm_load(vm, image_len) || vm_args(vm, argc, argv))
		return 0xee;
	return vm_run(vm);
}

#define CHECK(cond) do { if (!(cond)) { \
	printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)

int main(void)
{
	static struct vm vm;
	char *argv[] = { "fault", NULL };
	uint8_t r;

	r = run("tests/vm/arith.lx", 1, argv, &vm);
	CHECK(r == VM_OK);
	CHECK(vm.status == 8);
	CHECK(out_len == 3 && !memcmp(out, "ok\n", 3));
	if (vm.status != 8)
		printf("arith failed check %d\n", vm.status);

	/* fault.s takes the fault number as argc - 1 */
	{
		char *a2[] = { "f", "x", NULL };
		char *a3[] = { "f", "x", "x", NULL };
		char *a4[] = { "f", "x", "x", "x", NULL };
		char *a5[] = { "f", "x", "x", "x", "x", NULL };

		CHECK(run("tests/vm/fault.lx", 2, a2, &vm) == VM_BAD_ADDRESS);
		CHECK(run("tests/vm/fault.lx", 3, a3, &vm) == VM_STACK);
		CHECK(run("tests/vm/fault.lx", 4, a4, &vm) == VM_DIVIDE);
		CHECK(run("tests/vm/fault.lx", 5, a5, &vm) == VM_BAD_ADDRESS);
	}

	/* a bad header is refused */
	memset(&vm, 0, sizeof(vm));
	vm.ops = &ops;
	memcpy(image, "XX\1\0\0\0", 6);
	image_len = 6;
	CHECK(vm_load(&vm, image_len) < 0);

	if (fails)
		return 1;
	printf("vm: all passed\n");
	return 0;
}
