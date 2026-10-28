// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host test of fs/lxfs.c, run by "make check".  The EEPROM is a 1 KiB
 * array that starts out 0xff, as a blank part does, and every byte the
 * filesystem writes is counted, so the test also catches a change that
 * writes more of the EEPROM than it used to.
 *
 * A reset is simulated by letting only the first n changed bytes of an
 * operation reach the array, for every n until the operation completes,
 * and mounting again.
 *
 * fs/lxfs.c 的主机测试, 由 "make check" 运行. EEPROM 是一个初值为 0xff 的 1 KiB
 * 数组, 与空白芯片相同; 文件系统写入的每个字节都被计数, 因此测试也能发现写入
 * EEPROM 字节数比以前增多的改动.
 *
 * 模拟复位的办法是: 对每个 n, 只让某个操作的前 n 个变化字节到达数组, 直到该操作
 * 完成, 然后重新挂载.
 */
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include <leonix/errno.h>
#include <leonix/lxfs.h>

#define EEPROM_SIZE	1024

/*
 * the layout from the top of fs/lxfs.c, for building damaged images
 * 与 fs/lxfs.c 开头的布局相同, 用于构造损坏的映像
 */
#define FAT_OFF		8
#define DIR_OFF		40
#define DIRENT_SIZE	15
#define FILES		16
#define DATA_BYTES	672
#define FIRST_DATA_BLOCK	11
#define DATA_BLOCKS	21
#define BLOCK		32
#define VERSION_OFF	4

static uint8_t eeprom[EEPROM_SIZE];
static unsigned long writes;
static int fails;

/*
 * bytes that may still change before the simulated reset; -1: no reset
 * 模拟复位之前仍可变化的字节数; -1 表示不复位
 */
static long budget = -1;
static jmp_buf reset_point;
/*
 * leave the byte a reset interrupts erased to 0xff, as an EEPROM write
 * erases the byte before programming it
 * 使复位所打断的字节停在擦除后的 0xff; EEPROM 写入一个字节时先擦除再编程
 */
static int erase_interrupted;

static void ee_read(uint16_t off, void *buf, uint16_t len)
{
	memcpy(buf, eeprom + off, len);
}

/*
 * as avr-libc's eeprom_update_block(): unchanged bytes are not written
 * 与 avr-libc 的 eeprom_update_block() 相同: 不写入未变化的字节
 */
static void ee_write(uint16_t off, const void *buf, uint16_t len)
{
	const uint8_t *p = buf;
	uint16_t i;

	for (i = 0; i < len; i++) {
		if (eeprom[off + i] != p[i]) {
			if (budget == 0) {
				if (erase_interrupted)
					eeprom[off + i] = 0xff;
				longjmp(reset_point, 1);
			}
			if (budget > 0)
				budget--;
			eeprom[off + i] = p[i];
			writes++;
		}
	}
}

/*
 * On the ATmega2560 of "make check-qemu" the text of each condition
 * stays in flash: copied to SRAM at reset, as plain strings are, it
 * leaves too little for the stack.
 *
 * 在 "make check-qemu" 所用的 ATmega2560 上, 各条件的文本留在 flash 中: 若像
 * 普通字符串那样在复位时复制到 SRAM, 留给栈的空间不够.
 */
#ifdef __AVR__
#include <avr/pgmspace.h>
#define FAIL_PRINT(cond) \
	printf_P(PSTR("FAIL line %d: %S\n"), __LINE__, PSTR(#cond))
#else
#define FAIL_PRINT(cond) printf("FAIL line %d: %s\n", __LINE__, #cond)
#endif

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		FAIL_PRINT(cond);					\
		fails++;						\
	}								\
} while (0)

static int fs_open(struct file *f, const char *name, uint16_t flags)
{
	memset(f, 0, sizeof(*f));
	f->f_flags = flags;
	return lxfs_open(f, name, flags);
}

static int count_files(void)
{
	struct file d;
	char name[NAME_MAX + 1];
	int n = 0;

	fs_open(&d, "/", O_RDONLY);
	while (d.f_op->readdir(&d, name, sizeof(name)) > 0)
		n++;
	return n;
}

/*
 * the contents of @name into @buf, and its size, or -1 if there is none
 * 把 @name 的内容读入 @buf, 返回其大小; 文件不存在时返回 -1
 */
static int file_get(const char *name, char *buf)
{
	struct file f;
	int n = 0, i;

	if (fs_open(&f, name, O_RDONLY) < 0)
		return -1;
	while ((i = f.f_op->read(&f, buf + n, 64)) > 0)
		n += i;
	return n;
}

