// SPDX-License-Identifier: GPL-2.0-only
/*
 * lxfs, the root filesystem: one flat directory on the 1 KiB EEPROM.
 *
 * The device is cut into 32 blocks of 32 bytes.  The front holds a
 * superblock, a table of the next block of each block, as in FAT, and
 * the directory; the blocks after it hold file data.
 *
 *   0    superblock: magic "LXFS", version, 3 bytes unused
 *   8    next-block table, one byte per block: LXFS_FREE, LXFS_END, or
 *        the number of the next block of the same file.  The first
 *        three entries, for blocks of metadata that never chain, are
 *        the size log described below.
 *   40   directory, LXFS_FILES entries: name, first block, size
 *   288  data, blocks LXFS_DATA_FIRST to LXFS_BLOCKS - 1
 *
 * A blank EEPROM reads 0xff throughout, so LXFS_FREE is 0xff and a free
 * directory entry starts with 0xff: formatting writes only the
 * superblock.  The device is reached through two functions given to
 * lxfs_init(), which lets tests/test_lxfs.c run this file on the host.
 *
 * A file is written at its end only: O_APPEND, O_TRUNC, or a file that
 * is empty.  A reset leaves every file as it was before the operation
 * or as it is after it.  The device writes one byte at a time, and a
 * byte is written whole or not at all, so each operation changes one
 * byte last and that byte decides: the size for a write, the first byte
 * of the name for a create or unlink, the first block for a truncate.
 * Data goes to the device before the size that covers it.  A size
 * whose two bytes both change is first written to the log, entries 0
 * to 2 of the next-block table: the low byte, the high byte, and last
 * the directory index, which LXFS_FREE marks as no log.
 *
 * What a reset leaves besides, blocks taken that no file reaches or
 * linked past the end of a file, and a log not yet copied, lxfs_init()
 * puts right at the next mount.  It also cuts a chain that leaves the
 * data area, loops or runs into another file's blocks, which earlier
 * kernels could leave and the code here would otherwise follow.
 *
 * Only one task may use the filesystem, which holds for the shell and
 * the programs it runs.
 *
 * lxfs, 根文件系统: 1 KiB EEPROM 上的一个平坦目录.
 *
 * 设备分为 32 个 32 字节的块. 前部依次是超级块、与 FAT 相同的记录每个块下一块的
 * 表, 以及目录; 其后的块存放文件数据.
 *
 *   0    超级块: 魔数 "LXFS", 版本号, 3 个未用字节
 *   8    下一块表, 每块一个字节: LXFS_FREE、LXFS_END, 或同一文件下一块的
 *        块号. 前三项对应从不成链的元数据块, 用作下文所述的大小日志.
 *   40   目录, 共 LXFS_FILES 项: 名字、首块、大小
 *   288  数据, 第 LXFS_DATA_FIRST 至 LXFS_BLOCKS - 1 块
 *
 * 空白 EEPROM 全部读出 0xff, 因此 LXFS_FREE 取 0xff, 空闲目录项以 0xff 开头: 格
 * 式化只需写入超级块. 设备经由传给 lxfs_init() 的两个函数访问,
 * tests/test_lxfs.c 因此可以在主机上运行本文件.
 *
 * 文件只在末尾写入: O_APPEND、O_TRUNC, 或空文件. 复位之后, 每个文件或保持操作之
 * 前的样子, 或是操作之后的样子. 设备每次写一个字节, 一个字节要么完整写入, 要么
 * 完全未写, 因此每种操作最后修改一个字节, 由该字节决定结果: 写入为大小, 创建与
 * 删除为名字的第一个字节, 截断为首块. 数据先于覆盖它的大小写入设备. 两个字节都
 * 要改变的大小先写入日志, 即下一块表的第 0 至 2 项: 依次为低字节、高字节, 最后
 * 是目录项编号, 编号为 LXFS_FREE 表示没有日志.
 *
 * 复位还可能留下其他状态: 被占用却没有文件到达的块, 链接到文件末尾之后的块, 以
 * 及尚未复制的日志. lxfs_init() 在下一次挂载时修复这些状态. lxfs_init() 也截断
 * 离开数据区、成环或进入另一文件的块的链; 早先的内核可能留下这类链, 而此处的代
 * 码原本会沿链走下去.
 *
 * 只允许一个任务使用该文件系统, shell 及其运行的程序满足这一条件.
 */
