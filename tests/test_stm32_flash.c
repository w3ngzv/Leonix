// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host test of drivers/stm32_flash.c, the journal that keeps lxfs on
 * the STM32F103's Flash, run by "make check".  QEMU's stm32vldiscovery
 * ignores Flash writes, so this is where the driver's writes and its
 * mount are exercised at all.
 *
 * The 8 KiB lxfs area is an array.  The registers come from
 * tests/fake_stm32/asm/stm32f1.h, and each read of FLASH_SR carries out
 * the half-word program or the page erase the driver has started, with
 * the rules of PM0075 2.3.3: a half-word that is not 0xffff takes only
 * 0x0000, otherwise PGERR and nothing changes.
 *
 * A reset is simulated by letting only the first n programs and erases
 * of an operation complete, for every n until the operation completes,
 * and mounting again.  The operation in flight at the reset either has
 * no effect, or is left half done: a half-word with only its low byte
 * programmed, a page with only its first half erased.
 *
 * drivers/stm32_flash.c 的主机测试, 由 "make check" 运行. 该驱动以日志的方式
 * 把 lxfs 保存在 STM32F103 的 Flash 上. QEMU 的 stm32vldiscovery 忽略 Flash
 * 写入, 驱动的写入与挂载只在本测试中得到执行.
 *
 * 8 KiB 的 lxfs 区域是一个数组. 寄存器取自 tests/fake_stm32/asm/stm32f1.h,
 * 每次读取 FLASH_SR 时执行驱动已启动的半字编程或页擦除, 规则与 PM0075 2.3.3
 * 相同: 值不为 0xffff 的半字只能写入 0x0000, 否则置位 PGERR 且内容不变.
 *
 * 模拟复位的办法是: 对每个 n, 只让某个操作的前 n 次编程与擦除完成, 直到该操作
 * 完成, 然后重新挂载. 复位时正在进行的那一次编程或擦除, 要么没有效果, 要么只
 * 完成一半: 半字只编程了低字节, 页只擦除了前半.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <leonix/fs.h>
#include <leonix/init.h>

/*
 * The driver's fs_initcall() puts a pointer in a section of an ELF
 * name, which a Mach-O host rejects; the test calls flash_fs_init()
 * itself.
 *
 * 驱动的 fs_initcall() 把指针放入一个 ELF 名称的段, Mach-O 主机不接受该名称;
 * 测试自行调用 flash_fs_init().
 */
#undef __define_initcall
#define __define_initcall(fn, id)					\
	static const initcall_t __initcall_##fn##id __attribute__((unused)) = fn

#include "../drivers/stm32_flash.c"

#define FAKE_PAGES	(FAKE_FLASH_BYTES / PAGE_BYTES)
#define FILE_BYTES	40
#define CRASH_OPS	24
#define ROTATE_OPS	400

uint8_t fake_flash[FAKE_FLASH_BYTES];
uint32_t fake_flash_keyr, fake_flash_cr = FLASH_CR_LOCK, fake_flash_ar;

/*
 * what the Flash holds, to tell which half-word the driver has stored
 * Flash 中的实际内容, 用于找出驱动写入的是哪个半字
 */
static uint8_t shadow[FAKE_FLASH_BYTES];

static unsigned long programs, erases[FAKE_PAGES];
static int fails;
static char last_msg[64];

/*
 * programs and erases that may still complete before the simulated
 * reset; -1: no reset
 *
 * 模拟复位之前仍可完成的编程与擦除次数; -1 表示不复位
 */
static long budget = -1;
static int torn_half_done;
static jmp_buf reset_point;

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		printf("FAIL line %d: %s\n", __LINE__, #cond);		\
		fails++;						\
	}								\
} while (0)

void printk_P(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(last_msg, sizeof(last_msg), fmt, ap);
	va_end(ap);
}

static uint16_t half_at(const uint8_t *p, unsigned int off)
{
	return p[off] | p[off + 1] << 8;
}

static void half_set(uint8_t *p, unsigned int off, uint16_t v)
{
	p[off] = v & 0xff;
	p[off + 1] = v >> 8;
}

/*
 * one program or erase is about to complete; 1 if the reset comes first
 * 一次编程或擦除即将完成; 若复位先到达则返回 1
 */
static int reset_now(void)
{
	if (budget == 0)
		return 1;
	if (budget > 0)
		budget--;
	return 0;
}