static int file_put(const char *name, uint16_t flags, const char *buf, int len)
{
	struct file f;
	int i, n = 0;

	if (fs_open(&f, name, O_WRONLY | flags) < 0)
		return -1;
	while (n < len && (i = f.f_op->write(&f, buf + n,
					      len - n > 200 ? 200 : len - n)) > 0)
		n += i;
	return n;
}

/*
 * every data block belongs to a file or is free, and a remount finds nothing
 * 每个数据块或属于某个文件或空闲, 且重新挂载时没有发现问题
 */
static void check_blocks(void)
{
	struct file d;
	char name[NAME_MAX + 2] = "/", buf[DATA_BYTES];
	int used = 0, n;

	CHECK(lxfs_init(ee_read, ee_write) == LXFS_CLEAN);
	fs_open(&d, "/", O_RDONLY);
	while (d.f_op->readdir(&d, name + 1, sizeof(name) - 1) > 0) {
		n = file_get(name, buf);
		used += (n + 31) / 32 * 32;
	}
	CHECK(used + lxfs_free() == DATA_BYTES);
}

static char old_a[300], new_a[300];
static int old_len, new_len;

/*
 * rewrites of /d before the operation, which move the log on
 * 操作之前改写 /d 的次数, 用于推进日志
 */
static int churn;

static void setup_ab(void)
{
	int i;

	memset(eeprom, 0xff, sizeof(eeprom));
	lxfs_init(ee_read, ee_write);
	file_put("/b", O_CREAT, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 40);
	file_put("/a", O_CREAT, old_a, old_len);
	for (i = 0; i < churn; i++)
		file_put("/d", O_CREAT | O_TRUNC, "d", 1);
}

static void op_append(void) { file_put("/a", O_APPEND, new_a + old_len, new_len - old_len); }
static void op_trunc(void) { file_put("/a", O_TRUNC, "", 0); }
static void op_unlink(void) { lxfs_unlink("/a"); }
static void op_create(void) { file_put("/c", O_CREAT | O_TRUNC, new_a, 40); }

/*
 * run @op with a reset after @n changed bytes; 1 if it finished
 * 运行 @op, 在 @n 个变化字节之后复位; 操作完成时返回 1
 */
static int run_until_reset(void (*op)(void), long n)
{
	/*
	 * not a local, which longjmp may lose
	 * 不用局部变量, longjmp 可能丢失其值
	 */
	static void (*pending)(void);

	pending = op;
	budget = n;
	if (setjmp(reset_point)) {
		budget = -1;
		return 0;
	}
	pending();
	budget = -1;
	return 1;
}

/*
 * Run @op on a fresh image with a reset after each possible number of
 * written bytes.  After the remount, /a and /c hold their contents from
 * before or after @op, and /b is untouched.
 *
 * 在新映像上运行 @op, 对每个可能的写入字节数各做一次复位. 重新挂载后, /a 与 /c
 * 保存 @op 之前或之后的内容, /b 不受影响.
 */
static void crash_test(void (*op)(void), int a_after, int c_after)
{
	char buf[DATA_BYTES];
	long n;
	int done = 0, a, c, ret;

	for (n = 0; !done; n++) {
		setup_ab();
		done = run_until_reset(op, n);
		ret = lxfs_init(ee_read, ee_write);
		CHECK(ret == LXFS_CLEAN || ret == LXFS_REPAIRED);
		a = file_get("/a", buf);
		CHECK((a == old_len && !memcmp(buf, old_a, a)) ||
		      (a == a_after && (a < 0 || !memcmp(buf, new_a, a))));
		c = file_get("/c", buf);
		CHECK(c == -1 || c == 0 || (c == c_after && !memcmp(buf, new_a, c)));
		if (done) {
			CHECK(a == a_after);
			CHECK(c == c_after);
		}
		CHECK(file_get("/b", buf) == 40 && buf[39] == 'b');
		if (churn)
			CHECK(file_get("/d", buf) == 1 && buf[0] == 'd');
		check_blocks();
	}
}

static void crash_tests(void)
{
	int i;

	for (i = 0; i < 300; i++)
		old_a[i] = new_a[i] = 'A' + i % 23;
	/* the append crosses 256; 追加跨过 256 */
	old_len = 250;
	new_len = 300;
	crash_test(op_append, new_len, -1);
	crash_test(op_trunc, 0, -1);
	crash_test(op_unlink, -1, -1);
	crash_test(op_create, old_len, 40);
}

static void dirent_poke(int index, const char *name, uint8_t first, uint16_t size)
{
	uint8_t *de = eeprom + DIR_OFF + index * DIRENT_SIZE;

	memset(de, 0, 12);
	strncpy((char *)de, name, 12);
	de[12] = first;
	de[13] = size;
	de[14] = size >> 8;
}