#include <stddef.h>
#include <string.h>

#include <leonix/errno.h>
#include <leonix/fs.h>
#include <leonix/lxfs.h>

#define LXFS_BLOCK		32
#define LXFS_BLOCKS		32
#define LXFS_FILES		16
#define LXFS_VERSION		1

#define LXFS_FREE		0xff
#define LXFS_END		0xfe

#define SB_OFF			0
#define FAT_OFF			8
#define LOG_OFF			FAT_OFF
#define DIR_OFF			(FAT_OFF + LXFS_BLOCKS)

struct lxfs_dirent {
	/*
	 * not terminated when NAME_MAX long
	 * 长度为 NAME_MAX 时没有结束符
	 */
	char name[NAME_MAX];
	/*
	 * LXFS_FREE while the file is empty
	 * 文件为空时为 LXFS_FREE
	 */
	uint8_t first;
	uint8_t size_lo, size_hi;
};

#define DIRENT_SIZE		sizeof(struct lxfs_dirent)
#define DIRENT_FIRST		offsetof(struct lxfs_dirent, first)
#define DIRENT_SIZE_LO		offsetof(struct lxfs_dirent, size_lo)
#define DIR_END			(DIR_OFF + LXFS_FILES * DIRENT_SIZE)
#define LXFS_DATA_FIRST		((DIR_END + LXFS_BLOCK - 1) / LXFS_BLOCK)

_Static_assert(LXFS_DATA_FIRST < LXFS_BLOCKS, "no room left for data");
_Static_assert(LXFS_BLOCKS < LXFS_END, "block numbers collide with markers");
_Static_assert(LXFS_DATA_FIRST >= 3, "the log needs three metadata blocks");
_Static_assert(LXFS_BLOCKS <= 32, "the block bitmap of lxfs_check() is 32 bits");
_Static_assert(LXFS_BLOCK * LXFS_BLOCKS == LXFS_SIZE,
	       "the blocks must cover the device exactly");

static const char lxfs_magic[4] = { 'L', 'X', 'F', 'S' };

static lxfs_read_fn dev_read;
static lxfs_write_fn dev_write;
static uint8_t mounted;

static uint8_t fat_get(uint8_t block)
{
	uint8_t next;

	dev_read(FAT_OFF + block, &next, 1);
	return next;
}

static void fat_set(uint8_t block, uint8_t next)
{
	dev_write(FAT_OFF + block, &next, 1);
}

static void dirent_get(uint8_t index, struct lxfs_dirent *de)
{
	dev_read(DIR_OFF + index * DIRENT_SIZE, de, DIRENT_SIZE);
}

static void dirent_set(uint8_t index, uint8_t field, uint8_t value)
{
	dev_write(DIR_OFF + index * DIRENT_SIZE + field, &value, 1);
}

static uint16_t dirent_size(const struct lxfs_dirent *de)
{
	return de->size_lo | (uint16_t)de->size_hi << 8;
}

static uint8_t dirent_used(const struct lxfs_dirent *de)
{
	return (uint8_t)de->name[0] != LXFS_FREE;
}

/*
 * Change the size of entry @index from @old to @size, through the log
 * when both bytes change.
 *
 * 把目录项 @index 的大小从 @old 改为 @size, 两个字节都改变时经由日志进行.
 */
