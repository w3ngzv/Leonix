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
#define DATA_BYTES	736

static uint8_t eeprom[EEPROM_SIZE];
static unsigned int writes;
static int fails;

/*
 * bytes that may still change before the simulated reset; -1: no reset
 * 模拟复位之前仍可变化的字节数; -1 表示不复位
 */
static long budget = -1;
static jmp_buf reset_point;

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
			if (budget == 0)
				longjmp(reset_point, 1);
			if (budget > 0)
				budget--;
			eeprom[off + i] = p[i];
			writes++;
		}
	}
}

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		printf("FAIL line %d: %s\n", __LINE__, #cond);		\
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

static void setup_ab(void)
{
	memset(eeprom, 0xff, sizeof(eeprom));
	lxfs_init(ee_read, ee_write);
	file_put("/b", O_CREAT, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 40);
	file_put("/a", O_CREAT, old_a, old_len);
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
	eeprom[FAT_OFF + 9] = 10;
	eeprom[FAT_OFF + 10] = 0xfe;
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
	eeprom[4] = 2;
	memcpy(copy, eeprom, sizeof(copy));
	CHECK(lxfs_init(ee_read, ee_write) == -EINVAL);
	CHECK(fs_open(&f, "/keep", O_RDONLY) == -EIO);
	CHECK(fs_open(&f, "/new", O_WRONLY | O_CREAT) == -EIO);
	CHECK(lxfs_unlink("/keep") == -EIO);
	CHECK(lxfs_free() == 0);
	CHECK(!memcmp(copy, eeprom, sizeof(copy)));
	eeprom[4] = 1;

	/*
	 * a name the directory would read as a free entry
	 * 会被目录读作空闲项的文件名
	 */
	CHECK(lxfs_init(ee_read, ee_write) == LXFS_CLEAN);
	CHECK(fs_open(&f, "/\xff", O_WRONLY | O_CREAT) == -EINVAL);
}

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
	CHECK(free_before == 736);
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

	crash_tests();
	repair_tests();

	if (fails) {
		printf("lxfs: %d failed\n", fails);
		return 1;
	}
	printf("lxfs: all passed, %u EEPROM bytes written\n", writes);
	return 0;
}