/*
 * Rewriting a one-block file takes each data block in turn, and a
 * remount carries on past the highest block in use rather than going
 * back to the first.
 *
 * 反复改写一个单块文件时, 各数据块轮流被取用; 重新挂载后从使用中的最高块之后
 * 继续, 不回到第一块.
 */
/*
 * the one data block in use, read from the table; -1 unless exactly one
 * 从下一块表读出唯一一个使用中的数据块; 不是恰好一个时返回 -1
 */
static int only_block(void)
{
	int b, found = -1;

	for (b = FIRST_DATA_BLOCK; b < FIRST_DATA_BLOCK + DATA_BLOCKS; b++) {
		if (eeprom[FAT_OFF + b] == 0xff)
			continue;
		if (found >= 0)
			return -1;
		found = b;
	}
	return found;
}

static void wear_tests(void)
{
	uint8_t used[DATA_BLOCKS] = { 0 };
	int i, b;

	memset(eeprom, 0xff, sizeof(eeprom));
	lxfs_init(ee_read, ee_write);
	for (i = 0; i < DATA_BLOCKS; i++) {
		CHECK(file_put("/w", O_CREAT | O_TRUNC, "x", 1) == 1);
		b = only_block();
		CHECK(b >= 0);
		if (b >= 0)
			used[b - FIRST_DATA_BLOCK]++;
	}
	for (i = 0; i < DATA_BLOCKS; i++)
		CHECK(used[i] == 1);

	CHECK(file_put("/w", O_TRUNC, "x", 1) == 1);
	CHECK(only_block() == FIRST_DATA_BLOCK);
	CHECK(file_put("/w", O_TRUNC, "x", 1) == 1);
	CHECK(lxfs_init(ee_read, ee_write) == LXFS_CLEAN);
	CHECK(file_put("/w", O_TRUNC, "x", 1) == 1);
	CHECK(only_block() == FIRST_DATA_BLOCK + 2);
	check_blocks();
}

/*
 * Creating and removing a file takes each free directory entry in turn,
 * and a remount carries on past the highest entry in use.
 *
 * 反复新建与删除文件时, 各空闲目录项轮流被取用; 重新挂载后从使用中的最高
 * 目录项之后继续.
 */
/*
 * the index of the entry whose name starts with @c, or -1
 * 名字以 @c 开头的目录项的编号; 没有时返回 -1
 */
static int dirent_of(char c)
{
	int i;

	for (i = 0; i < FILES; i++)
		if (eeprom[DIR_OFF + i * DIRENT_SIZE] == c)
			return i;
	return -1;
}

static void dirent_wear_tests(void)
{
	int i;

	memset(eeprom, 0xff, sizeof(eeprom));
	lxfs_init(ee_read, ee_write);
	CHECK(file_put("/k", O_CREAT, "", 0) == 0);
	for (i = 1; i < 6; i++) {
		CHECK(file_put("/w", O_CREAT, "", 0) == 0);
		CHECK(dirent_of('w') == i);
		CHECK(lxfs_unlink("/w") == 0);
	}
	CHECK(file_put("/w", O_CREAT, "", 0) == 0);
	CHECK(lxfs_init(ee_read, ee_write) == LXFS_CLEAN);
	for (i = 7; i < FILES; i++) {
		CHECK(file_put("/v", O_CREAT, "", 0) == 0);
		CHECK(dirent_of('v') == i);
		CHECK(lxfs_unlink("/v") == 0);
	}
	CHECK(file_put("/v", O_CREAT, "", 0) == 0);
	CHECK(dirent_of('v') == 1);
	CHECK(dirent_of('k') == 0);
	CHECK(dirent_of('w') == 6);
	check_blocks();
}

/*
 * A version 1 image: the superblock, and files built by v1_file().
 * 版本 1 的映像: 超级块, 以及由 v1_file() 构造的文件.
 */
static void v1_format(void)
{
	memset(eeprom, 0xff, sizeof(eeprom));
	memcpy(eeprom, "LXFS", 4);
	eeprom[VERSION_OFF] = 1;
}

/*
 * entry @index: @size bytes of @fill in the @n blocks of @chain
 * 目录项 @index: 在 @chain 的 @n 个块中存放 @size 个 @fill 字节
 */
static void v1_file(int index, const char *name, const uint8_t *chain, int n,
		    uint16_t size, char fill)
{
	int i;

	dirent_poke(index, name, n ? chain[0] : 0xff, size);
	for (i = 0; i < n; i++) {
		eeprom[FAT_OFF + chain[i]] = i + 1 < n ? chain[i + 1] : 0xfe;
		memset(eeprom + chain[i] * BLOCK, fill, BLOCK);
	}
}