static void set_size(uint8_t index, uint16_t old, uint16_t size)
{
	uint8_t log[3] = { index, size, size >> 8 };

	if ((old ^ size) & 0xff00 && (old ^ size) & 0x00ff) {
		dev_write(LOG_OFF + 1, log + 1, 2);
		dev_write(LOG_OFF, log, 1);
	}
	dev_write(DIR_OFF + index * DIRENT_SIZE + DIRENT_SIZE_LO, log + 1, 2);
	log[0] = LXFS_FREE;
	dev_write(LOG_OFF, log, 1);
}

/*
 * give every block of a chain back
 * 释放一条链上的每个块
 */
static void free_chain(uint8_t block)
{
	uint8_t next;

	while (block != LXFS_FREE && block != LXFS_END) {
		next = fat_get(block);
		fat_set(block, LXFS_FREE);
		block = next;
	}
}

/*
 * the directory index of @name, or -ENOENT
 * @name 的目录项编号; 不存在时返回 -ENOENT
 */
static int find(const char *name)
{
	struct lxfs_dirent de;
	uint8_t i;

	for (i = 0; i < LXFS_FILES; i++) {
		dirent_get(i, &de);
		if (dirent_used(&de) && !strncmp(de.name, name, NAME_MAX))
			return i;
	}
	return -ENOENT;
}

/*
 * check @rest, "/name", and return the name, or NULL with *err set
 * 检查形如 "/name" 的 @rest 并返回名字; 不合法时返回 NULL 并设置 *err
 */
static const char *rest_to_name(const char *rest, int *err)
{
	const char *name = rest + 1;
	size_t len = strlen(name);

	*err = -ENOENT;
	if (rest[0] != '/' || !len || strchr(name, '/'))
		return NULL;
	*err = -ENAMETOOLONG;
	if (len > NAME_MAX)
		return NULL;
	*err = -EINVAL;
	/* a free entry starts so; 空闲目录项以此开头 */
	if ((uint8_t)name[0] == LXFS_FREE)
		return NULL;
	return name;
}

static int lxfs_read(struct file *f, char *buf, uint8_t count)
{
	struct lxfs_dirent de;
	uint16_t size, skip;
	uint8_t block, off, n, done = 0;

	dirent_get(f->private_data, &de);
	size = dirent_size(&de);
	if (f->f_pos >= size)
		return 0;
	if (count > size - f->f_pos)
		count = size - f->f_pos;

	block = de.first;
	for (skip = f->f_pos / LXFS_BLOCK; skip; skip--)
		block = fat_get(block);
	off = f->f_pos % LXFS_BLOCK;
	while (done < count) {
		n = LXFS_BLOCK - off;
		if (n > count - done)
			n = count - done;
		dev_read(block * LXFS_BLOCK + off, buf + done, n);
		done += n;
		off = 0;
		block = fat_get(block);
	}
	f->f_pos += done;
	return done;
}

/*
 * a free data block, marked as the end of a chain, or LXFS_FREE
 * 取一个空闲数据块并标为链尾; 没有空闲块时返回 LXFS_FREE
 */
static uint8_t alloc_block(void)
{
	uint8_t b;

	for (b = LXFS_DATA_FIRST; b < LXFS_BLOCKS; b++) {
		if (fat_get(b) == LXFS_FREE) {
			fat_set(b, LXFS_END);
			return b;
		}
	}
	return LXFS_FREE;
}