static void fake_erase(void)
{
	unsigned int page = (uint32_t)(fake_flash_ar -
				       (uint32_t)(uintptr_t)fake_flash) / PAGE_BYTES;
	uint8_t *p = fake_flash + page * PAGE_BYTES;

	CHECK(page < FAKE_PAGES);
	if (page >= FAKE_PAGES)
		return;
	if (reset_now()) {
		if (torn_half_done)
			memset(p, 0xff, PAGE_BYTES / 2);
		memcpy(shadow, fake_flash, sizeof(shadow));
		longjmp(reset_point, 1);
	}
	memset(p, 0xff, PAGE_BYTES);
	memcpy(shadow + page * PAGE_BYTES, p, PAGE_BYTES);
	erases[page]++;
}

static void fake_program(void)
{
	unsigned int off, found = 0, at = 0;
	uint16_t old, stored;

	for (off = 0; off < FAKE_FLASH_BYTES; off += 2) {
		if (half_at(fake_flash, off) != half_at(shadow, off)) {
			found++;
			at = off;
		}
	}
	CHECK(found == 1);
	if (found != 1)
		return;
	old = half_at(shadow, at);
	stored = half_at(fake_flash, at);
	if (reset_now()) {
		half_set(fake_flash, at, torn_half_done ? old & (stored | 0xff00) : old);
		memcpy(shadow, fake_flash, sizeof(shadow));
		longjmp(reset_point, 1);
	}
	if (old != ERASED_HALF && stored != 0)
		stored = old;
	half_set(fake_flash, at, stored);
	half_set(shadow, at, stored);
	programs++;
}

uint32_t *fake_flash_sr(void)
{
	static uint32_t sr;

	if (fake_flash_keyr == FLASH_KEY2) {
		fake_flash_cr &= ~FLASH_CR_LOCK;
		fake_flash_keyr = 0;
	}
	if (fake_flash_cr & (FLASH_CR_PG | FLASH_CR_PER))
		CHECK(!(fake_flash_cr & FLASH_CR_LOCK));
	if ((fake_flash_cr & FLASH_CR_PER) && (fake_flash_cr & FLASH_CR_STRT)) {
		fake_erase();
		fake_flash_cr &= ~FLASH_CR_STRT;
	} else if (fake_flash_cr & FLASH_CR_PG) {
		fake_program();
	} else {
		CHECK(!memcmp(fake_flash, shadow, sizeof(shadow)));
	}
	sr = 0;
	return &sr;
}

/*
 * mount after a reset; the message flash_fs_init() logged, "" if none
 * 复位后挂载; 返回 flash_fs_init() 记录的消息, 没有消息时为 ""
 */
static const char *remount(void)
{
	last_msg[0] = '\0';
	fake_flash_cr = FLASH_CR_LOCK;
	CHECK(flash_fs_init() == 0);
	return last_msg;
}

static void blank_flash(void)
{
	memset(fake_flash, 0xff, sizeof(fake_flash));
	memcpy(shadow, fake_flash, sizeof(shadow));
}

static int file_get(const char *name, char *buf)
{
	struct file f;
	int n = 0, i;

	memset(&f, 0, sizeof(f));
	if (lxfs_open(&f, name, O_RDONLY) < 0)
		return -1;
	while ((i = f.f_op->read(&f, buf + n, 64)) > 0)
		n += i;
	return n;
}

static void file_put(const char *name, const char *buf, int len)
{
	struct file f;

	memset(&f, 0, sizeof(f));
	f.f_flags = O_WRONLY | O_CREAT | O_TRUNC;
	if (lxfs_open(&f, name, f.f_flags) == 0)
		f.f_op->write(&f, buf, len);
}

/*
 * the contents /a has after @k operations
 * 经过 @k 次操作后 /a 的内容
 */
static void version(char *buf, int k)
{
	int i;

	for (i = 0; i < FILE_BYTES; i++)
		buf[i] = 'a' + (k + i) % 26;
}

static int is_version(const char *buf, int len, int k)
{
	char want[FILE_BYTES];

	version(want, k);
	return len == FILE_BYTES && !memcmp(buf, want, FILE_BYTES);
}

static void op_rewrite(int k)
{
	char buf[FILE_BYTES];

	version(buf, k);
	file_put("/a", buf, FILE_BYTES);
}

static void setup(int ops)
{
	int k;

	blank_flash();
	CHECK(!strcmp(remount(), "mkfs lxfs"));
	file_put("/b", "bbbbbbbbbbbbbbbbbbbb", 20);
	for (k = 0; k <= ops; k++)
		op_rewrite(k);
}