static const uint8_t chain_a[] = { 9, 15, 10 };
static const uint8_t chain_b[] = { 11 };
static const uint8_t chain_c[] = { 16 };

/*
 * /a in blocks 9, 15 and 10, which the log now takes, /b with a size
 * still in the version 1 log, and /c.  The bytes of /a read as tags of
 * /b, should the upgrade leave them in the log.
 *
 * /a 位于第 9、15、10 块, 其中两块现由日志占用; /b 的大小仍在版本 1 的日志
 * 中; 以及 /c. /a 的字节若被升级留在日志中, 会被读作 /b 的标记.
 */
#define A_FILL	2
static void v1_image(void)
{
	v1_format();
	v1_file(0, "a", chain_a, 3, 80, A_FILL);
	v1_file(2, "b", chain_b, 1, 20, 'b');
	v1_file(5, "c", chain_c, 1, 5, 'c');
	eeprom[FAT_OFF] = 2;
	eeprom[FAT_OFF + 1] = 30;
	eeprom[FAT_OFF + 2] = 0;
}

/*
 * Static, as the buffers of upgrade_tests(): with check_blocks() below
 * them the stack outgrows the ATmega2560 of "make check-qemu".
 *
 * 与 upgrade_tests() 的缓冲区相同, 声明为静态: 连同其下的 check_blocks(),
 * 栈会超出 "make check-qemu" 所用 ATmega2560 的 SRAM.
 */
static char v1_buf[DATA_BYTES];

static void check_v1_files(void)
{
	char *buf = v1_buf;
	int i;

	CHECK(eeprom[VERSION_OFF] == 2);
	CHECK(file_get("/a", buf) == 80);
	for (i = 0; i < 80 && buf[i] == A_FILL; i++)
		;
	CHECK(i == 80);
	CHECK(file_get("/b", buf) == 30 && buf[29] == 'b');
	CHECK(file_get("/c", buf) == 5 && buf[4] == 'c');
	check_blocks();
}

static void op_mount(void) { lxfs_init(ee_read, ee_write); }

static void upgrade_tests(void)
{
	static const uint8_t full[DATA_BLOCKS + 2] = {
		9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
		21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
	};
	static const uint8_t loop[] = { 9, 12 };
	static uint8_t copy[EEPROM_SIZE];
	char *buf = v1_buf;
	long n;
	int done = 0, ret;

	v1_image();
	CHECK(lxfs_init(ee_read, ee_write) == LXFS_UPGRADED);
	check_v1_files();
	CHECK(lxfs_init(ee_read, ee_write) == LXFS_CLEAN);
	check_v1_files();

	/*
	 * a reset after every changed byte of the upgrade
	 * 升级过程中每个变化字节之后复位
	 */
	for (n = 0; !done; n++) {
		v1_image();
		done = run_until_reset(op_mount, n);
		ret = lxfs_init(ee_read, ee_write);
		CHECK(ret == LXFS_UPGRADED || ret == LXFS_CLEAN ||
		      ret == LXFS_REPAIRED);
		check_v1_files();
	}

	/*
	 * 21 blocks fit after the move, 23 do not and are left as they are
	 * 21 块在移动后放得下; 23 块放不下, 保持原样
	 */
	v1_format();
	v1_file(0, "full", full, DATA_BLOCKS, DATA_BLOCKS * BLOCK, 'f');
	CHECK(lxfs_init(ee_read, ee_write) == LXFS_UPGRADED);
	CHECK(file_get("/full", buf) == DATA_BYTES && buf[DATA_BYTES - 1] == 'f');
	CHECK(lxfs_free() == 0);
	check_blocks();
	v1_format();
	v1_file(0, "full", full, DATA_BLOCKS + 2, 736, 'f');
	memcpy(copy, eeprom, sizeof(copy));
	CHECK(lxfs_init(ee_read, ee_write) == -ENOSPC);
	CHECK(!memcmp(copy, eeprom, sizeof(copy)));

	/*
	 * a chain that loops through block 9, from an earlier kernel
	 * 早期内核留下的、经过第 9 块成环的链
	 */
	v1_format();
	v1_file(0, "loop", loop, 2, 200, 'l');
	eeprom[FAT_OFF + 12] = 9;
	ret = lxfs_init(ee_read, ee_write);
	CHECK(ret == LXFS_UPGRADED);
	CHECK(file_get("/loop", buf) == 64 && buf[63] == 'l');
	check_blocks();
}