static int lxfs_write(struct file *f, const char *buf, uint8_t count)
{
	struct lxfs_dirent de;
	uint16_t old, size;
	uint8_t first, last, block, off, n, done = 0;

	dirent_get(f->private_data, &de);
	old = size = dirent_size(&de);
	if (f->f_pos != size)
		return -EINVAL;
	first = de.first;

	last = de.first;
	if (last != LXFS_FREE)
		while ((block = fat_get(last)) != LXFS_END)
			last = block;

	while (done < count) {
		off = size % LXFS_BLOCK;
		if (last == LXFS_FREE || (off == 0 && size)) {
			block = alloc_block();
			if (block == LXFS_FREE)
				break;
			if (last == LXFS_FREE)
				de.first = block;
			else
				fat_set(last, block);
			last = block;
		}
		n = LXFS_BLOCK - off;
		if (n > count - done)
			n = count - done;
		dev_write(last * LXFS_BLOCK + off, buf + done, n);
		done += n;
		size += n;
	}
	if (!done)
		return -ENOSPC;
	if (de.first != first)
		dirent_set(f->private_data, DIRENT_FIRST, de.first);
	set_size(f->private_data, old, size);
	f->f_pos = size;
	return done;
}

/*
 * Byte @off of the file.  The chain is walked from the first block every
 * time, at most one step per data block; the virtual machine fetches its
 * code this way when a program is run from lxfs.
 *
 * 文件的第 @off 字节. 每次都从首块开始沿链查找, 至多每个数据块一步; 从 lxfs 运
 * 行程序时, 虚拟机以这种方式取指.
 */
static int16_t lxfs_peek(struct file *f, uint16_t off)
{
	struct lxfs_dirent de;
	uint8_t block, b;

	dirent_get(f->private_data, &de);
	if (off >= dirent_size(&de))
		return -1;
	block = de.first;
	for (b = off / LXFS_BLOCK; b; b--)
		block = fat_get(block);
	dev_read(block * LXFS_BLOCK + off % LXFS_BLOCK, &b, 1);
	return b;
}

static const struct file_operations lxfs_file_fops = {
	.read = lxfs_read,
	.write = lxfs_write,
	.peek = lxfs_peek,
};

/**
 * lxfs_readdir() - the next name in the root directory
 * @f: the open directory
 * @name: where to store the name
 * @size: bytes in @name
 *
 * The cursor is private_data, so that fs/vfs.c can list its mount points
 * first through f_pos and then hand the same open file here.
 *
 * 把根目录中的下一个名字写入 @name. 游标保存在 private_data 中, fs/vfs.c 因此
 * 可以先以 f_pos 列出各挂载点, 再把同一个打开的文件交给本函数.
 *
 * Return: 1, or 0 after the last name.
 * 成功时返回 1, 最后一个名字之后返回 0.
 */
int lxfs_readdir(struct file *f, char *name, uint8_t size)
{
	struct lxfs_dirent de;
	uint8_t len;

	while (mounted && f->private_data < LXFS_FILES) {
		dirent_get(f->private_data++, &de);
		if (!dirent_used(&de))
			continue;
		len = size - 1 < NAME_MAX ? size - 1 : NAME_MAX;
		memcpy(name, de.name, len);
		name[len] = '\0';
		return 1;
	}
	return 0;
}

static const struct file_operations lxfs_dir_fops = {
	.readdir = lxfs_readdir,
};

/**
 * lxfs_open() - open a file on lxfs
 * @f: the file to fill in
 * @rest: "/name"
 * @flags: open() flags; writing needs O_APPEND, O_TRUNC or an empty file
 *
 * 打开 lxfs 上的文件 @rest. fs/vfs.c 把 lxfs 挂载在 /. 写入要求 O_APPEND、
 * O_TRUNC 或空文件.
 *
 * Return: 0, or a negative errno; -EIO while nothing is mounted.
 * 成功时返回 0, 失败时返回负的 errno; 未挂载时返回 -EIO.
 */