/*
 * Rewrite /a many times, through every slot several times over, with a
 * remount now and then; every page is erased about as often as the
 * others.
 *
 * 多次重写 /a, 多次轮转所有槽, 其间不时重新挂载; 每一页的擦除次数大致相同.
 */
static void rotate_test(void)
{
	char buf[FILE_BYTES];
	unsigned long lo = ~0ul, hi = 0;
	uint32_t first_gen;
	unsigned int page;
	int k, n;

	memset(erases, 0, sizeof(erases));
	setup(0);
	first_gen = gen;
	for (k = 1; k <= ROTATE_OPS; k++) {
		op_rewrite(k);
		if (k % 7)
			continue;
		CHECK(!strcmp(remount(), ""));
		n = file_get("/a", buf);
		CHECK(is_version(buf, n, k));
	}
	CHECK(flash_ok);
	CHECK(gen > first_gen + 2 * nr_slots);
	for (page = 0; page < FAKE_PAGES; page++) {
		lo = erases[page] < lo ? erases[page] : lo;
		hi = erases[page] > hi ? erases[page] : hi;
	}
	CHECK(hi - lo <= 1);
	printf("stm32_flash: %d rewrites, %lu..%lu erases per page\n",
	       ROTATE_OPS, lo, hi);
}

/*
 * run op_rewrite(@k) with a reset after @n programs and erases; 1 if it
 * finished
 *
 * 运行 op_rewrite(@k), 在 @n 次编程与擦除之后复位; 操作完成时返回 1
 */
static int run_until_reset(int k, long n)
{
	/*
	 * not a local, which longjmp may lose
	 * 不用局部变量, longjmp 可能丢失其值
	 */
	static int pending;

	pending = k;
	budget = n;
	if (setjmp(reset_point)) {
		budget = -1;
		return 0;
	}
	op_rewrite(pending);
	budget = -1;
	return 1;
}

/*
 * After @ops rewrites, rewrite /a once more with a reset at every
 * possible point.  After the remount /a holds the version before or
 * after, or is empty from the truncation, and /b is untouched.  The
 * range of @ops takes the rewrite across several changes of slot.
 *
 * 重写 @ops 次之后, 再重写一次 /a, 在每个可能的位置复位. 重新挂载后, /a 是
 * 操作之前或之后的版本, 或因截断而为空, /b 不受影响. @ops 的取值范围使该次
 * 重写跨过数次换槽.
 */
static void crash_test(int ops)
{
	char buf[FILE_BYTES * 2];
	const char *msg;
	int done = 0, n;
	long resets;

	for (resets = 0; !done; resets++) {
		setup(ops);
		done = run_until_reset(ops + 1, resets);
		msg = remount();
		CHECK(!strcmp(msg, "") || !strcmp(msg, "lxfs repaired"));
		CHECK(flash_ok);
		n = file_get("/a", buf);
		CHECK(n == 0 || is_version(buf, n, ops) ||
		      is_version(buf, n, ops + 1));
		if (done)
			CHECK(is_version(buf, n, ops + 1));
		CHECK(file_get("/b", buf) == 20 && buf[19] == 'b');
		CHECK(lxfs_init(flash_dev_read, flash_dev_write) == LXFS_CLEAN);
	}
}

int main(void)
{
	char buf[FILE_BYTES];
	int ops;

	/*
	 * a blank part is formatted, and the files survive a remount
	 * 空白芯片被格式化, 文件在重新挂载后仍在
	 */
	blank_flash();
	CHECK(!strcmp(remount(), "mkfs lxfs"));
	op_rewrite(3);
	CHECK(!strcmp(remount(), ""));
	CHECK(is_version(buf, file_get("/a", buf), 3));

	/*
	 * an area of something other than lxfs is taken as blank
	 * 内容不属于 lxfs 的区域视为空白
	 */
	memset(fake_flash, 0x5a, sizeof(fake_flash));
	memcpy(shadow, fake_flash, sizeof(shadow));
	CHECK(!strcmp(remount(), "mkfs lxfs"));
	CHECK(file_get("/a", buf) == -1);
	CHECK(!strcmp(remount(), ""));

	rotate_test();

	for (torn_half_done = 0; torn_half_done <= 1; torn_half_done++)
		for (ops = 0; ops < CRASH_OPS; ops++)
			crash_test(ops);

	if (fails) {
		printf("stm32_flash: %d failed\n", fails);
		return 1;
	}
	printf("stm32_flash: all passed, %lu half-words programmed\n", programs);
	return 0;
}