/*
 * images that earlier kernels or a damaged part can leave behind
 * 早期内核或损坏的芯片可能留下的映像
 */
static void repair_tests(void)
{
	char buf[DATA_BYTES];
	struct file f;
	uint8_t copy[EEPROM_SIZE];

	/*
	 * a block marked used that no file reaches
	 * 标记为已用、但没有文件到达的块
	 */
	memset(eeprom, 0xff, sizeof(eeprom));
	lxfs_init(ee_read, ee_write);
	eeprom[FAT_OFF + 20] = 0xfe;
	CHECK(lxfs_init(ee_read, ee_write) == LXFS_REPAIRED);
	check_blocks();

	/*
	 * One full block with a second linked after it, as an earlier
	 * kernel leaves it after a reset: it linked new blocks before
	 * writing the size that covers them.  The next append must not
	 * read from one block and write the other.
	 *
	 * 一个已满的块之后链接着第二个块, 这是早期内核复位后留下的状态: 该内核
	 * 先链接新块, 后写入覆盖新块的大小. 下一次追加不得从一个块读取而写入另
	 * 一个块.
	 */
	memset(eeprom, 0xff, sizeof(eeprom));
	lxfs_init(ee_read, ee_write);
	memset(buf, 'x', 32);
	file_put("/t", O_CREAT, buf, 32);
	eeprom[FAT_OFF + 11] = 12;
	eeprom[FAT_OFF + 12] = 0xfe;
	CHECK(lxfs_init(ee_read, ee_write) == LXFS_REPAIRED);
	file_put("/t", O_APPEND, "0123456789", 10);
	CHECK(file_get("/t", buf) == 42 && !memcmp(buf + 32, "0123456789", 10));
	check_blocks();

	/*
	 * a create that an earlier kernel left half written: size 0xffff
	 * 早期内核写到一半的创建: 大小为 0xffff
	 */
	memset(eeprom, 0xff, sizeof(eeprom));
	lxfs_init(ee_read, ee_write);
	dirent_poke(3, "x", 0xff, 0xffff);
	CHECK(lxfs_init(ee_read, ee_write) == LXFS_REPAIRED);
	CHECK(file_get("/x", buf) == 0);
	check_blocks();

	/*
	 * a chain that loops, and one that runs into the metadata
	 * 成环的块链, 以及延伸到元数据中的块链
	 */
	memset(eeprom, 0xff, sizeof(eeprom));
	lxfs_init(ee_read, ee_write);
	dirent_poke(0, "loop", 12, 100);
	eeprom[FAT_OFF + 12] = 13;
	eeprom[FAT_OFF + 13] = 12;
	dirent_poke(1, "meta", 14, 64);
	eeprom[FAT_OFF + 14] = 3;
	CHECK(lxfs_init(ee_read, ee_write) == LXFS_REPAIRED);
	CHECK(file_get("/loop", buf) == 64);
	CHECK(file_get("/meta", buf) == 32);
	CHECK(lxfs_unlink("/meta") == 0);
	CHECK(eeprom[DIR_OFF] == 'l');
	check_blocks();

	/*
	 * a layout this kernel does not know is left alone, not formatted
	 * 本内核不认识的布局保持原样, 不予格式化
	 */
	memset(eeprom, 0xff, sizeof(eeprom));
	lxfs_init(ee_read, ee_write);
	file_put("/keep", O_CREAT, "data", 4);
	eeprom[VERSION_OFF] = 3;
	memcpy(copy, eeprom, sizeof(copy));
	CHECK(lxfs_init(ee_read, ee_write) == -EINVAL);
	CHECK(fs_open(&f, "/keep", O_RDONLY) == -EIO);
	CHECK(fs_open(&f, "/new", O_WRONLY | O_CREAT) == -EIO);
	CHECK(lxfs_unlink("/keep") == -EIO);
	CHECK(lxfs_free() == 0);
	CHECK(!memcmp(copy, eeprom, sizeof(copy)));
	eeprom[VERSION_OFF] = 2;

	/*
	 * a file created where a removed one left records in the log
	 * 在被删除的文件于日志中留有记录的目录项上创建文件
	 */
	memset(eeprom, 0xff, sizeof(eeprom));
	lxfs_init(ee_read, ee_write);
	file_put("/x", O_CREAT, "12345", 5);
	CHECK(lxfs_unlink("/x") == 0);
	CHECK(file_put("/y", O_CREAT, "", 0) == 0);
	CHECK(file_get("/y", buf) == 0);
	CHECK(lxfs_init(ee_read, ee_write) == LXFS_CLEAN);
	CHECK(file_get("/y", buf) == 0);
	CHECK(file_put("/y", O_APPEND, "ab", 2) == 2);
	CHECK(file_get("/y", buf) == 2 && !memcmp(buf, "ab", 2));
	check_blocks();

	/*
	 * a name the directory would read as a free entry
	 * 会被目录读作空闲项的文件名
	 */
	CHECK(lxfs_init(ee_read, ee_write) == LXFS_CLEAN);
	CHECK(fs_open(&f, "/\xff", O_WRONLY | O_CREAT) == -EINVAL);
}