int lxfs_open(struct file *f, const char *rest, uint16_t flags)
{
	struct lxfs_dirent de;
	const char *name;
	int index, err;

	if (!mounted)
		return -EIO;
	if (!strcmp(rest, "/")) {
		f->f_op = &lxfs_dir_fops;
		return 0;
	}
	name = rest_to_name(rest, &err);
	if (!name)
		return err;

	index = find(name);
	if (index < 0) {
		if (!(flags & O_CREAT))
			return -ENOENT;
		for (index = 0; index < LXFS_FILES; index++) {
			dirent_get(index, &de);
			if (!dirent_used(&de))
				break;
		}
		if (index == LXFS_FILES)
			return -ENOSPC;
		memset(&de, 0, sizeof(de));
		strncpy(de.name, name, NAME_MAX);
		de.first = LXFS_FREE;
		/*
		 * the entry is in use once its first byte is
		 * 第一个字节写入之后, 该目录项才算被占用
		 */
		dev_write(DIR_OFF + index * DIRENT_SIZE + 1,
			  (const uint8_t *)&de + 1, DIRENT_SIZE - 1);
		dev_write(DIR_OFF + index * DIRENT_SIZE, &de, 1);
	} else if ((flags & O_TRUNC) && (flags & O_ACCMODE) != O_RDONLY) {
		dirent_get(index, &de);
		dirent_set(index, DIRENT_FIRST, LXFS_FREE);
		set_size(index, dirent_size(&de), 0);
		free_chain(de.first);
	}

	f->f_op = &lxfs_file_fops;
	f->private_data = index;
	dirent_get(index, &de);
	f->f_size = dirent_size(&de);
	if (flags & O_APPEND)
		f->f_pos = f->f_size;
	return 0;
}

/**
 * lxfs_unlink() - remove a file from lxfs
 * @rest: "/name"
 *
 * 删除 lxfs 上的文件 @rest. 先清除名字的第一个字节, 再释放其块.
 *
 * Return: 0, or a negative errno; -EIO while nothing is mounted.
 * 成功时返回 0, 失败时返回负的 errno; 未挂载时返回 -EIO.
 */
int lxfs_unlink(const char *rest)
{
	struct lxfs_dirent de;
	const char *name;
	int index, err;

	if (!mounted)
		return -EIO;
	name = rest_to_name(rest, &err);
	if (!name)
		return err;
	index = find(name);
	if (index < 0)
		return index;
	dirent_get(index, &de);
	dirent_set(index, 0, LXFS_FREE);
	free_chain(de.first);
	return 0;
}

/**
 * lxfs_free() - free data bytes
 *
 * Return: bytes in free blocks, whole blocks only; 0 while nothing is
 * mounted.
 * 空闲块中的字节数, 只计整块; 未挂载时返回 0.
 */
uint16_t lxfs_free(void)
{
	uint16_t free = 0;
	uint8_t b;

	for (b = LXFS_DATA_FIRST; mounted && b < LXFS_BLOCKS; b++)
		if (fat_get(b) == LXFS_FREE)
			free += LXFS_BLOCK;
	return free;
}

/*
 * Make entry @index agree with the next-block table.  The blocks its
 * size needs are followed and claimed in *seen; the chain is cut where
 * it leaves the data area or reaches a claimed block, and the size cut
 * to the blocks kept.  Blocks linked past them are cut off.  Returns 1
 * if anything was written.
 *
 * 使目录项 @index 与下一块表一致. 沿链走过其大小所需的块, 并在 *seen 中登记; 链
 * 离开数据区或到达已登记的块时截断, 大小随之缩减到保留下来的块. 链接在这些块之
 * 后的块被切断. 有任何写入时返回 1.
 */
static uint8_t check_file(uint8_t index, uint32_t *seen)
{
	struct lxfs_dirent de;
	uint16_t size, need, got = 0;
	uint8_t block, last = LXFS_FREE, fixed = 0;

	dirent_get(index, &de);
	size = dirent_size(&de);
	need = size ? (size - 1) / LXFS_BLOCK + 1 : 0;
	block = de.first;
	while (got < need && block >= LXFS_DATA_FIRST && block < LXFS_BLOCKS &&
	       !(*seen & (uint32_t)1 << block)) {
		*seen |= (uint32_t)1 << block;
		last = block;
		block = fat_get(block);
		got++;
	}
	if (!got && de.first != LXFS_FREE) {
		dirent_set(index, DIRENT_FIRST, LXFS_FREE);
		fixed = 1;
	}
	if (got < need) {
		set_size(index, size, got * LXFS_BLOCK);
		fixed = 1;
	}
	if (got && block != LXFS_END) {
		fat_set(last, LXFS_END);
		fixed = 1;
	}
	return fixed;
}