#ifndef __AVR__
/*
 * Random appends, rewrites and unlinks on a few files, checked against a
 * copy of what each file should hold.  About one operation in three is
 * cut by a reset, which may come again during the repairing mount, and
 * half the resets leave the interrupted byte erased.  Each file must
 * hold its contents from before or after the operation, peek() must
 * agree with read(), and a second mount must find nothing to repair.
 * The model needs more SRAM than the ATmega2560 of "make check-qemu"
 * has left, so it runs on the host only.
 *
 * 对几个文件随机执行追加、改写与删除, 并与记录各文件应有内容的副本比对. 约三
 * 分之一的操作被复位打断, 复位也可能在修复性的挂载期间再次发生, 其中一半的复位
 * 使被打断的字节停在擦除状态. 每个文件必须保存操作之前或之后的内容, peek() 必须
 * 与 read() 一致, 第二次挂载不得再发现需要修复之处. 该模型所需的 SRAM 超出
 * "make check-qemu" 所用 ATmega2560 的余量, 因此只在主机上运行.
 */
#define MODEL_FILES	6
#define MODEL_SEEDS	1000
#define MODEL_OPS	400
#define MODEL_CHUNK	64

enum { M_APPEND, M_REWRITE, M_UNLINK, M_KINDS };

static const char *const model_names[MODEL_FILES] = {
	"/m0", "/m1", "/m2", "/m3", "/m4", "/m5",
};
static char model_data[MODEL_FILES][DATA_BYTES];
/* -1: no such file; -1 表示文件不存在 */
static int model_len[MODEL_FILES];
static uint32_t model_seed;
static int m_kind, m_file, m_len, m_split;
static char m_buf[MODEL_CHUNK];

/*
 * xorshift32, so that a seed gives the same run with any C library
 * xorshift32, 使同一种子在任何 C 库上得到相同的运行
 */
static uint32_t model_rand(void)
{
	model_seed ^= model_seed << 13;
	model_seed ^= model_seed >> 17;
	model_seed ^= model_seed << 5;
	return model_seed;
}

/*
 * The operation opens file m_file once and writes m_buf in two calls,
 * split at m_split, so that the second call sees what the first left in
 * the log.
 *
 * 该操作只打开文件 m_file 一次, 以两次调用写入 m_buf, 在 m_split 处分开, 第二次
 * 调用因此读到第一次调用留在日志中的记录.
 */
static void op_model(void)
{
	struct file f;
	uint16_t flags = m_kind == M_APPEND ? O_APPEND : O_CREAT | O_TRUNC;

	if (m_kind == M_UNLINK) {
		lxfs_unlink(model_names[m_file]);
		return;
	}
	if (fs_open(&f, model_names[m_file], O_WRONLY | flags) < 0)
		return;
	if (m_split && f.f_op->write(&f, m_buf, m_split) < m_split)
		return;
	if (m_len > m_split)
		f.f_op->write(&f, m_buf + m_split, m_len - m_split);
}

/*
 * 1 if @got bytes in @buf are a state m_kind may leave file m_file in,
 * from @pre bytes in @prev, with @room bytes the writes may fill.  A
 * finished operation leaves exactly one state; a cut one may also leave
 * the state before it, after its truncate or after its first write.
 *
 * 若 @buf 中的 @got 个字节是 m_kind 可能使文件 m_file 从 @prev 中的 @pre 个
 * 字节变成的状态, 返回 1; @room 为写入可以占用的字节数. 完成的操作只留下一种
 * 状态; 被打断的操作还可能留下操作之前、截断之后或第一次写入之后的状态.
 */
static int model_allowed(const char *prev, int pre, const char *buf, int got,
			 int done, int room)
{
	int base = m_kind == M_APPEND ? pre : 0;
	int want = base + (m_len < room ? m_len : room);
	int half = base + (m_split < room ? m_split : room);

	if (m_kind == M_UNLINK)
		want = half = -1;
	if (!done && got == pre && (got <= 0 || !memcmp(buf, prev, got)))
		return 1;
	if (got != want &&
	    (done || (got != half && !(m_kind == M_REWRITE && got == 0))))
		return 0;
	return got <= 0 || (!memcmp(buf, prev, base) &&
			    !memcmp(buf + base, m_buf, got - base));
}

/*
 * the bytes of file @i agree with the model, read() and peek() alike
 * 文件 @i 的内容与模型一致, 由 read() 与 peek() 读出的都是如此
 */
static void model_check(int i)
{
	struct file f;
	char buf[DATA_BYTES];
	int n, k;

	n = file_get(model_names[i], buf);
	CHECK(n == model_len[i] && (n <= 0 || !memcmp(buf, model_data[i], n)));
	if (n < 0 || fs_open(&f, model_names[i], O_RDONLY) < 0)
		return;
	for (k = 0; k < n && f.f_op->peek(&f, k) == (uint8_t)buf[k]; k++)
		;
	CHECK(k == n && f.f_op->peek(&f, n) == -1);
}

static void model_tests(void)
{
	static char prev[DATA_BYTES], buf[DATA_BYTES];
	unsigned long saved_writes = writes;
	int seed, op, i, pre, got, done, ret, room, before = fails;

	for (seed = 1; seed <= MODEL_SEEDS; seed++) {
		model_seed = seed;
		memset(eeprom, 0xff, sizeof(eeprom));
		lxfs_init(ee_read, ee_write);
		for (i = 0; i < MODEL_FILES; i++)
			model_len[i] = -1;
		for (op = 0; op < MODEL_OPS && fails == before; op++) {
			m_file = model_rand() % MODEL_FILES;
			m_kind = model_rand() % M_KINDS;
			if (m_kind == M_APPEND && model_len[m_file] < 0)
				m_kind = M_REWRITE;
			m_len = model_rand() % (MODEL_CHUNK + 1);
			m_split = model_rand() % (m_len + 1);
			for (i = 0; i < m_len; i++)
				m_buf[i] = model_rand();
			pre = model_len[m_file];
			memcpy(prev, model_data[m_file], pre > 0 ? pre : 0);
			/*
			 * a truncate gives back the blocks of the file, an
			 * append also fills the rest of its last block
			 * 截断归还该文件的块, 追加还会填满其最后一块的剩余部分
			 */
			room = lxfs_free();
			if (pre > 0)
				room += m_kind == M_APPEND ?
					(BLOCK - pre % BLOCK) % BLOCK :
					(pre + BLOCK - 1) / BLOCK * BLOCK;

			erase_interrupted = model_rand() & 1;
			done = run_until_reset(op_model, model_rand() % 3 ?
					       -1 : (long)(model_rand() % 40));
			if (!done)
				while (model_rand() % 3 == 0 &&
				       !run_until_reset(op_mount, model_rand() % 8))
					;
			erase_interrupted = 0;
			if (!done) {
				ret = lxfs_init(ee_read, ee_write);
				CHECK(ret == LXFS_CLEAN || ret == LXFS_REPAIRED);
			}

			got = file_get(model_names[m_file], buf);
			CHECK(model_allowed(prev, pre, buf, got, done, room));
			model_len[m_file] = got;
			memcpy(model_data[m_file], buf, got > 0 ? got : 0);
			for (i = 0; i < MODEL_FILES; i++)
				model_check(i);
			/*
			 * not after every operation: a mount empties the cache
			 * of fs/lxfs.c, which the runs between mounts exercise
			 * 不在每次操作之后挂载: 挂载会清空 fs/lxfs.c 的缓存,
			 * 两次挂载之间的操作正是用来检验该缓存
			 */
			if (!done || model_rand() % 10 == 0)
				check_blocks();
		}
		if (fails != before) {
			printf("model: seed %d, operation %d\n", seed, op - 1);
			break;
		}
	}
	/*
	 * the total printed at the end stays comparable between versions
	 * 使最后打印的总数在不同版本之间仍可比较
	 */
	writes = saved_writes;
}
#endif