/*
 * Finish a logged size, then make the table and the directory agree.
 *
 * 完成日志中记下的大小修改, 再使下一块表与目录一致.
 */
static uint8_t lxfs_check(void)
{
	struct lxfs_dirent de;
	uint32_t seen = 0;
	uint8_t log[3], i, fixed = 0;

	dev_read(LOG_OFF, log, sizeof(log));
	if (log[0] != LXFS_FREE) {
		if (log[0] < LXFS_FILES)
			dev_write(DIR_OFF + log[0] * DIRENT_SIZE + DIRENT_SIZE_LO,
				  log + 1, 2);
		log[0] = LXFS_FREE;
		dev_write(LOG_OFF, log, 1);
		fixed = 1;
	}
	for (i = 0; i < LXFS_FILES; i++) {
		dirent_get(i, &de);
		if (dirent_used(&de))
			fixed |= check_file(i, &seen);
	}
	for (i = LXFS_DATA_FIRST; i < LXFS_BLOCKS; i++) {
		if (!(seen & (uint32_t)1 << i) && fat_get(i) != LXFS_FREE) {
			fat_set(i, LXFS_FREE);
			fixed = 1;
		}
	}
	return fixed;
}

/**
 * lxfs_init() - mount lxfs on a device
 * @read: reads the device
 * @write: writes the device
 *
 * A device without the magic is formatted, which on a blank EEPROM
 * writes the superblock alone.  One with the magic and another version
 * is left as it is and not mounted, so that a kernel flashed over a
 * newer one does not destroy its files.
 *
 * 挂载经由 @read 与 @write 访问的设备. 没有魔数的设备被格式化, 在空白 EEPROM 上
 * 这只写入超级块. 有魔数而版本不同的设备保持原样, 不挂载, 使覆盖烧录到较新内核
 * 之上的旧内核不会破坏其文件.
 *
 * Return: LXFS_CLEAN, LXFS_FORMATTED or LXFS_REPAIRED, or -EINVAL for
 * an unknown version.
 * 返回 LXFS_CLEAN、LXFS_FORMATTED 或 LXFS_REPAIRED; 版本未知时返回 -EINVAL.
 */
int lxfs_init(lxfs_read_fn read, lxfs_write_fn write)
{
	uint8_t sb[sizeof(lxfs_magic) + 1];
	uint8_t blank[DIRENT_SIZE];
	uint8_t i;

	dev_read = read;
	dev_write = write;
	mounted = 0;
	dev_read(SB_OFF, sb, sizeof(sb));
	if (!memcmp(sb, lxfs_magic, sizeof(lxfs_magic))) {
		if (sb[sizeof(lxfs_magic)] != LXFS_VERSION)
			return -EINVAL;
		mounted = 1;
		return lxfs_check() ? LXFS_REPAIRED : LXFS_CLEAN;
	}

	memset(blank, LXFS_FREE, sizeof(blank));
	for (i = 0; i < LXFS_BLOCKS; i++)
		fat_set(i, LXFS_FREE);
	for (i = 0; i < LXFS_FILES; i++)
		dev_write(DIR_OFF + i * DIRENT_SIZE, blank, sizeof(blank));
	memcpy(sb, lxfs_magic, sizeof(lxfs_magic));
	sb[sizeof(lxfs_magic)] = LXFS_VERSION;
	dev_write(SB_OFF, sb, sizeof(sb));
	mounted = 1;
	return LXFS_FORMATTED;
}