int main(void)
{
	struct file f;
	char buf[256], name[NAME_MAX + 1];
	uint16_t free_before;
	int i, n;

	memset(eeprom, 0xff, sizeof(eeprom));
	CHECK(lxfs_init(ee_read, ee_write) == LXFS_FORMATTED);
	/* the superblock alone; 只写超级块 */
	CHECK(writes == 5);
	CHECK(lxfs_init(ee_read, ee_write) == LXFS_CLEAN);
	free_before = lxfs_free();
	CHECK(free_before == DATA_BYTES);
	CHECK(count_files() == 0);

	/*
	 * create, write across a block boundary, read back
	 * 创建, 跨块边界写入, 读回
	 */
	CHECK(fs_open(&f, "/notes", O_WRONLY | O_CREAT | O_TRUNC) == 0);
	for (i = 0; i < 40; i++)
		buf[i] = 'a' + i % 26;
	CHECK(f.f_op->write(&f, buf, 40) == 40);
	CHECK(fs_open(&f, "/notes", O_RDONLY) == 0);
	memset(buf + 100, 0, 64);
	CHECK(f.f_op->read(&f, buf + 100, 64) == 40);
	CHECK(!memcmp(buf, buf + 100, 40));
	CHECK(f.f_op->read(&f, buf + 100, 64) == 0);
	CHECK(lxfs_free() == free_before - 64);

	/*
	 * append, then read in odd-sized pieces
	 * 追加, 然后以奇数大小分段读取
	 */
	CHECK(fs_open(&f, "/notes", O_WRONLY | O_APPEND) == 0);
	CHECK(f.f_op->write(&f, "XYZ", 3) == 3);
	CHECK(fs_open(&f, "/notes", O_RDONLY) == 0);
	n = 0;
	while ((i = f.f_op->read(&f, buf + 100 + n, 7)) > 0)
		n += i;
	CHECK(n == 43 && !memcmp(buf + 100 + 40, "XYZ", 3));

	/* writing anywhere but the end is refused; 拒绝在末尾以外的位置写入 */
	CHECK(fs_open(&f, "/notes", O_WRONLY) == 0);
	CHECK(f.f_op->write(&f, "q", 1) == -EINVAL);

	/* truncate gives the blocks back; 截断释放其数据块 */
	CHECK(fs_open(&f, "/notes", O_WRONLY | O_TRUNC) == 0);
	CHECK(lxfs_free() == free_before);

	/* names; 文件名 */
	CHECK(fs_open(&f, "/missing", O_RDONLY) == -ENOENT);
	CHECK(fs_open(&f, "/abcdefghijklm", O_WRONLY | O_CREAT) == -ENAMETOOLONG);
	CHECK(fs_open(&f, "/abcdefghijkl", O_WRONLY | O_CREAT) == 0);
	CHECK(fs_open(&f, "/abcdefghijkl", O_RDONLY) == 0);
	CHECK(fs_open(&f, "/a/b", O_WRONLY | O_CREAT) == -ENOENT);
	CHECK(count_files() == 2);

	/* unlink; 删除文件 */
	CHECK(lxfs_unlink("/abcdefghijkl") == 0);
	CHECK(lxfs_unlink("/abcdefghijkl") == -ENOENT);
	CHECK(count_files() == 1);

	/* fill the data area, then one byte more; 填满数据区, 再多写一个字节 */
	CHECK(fs_open(&f, "/big", O_WRONLY | O_CREAT) == 0);
	memset(buf, 'B', sizeof(buf));
	n = 0;
	while ((i = f.f_op->write(&f, buf, 200)) > 0)
		n += i;
	CHECK(n == (int)free_before && i == -ENOSPC);
	CHECK(lxfs_free() == 0);
	CHECK(lxfs_unlink("/big") == 0);
	CHECK(lxfs_free() == free_before);

	/* the directory has LXFS_FILES slots; 目录共有 LXFS_FILES 个槽位 */
	for (i = 0; i < 15; i++) {
		snprintf(name, sizeof(name), "/f%d", i);
		CHECK(fs_open(&f, name, O_WRONLY | O_CREAT) == 0);
	}
	CHECK(fs_open(&f, "/one-more", O_WRONLY | O_CREAT) == -ENOSPC);

	/*
	 * a second mount of the same bytes sees the same files
	 * 对相同字节再次挂载, 看到相同的文件
	 */
	CHECK(lxfs_init(ee_read, ee_write) == LXFS_CLEAN);
	CHECK(count_files() == 16);

	/*
	 * Each rewrite of /d adds two records, so the log goes round more
	 * than twice: every slot is the head under either phase, and
	 * records of /a and /b are copied back to their entries.
	 *
	 * 每次改写 /d 增加两条记录, 日志因此绕过两圈以上: 每个槽都在两种相位下
	 * 成为写入位置, /a 与 /b 的记录也被复制回其目录项.
	 */
	for (churn = 0; churn <= 20; churn++)
		crash_tests();
	churn = 0;
	repair_tests();
	upgrade_tests();
	wear_tests();
	dirent_wear_tests();
#ifndef __AVR__
	model_tests();
#endif

	if (fails) {
		printf("lxfs: %d failed\n", fails);
		return 1;
	}
	printf("lxfs: all passed, %lu EEPROM bytes written\n", writes);
	return 0;
}
