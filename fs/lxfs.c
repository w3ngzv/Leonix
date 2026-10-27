// SPDX-License-Identifier: GPL-2.0-only
/*
 * lxfs, the root filesystem: one flat directory on the 1 KiB EEPROM.
 *
 * The device is cut into 32 blocks of 32 bytes.  The front holds a
 * superblock, a table of the next block of each block, as in FAT, the
 * directory and the log; the blocks after it hold file data.
 *
 *   0    superblock: magic "LXFS", version, 3 bytes unused
 *   8    next-block table, one byte per block: LXFS_FREE, LXFS_END, or
 *        the number of the next block of the same file
 *   40   directory, LXFS_FILES entries: name, first block, size
 *   288  log: LOG_SLOTS tags, then LOG_SLOTS records of first block
 *        and size
 *   352  data, blocks LXFS_DATA_FIRST to LXFS_BLOCKS - 1
 *
 * The first block and size of a file change with every write, and an
 * EEPROM byte lasts about 100,000 writes.  So they are not written to
 * the directory entry each time but to the next slot of the log, in
 * turn; the newest record of an entry overrides the entry's own first
 * block and size.  A record is copied back to its entry only when its
 * slot comes round again while it is still the newest for that entry.
 * A tag is the entry's index and a phase bit that flips on every pass
 * through the log, which lets the mount find where the log goes on.
 *
 * A blank EEPROM reads 0xff throughout, so LXFS_FREE is 0xff, a free
 * directory entry starts with 0xff and a free tag is 0xff: formatting
 * writes only the superblock.  The device is reached through two
 * functions given to lxfs_init(), which lets tests/test_lxfs.c run this
 * file on the host.
 *
 * A file is written at its end only: O_APPEND, O_TRUNC, or a file that
 * is empty.  A reset leaves every file as it was before the operation
 * or as it is after it.  The device writes one byte at a time, and a
 * byte is written whole or not at all, so each operation changes one
 * byte last and that byte decides: the tag of a record for a write or
 * a truncate, the first byte of the name for a create or unlink.  Data
 * goes to the device before the record that covers it.  A slot that
 * still holds a record has its tag cleared before the new record goes
 * in, so that a reset never leaves a tag over a half-written record.
 *
 * What a reset leaves besides, blocks taken that no file reaches or
 * linked past the end of a file, lxfs_init() puts right at the next
 * mount.  It also cuts a chain that leaves the data area, loops or runs
 * into another file's blocks, which earlier kernels could leave and the
 * code here would otherwise follow.  Version 1, which kept data in the
 * blocks the log now fills, is upgraded in place if
 * CONFIG_LXFS_V1_UPGRADE is set.
 *
 * Only one task may use the filesystem, which holds for the shell and
 * the programs it runs.
 *
 * lxfs, 根文件系统: 1 KiB EEPROM 上的一个平坦目录.
 *
 * 设备分为 32 个 32 字节的块. 前部依次是超级块、与 FAT 相同的记录每个块下一块的
 * 表、目录与日志; 其后的块存放文件数据.
 *
 *   0    超级块: 魔数 "LXFS", 版本号, 3 个未用字节
 *   8    下一块表, 每块一个字节: LXFS_FREE、LXFS_END, 或同一文件下一块的
 *        块号
 *   40   目录, 共 LXFS_FILES 项: 名字、首块、大小
 *   288  日志: LOG_SLOTS 个标记, 其后是 LOG_SLOTS 条记录, 每条为首块与
 *        大小
 *   352  数据, 第 LXFS_DATA_FIRST 至 LXFS_BLOCKS - 1 块
 *
 * 文件的首块与大小随每次写入而改变, 而 EEPROM 的每个字节约可写入 100,000 次.
 * 因此这两项不是每次都写入目录项, 而是依次写入日志的下一个槽; 某目录项最新的
 * 记录取代该目录项自身的首块与大小. 只有当某条记录所在的槽再次轮到、而该记录
 * 仍是其目录项最新的记录时, 才把该记录复制回目录项. 标记由目录项编号与一个相位
 * 位组成, 相位位每轮过日志一遍翻转一次, 挂载时据此找出日志续写的位置.
 *
 * 空白 EEPROM 全部读出 0xff, 因此 LXFS_FREE 取 0xff, 空闲目录项以 0xff 开头, 空
 * 闲标记为 0xff: 格式化只需写入超级块. 设备经由传给 lxfs_init() 的两个函数访
 * 问, tests/test_lxfs.c 因此可以在主机上运行本文件.
 *
 * 文件只在末尾写入: O_APPEND、O_TRUNC, 或空文件. 复位之后, 每个文件或保持操作之
 * 前的样子, 或是操作之后的样子. 设备每次写一个字节, 一个字节要么完整写入, 要么
 * 完全未写, 因此每种操作最后修改一个字节, 由该字节决定结果: 写入与截断为记录的
 * 标记, 创建与删除为名字的第一个字节. 数据先于覆盖它的记录写入设备. 仍保存记录
 * 的槽在写入新记录之前先清除标记, 复位因此不会留下标记有效而记录只写了一半的
 * 槽.
 *
 * 复位还可能留下其他状态: 被占用却没有文件到达的块, 以及链接到文件末尾之后的
 * 块. lxfs_init() 在下一次挂载时修复这些状态. lxfs_init() 也截断离开数据区、成
 * 环或进入另一文件的块的链; 早先的内核可能留下这类链, 而此处的代码原本会沿链走
 * 下去. 版本 1 把数据存放在现在由日志占用的块中, 设置了
 * CONFIG_LXFS_V1_UPGRADE 时挂载时就地升级.
 *
 * 只允许一个任务使用该文件系统, shell 及其运行的程序满足这一条件.
 */
#include <stddef.h>

#include <leonix/errno.h>
#include <leonix/fs.h>
#include <leonix/lxfs.h>
#include <leonix/string.h>

#define LXFS_BLOCK		32
#define LXFS_BLOCKS		32
#define LXFS_FILES		16
#define LXFS_VERSION		2

#define LXFS_FREE		0xff
#define LXFS_END		0xfe

#define SB_OFF			0
#define SB_VERSION		4
#define FAT_OFF			8
#define DIR_OFF			(FAT_OFF + LXFS_BLOCKS)

struct lxfs_dirent {
	/*
	 * not terminated when NAME_MAX long
	 * 长度为 NAME_MAX 时没有结束符
	 */
	char name[NAME_MAX];
	/*
	 * LXFS_FREE while the file is empty; overridden by the log
	 * 文件为空时为 LXFS_FREE; 以日志中的记录为准
	 */
	uint8_t first;
	uint8_t size_lo, size_hi;
};

#define DIRENT_SIZE		sizeof(struct lxfs_dirent)
#define DIRENT_FIRST		offsetof(struct lxfs_dirent, first)
#define DIR_END			(DIR_OFF + LXFS_FILES * DIRENT_SIZE)

/*
 * A record is the last three bytes of a directory entry.  A tag holds
 * the entry's index and the phase; a free tag has TAG_FREE bits set.
 *
 * 记录即目录项的最后三个字节. 标记保存目录项编号与相位; 空闲标记中 TAG_FREE 的
 * 位被置位.
 */
#define LOG_SLOTS		16
#define LOG_REC			(DIRENT_SIZE - DIRENT_FIRST)
#define LOG_TAGS		((DIR_END + LXFS_BLOCK - 1) / LXFS_BLOCK * LXFS_BLOCK)
#define LOG_RECS		(LOG_TAGS + LOG_SLOTS)
#define LOG_END			(LOG_RECS + LOG_SLOTS * LOG_REC)
#define TAG_INDEX		0x0f
#define TAG_PHASE		0x10
#define TAG_FREE		0xe0

#define LXFS_DATA_FIRST		((LOG_END + LXFS_BLOCK - 1) / LXFS_BLOCK)
#define LXFS_DATA_BLOCKS	(LXFS_BLOCKS - LXFS_DATA_FIRST)

/*
 * version 1: data from block 9, and a size log in table entries 0 to 2
 * 版本 1: 数据自第 9 块开始, 大小日志位于下一块表的第 0 至 2 项
 */
#define V1_DATA_FIRST		9
#define V1_LOG_OFF		FAT_OFF
#define COPY_CHUNK		8

_Static_assert(LXFS_DATA_FIRST < LXFS_BLOCKS, "no room left for data");
_Static_assert(LXFS_BLOCKS < LXFS_END, "block numbers collide with markers");
_Static_assert(LXFS_BLOCKS <= 32, "the block bitmap of lxfs_check() is 32 bits");
_Static_assert(TAG_INDEX + 1 >= LXFS_FILES, "a tag holds the index in 4 bits");
_Static_assert((LOG_SLOTS & (LOG_SLOTS - 1)) == 0, "slots wrap by masking");
_Static_assert(LOG_TAGS / LXFS_BLOCK == V1_DATA_FIRST,
	       "the log starts where version 1 kept data");
_Static_assert(LXFS_BLOCK % COPY_CHUNK == 0, "a block copies in whole chunks");
_Static_assert(LXFS_BLOCK * LXFS_BLOCKS == LXFS_SIZE,
	       "the blocks must cover the device exactly");

static const char lxfs_magic[4] = { 'L', 'X', 'F', 'S' };

static lxfs_read_fn dev_read;
static lxfs_write_fn dev_write;
static uint8_t mounted;
/*
 * where alloc_block() starts looking, so that blocks are taken in turn
 * and no block wears out first; set past the highest block in use at
 * mount, which keeps the turn across a reset
 *
 * alloc_block() 开始查找的位置, 使各块轮流被取用, 不由某一块先磨损; 挂载时
 * 设为使用中的最高块之后, 复位后轮转因此得以延续
 */
static uint8_t next_block;
/*
 * the slot the next record goes into, and the phase it is tagged with
 * 下一条记录写入的槽, 以及该记录标记中的相位
 */
static uint8_t log_head, log_phase;

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

static uint8_t tag_get(uint8_t slot)
{
	uint8_t tag;

	dev_read(LOG_TAGS + slot, &tag, 1);
	return tag;
}

static void tag_set(uint8_t slot, uint8_t tag)
{
	dev_write(LOG_TAGS + slot, &tag, 1);
}

static uint8_t tag_used(uint8_t tag)
{
	return !(tag & TAG_FREE);
}

/*
 * the newest of the @n slots before log_head that holds a record of
 * entry @index, or LXFS_FREE
 * log_head 之前的 @n 个槽中保存目录项 @index 记录的最新一个; 没有时返回
 * LXFS_FREE
 */
static uint8_t log_find(uint8_t index, uint8_t n)
{
	uint8_t slot = log_head, tag;

	while (n--) {
		slot = (slot - 1) & (LOG_SLOTS - 1);
		tag = tag_get(slot);
		if (tag_used(tag) && (tag & TAG_INDEX) == index)
			return slot;
	}
	return LXFS_FREE;
}

/*
 * the entry as the directory holds it, without the log
 * 目录中保存的目录项, 不计日志
 */
static void dirent_raw(uint8_t index, struct lxfs_dirent *de)
{
	dev_read(DIR_OFF + index * DIRENT_SIZE, de, DIRENT_SIZE);
}

/*
 * the entry with the first block and size of its newest record
 * 目录项, 首块与大小取自其最新的记录
 */
static void dirent_get(uint8_t index, struct lxfs_dirent *de)
{
	uint8_t slot;

	dirent_raw(index, de);
	slot = log_find(index, LOG_SLOTS);
	if (slot != LXFS_FREE)
		dev_read(LOG_RECS + slot * LOG_REC,
			 (uint8_t *)de + DIRENT_FIRST, LOG_REC);
}

static uint16_t dirent_size(const struct lxfs_dirent *de)
{
	return de->size_lo | (uint16_t)de->size_hi << 8;
}

static uint8_t dirent_used(const struct lxfs_dirent *de)
{
	return (uint8_t)de->name[0] != LXFS_FREE;
}

static uint8_t entry_used(uint8_t index)
{
	uint8_t c;

	dev_read(DIR_OFF + index * DIRENT_SIZE, &c, 1);
	return c != LXFS_FREE;
}

/*
 * Record @first and @size as entry @index's, in the slot at log_head.
 * The record there is first copied to its entry if no newer one of the
 * same entry exists; its tag is then cleared, and the new tag written
 * last.
 *
 * 在 log_head 处的槽中记下目录项 @index 的首块 @first 与大小 @size. 该槽原有的
 * 记录若没有同一目录项更新的记录, 先复制到其目录项; 随后清除其标记, 新标记最后
 * 写入.
 */
static void log_append(uint8_t index, uint8_t first, uint16_t size)
{
	uint8_t rec[LOG_REC] = { first, size, size >> 8 };
	uint8_t old[LOG_REC];
	uint8_t tag = tag_get(log_head), owner;

	if (tag_used(tag)) {
		owner = tag & TAG_INDEX;
		if (entry_used(owner) &&
		    log_find(owner, LOG_SLOTS - 1) == LXFS_FREE) {
			dev_read(LOG_RECS + log_head * LOG_REC, old, LOG_REC);
			dev_write(DIR_OFF + owner * DIRENT_SIZE + DIRENT_FIRST,
				  old, LOG_REC);
		}
		tag_set(log_head, LXFS_FREE);
	}
	dev_write(LOG_RECS + log_head * LOG_REC, rec, LOG_REC);
	tag_set(log_head, log_phase | index);
	log_head = (log_head + 1) & (LOG_SLOTS - 1);
	if (!log_head)
		log_phase ^= TAG_PHASE;
}

/*
 * Find log_head and log_phase from the tags.  Slots are written in
 * order, so those before log_head carry this pass's phase and the rest
 * the last pass's, or are free.  Slot 0 free means either an empty log
 * or a reset while slot 0 was being written: the next pass flips the
 * phase that the other slots carry.
 *
 * 由标记求出 log_head 与 log_phase. 槽按顺序写入, 因此 log_head 之前的槽带有
 * 本轮的相位, 其余的槽带有上一轮的相位, 或者空闲. 第 0 个槽空闲, 说明日志为
 * 空, 或者在写入第 0 个槽时发生了复位: 下一轮的相位与其他槽的相位相反.
 */
static void log_scan(void)
{
	uint8_t slot, tag, phase;

	log_head = 0;
	tag = tag_get(0);
	if (!tag_used(tag)) {
		log_phase = 0;
		for (slot = 1; slot < LOG_SLOTS; slot++) {
			tag = tag_get(slot);
			if (tag_used(tag)) {
				log_phase = (tag & TAG_PHASE) ^ TAG_PHASE;
				break;
			}
		}
		return;
	}
	phase = tag & TAG_PHASE;
	for (slot = 1; slot < LOG_SLOTS; slot++) {
		tag = tag_get(slot);
		if (!tag_used(tag) || (tag & TAG_PHASE) != phase) {
			log_head = slot;
			log_phase = phase;
			return;
		}
	}
	log_phase = phase ^ TAG_PHASE;
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
		dirent_raw(i, &de);
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

	/*
	 * TODO: subdirectories; the directory is one flat table
	 * 待办: 子目录; 目录只是一张平坦的表
	 */
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
	uint8_t b = next_block, n;

	for (n = LXFS_DATA_BLOCKS; n; n--) {
		if (b >= LXFS_BLOCKS)
			b = LXFS_DATA_FIRST;
		if (fat_get(b) == LXFS_FREE) {
			fat_set(b, LXFS_END);
			next_block = b + 1;
			return b;
		}
		b++;
	}
	return LXFS_FREE;
}

/*
 * point next_block past the highest data block in use
 * 使 next_block 指向使用中的最高数据块之后
 */
static void find_next_block(void)
{
	uint8_t b;

	next_block = LXFS_DATA_FIRST;
	for (b = LXFS_DATA_FIRST; b < LXFS_BLOCKS; b++)
		if (fat_get(b) != LXFS_FREE)
			next_block = b + 1;
}

static int lxfs_write(struct file *f, const char *buf, uint8_t count)
{
	struct lxfs_dirent de;
	uint16_t size;
	uint8_t last, block, off, n, done = 0;

	dirent_get(f->private_data, &de);
	size = dirent_size(&de);
	/*
	 * TODO: writing anywhere but the end, which needs the block of
	 * f_pos found as lxfs_read() finds it.
	 *
	 * 待办: 在末尾以外的位置写入, 需要像 lxfs_read() 那样找出 f_pos 所在的
	 * 块.
	 */
	if (f->f_pos != size)
		return -EINVAL;

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
	log_append(f->private_data, de.first, size);
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
		dirent_raw(f->private_data++, &de);
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
		for (index = 0; index < LXFS_FILES; index++)
			if (!entry_used(index))
				break;
		if (index == LXFS_FILES)
			return -ENOSPC;
		/*
		 * a record left by a file once at this index would
		 * override the new entry
		 * 曾位于该编号的文件留下的记录会取代新目录项的值
		 */
		if (log_find(index, LOG_SLOTS) != LXFS_FREE)
			log_append(index, LXFS_FREE, 0);
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
		log_append(index, LXFS_FREE, 0);
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
	uint8_t freed = LXFS_FREE;

	if (!mounted)
		return -EIO;
	name = rest_to_name(rest, &err);
	if (!name)
		return err;
	index = find(name);
	if (index < 0)
		return index;
	/*
	 * TODO: a descriptor still open on the file keeps its directory
	 * index, and reads whatever file is created there next.
	 *
	 * 待办: 仍打开该文件的描述符保留其目录项编号, 会读到此后在该目录项
	 * 创建的文件.
	 */
	dirent_get(index, &de);
	dev_write(DIR_OFF + index * DIRENT_SIZE, &freed, 1);
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
 * Follow the blocks that the size of entry @index needs from @first,
 * within @data_first and the end of the device, and claim them in
 * *seen.  Stops at a block already claimed.  Returns how many were
 * followed and leaves the last in *last and the next in *next.
 *
 * 自 @first 沿链走过目录项 @index 的大小所需的块, 只在 @data_first 至设备末尾
 * 之间, 并在 *seen 中登记. 遇到已登记的块时停止. 返回走过的块数, 最后一块存入
 * *last, 其下一块存入 *next.
 */
static uint8_t follow(uint8_t first, uint16_t size, uint8_t data_first,
		      uint32_t *seen, uint8_t *last, uint8_t *next)
{
	uint16_t need = size ? (size - 1) / LXFS_BLOCK + 1 : 0;
	uint8_t block = first, got = 0;

	*last = LXFS_FREE;
	while (got < need && block >= data_first && block < LXFS_BLOCKS &&
	       !(*seen & (uint32_t)1 << block)) {
		*seen |= (uint32_t)1 << block;
		*last = block;
		block = fat_get(block);
		got++;
	}
	*next = block;
	return got;
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
	uint16_t size, kept;
	uint8_t first, last, next, got, fixed = 0;

	dirent_get(index, &de);
	size = dirent_size(&de);
	got = follow(de.first, size, LXFS_DATA_FIRST, seen, &last, &next);
	first = got ? de.first : LXFS_FREE;
	kept = (uint16_t)got * LXFS_BLOCK;
	if (kept > size)
		kept = size;
	if (first != de.first || kept != size) {
		log_append(index, first, kept);
		fixed = 1;
	}
	if (got && next != LXFS_END) {
		fat_set(last, LXFS_END);
		fixed = 1;
	}
	return fixed;
}

/*
 * make the table and the directory agree
 * 使下一块表与目录一致
 */
static uint8_t lxfs_check(void)
{
	uint32_t seen = 0;
	uint8_t i, fixed = 0;

	for (i = 0; i < LXFS_FILES; i++)
		if (entry_used(i))
			fixed |= check_file(i, &seen);
	for (i = LXFS_DATA_FIRST; i < LXFS_BLOCKS; i++) {
		if (!(seen & (uint32_t)1 << i) && fat_get(i) != LXFS_FREE) {
			fat_set(i, LXFS_FREE);
			fixed = 1;
		}
	}
	return fixed;
}

#ifdef CONFIG_LXFS_V1_UPGRADE
/*
 * Move block @from of a version 1 file to @to, which no file reaches:
 * the data and the next block first, then the link from @prev, or from
 * entry @index when @from is the first block, and last @from freed.
 *
 * 把版本 1 文件的块 @from 移到没有文件到达的块 @to: 先写数据与下一块, 再改写
 * @prev 的链接, @from 为首块时改写目录项 @index 的首块, 最后释放 @from.
 */
static void v1_move(uint8_t index, uint8_t prev, uint8_t from, uint8_t to)
{
	uint8_t chunk[COPY_CHUNK];
	uint8_t off;

	for (off = 0; off < LXFS_BLOCK; off += COPY_CHUNK) {
		dev_read(from * LXFS_BLOCK + off, chunk, COPY_CHUNK);
		dev_write(to * LXFS_BLOCK + off, chunk, COPY_CHUNK);
	}
	fat_set(to, fat_get(from));
	if (prev == LXFS_FREE)
		dev_write(DIR_OFF + index * DIRENT_SIZE + DIRENT_FIRST, &to, 1);
	else
		fat_set(prev, to);
	fat_set(from, LXFS_FREE);
}

/*
 * Upgrade a version 1 filesystem.  Its size log is finished, the blocks
 * of its files that lie where the log now goes are moved to blocks no
 * file reaches, the tags are cleared, and the version written last.
 * Each step leaves a valid version 1, so a reset repeats the upgrade.
 * Blocks taken that no file reaches count as free, which is what a
 * reset during a move leaves.
 *
 * 升级版本 1 的文件系统. 先完成其大小日志, 再把各文件位于日志新位置的块移到没有
 * 文件到达的块, 然后清除标记, 最后写入版本号. 每一步之后都是有效的版本 1, 复位
 * 后升级因此重新进行. 被占用却没有文件到达的块计为空闲, 移动期间的复位留下的
 * 正是这种块.
 *
 * Return: 0, or -ENOSPC if the files need more blocks than version 2
 * has; nothing has been moved then.
 * 成功时返回 0; 文件所需的块多于版本 2 所有时返回 -ENOSPC, 此时没有移动任何块.
 */
static int lxfs_upgrade(void)
{
	struct lxfs_dirent de;
	uint32_t seen = 0, pending = 0, walked;
	uint8_t log[3], i, last, next, moves = 0, room = 0, to, prev, block;
	uint16_t size;

	dev_read(V1_LOG_OFF, log, sizeof(log));
	if (log[0] != LXFS_FREE) {
		if (log[0] < LXFS_FILES)
			dev_write(DIR_OFF + log[0] * DIRENT_SIZE + DIRENT_FIRST + 1,
				  log + 1, 2);
		log[0] = LXFS_FREE;
		dev_write(V1_LOG_OFF, log, 1);
	}

	for (i = 0; i < LXFS_FILES; i++) {
		dirent_raw(i, &de);
		if (dirent_used(&de))
			follow(de.first, dirent_size(&de), V1_DATA_FIRST, &seen,
			       &last, &next);
	}
	for (i = V1_DATA_FIRST; i < LXFS_BLOCKS; i++) {
		if (!(seen & (uint32_t)1 << i))
			room += i >= LXFS_DATA_FIRST;
		else if (i < LXFS_DATA_FIRST)
			moves++;
	}
	if (moves > room)
		return -ENOSPC;
	pending = seen & (((uint32_t)1 << LXFS_DATA_FIRST) - 1);

	for (i = 0; i < LXFS_FILES; i++) {
		dirent_raw(i, &de);
		if (!dirent_used(&de))
			continue;
		size = dirent_size(&de);
		prev = LXFS_FREE;
		block = de.first;
		walked = 0;
		while (size && block >= V1_DATA_FIRST && block < LXFS_BLOCKS &&
		       !(walked & (uint32_t)1 << block)) {
			/*
			 * a block of another file's chain as well is moved
			 * once, by the file that claimed it
			 * 同时属于另一文件的链的块只移动一次, 由登记该块的
			 * 文件移动
			 */
			if (pending & (uint32_t)1 << block) {
				pending &= ~((uint32_t)1 << block);
				for (to = LXFS_DATA_FIRST; to < LXFS_BLOCKS &&
				     seen & (uint32_t)1 << to; to++)
					;
				seen |= (uint32_t)1 << to;
				v1_move(i, prev, block, to);
				block = to;
			}
			walked |= (uint32_t)1 << block;
			prev = block;
			block = fat_get(block);
			size = size > LXFS_BLOCK ? size - LXFS_BLOCK : 0;
		}
	}

	for (i = 0; i < LOG_SLOTS; i++)
		tag_set(i, LXFS_FREE);
	i = LXFS_VERSION;
	dev_write(SB_OFF + SB_VERSION, &i, 1);
	return 0;
}

#else
static int lxfs_upgrade(void)
{
	return -EINVAL;
}
#endif

/**
 * lxfs_init() - mount lxfs on a device
 * @read: reads the device
 * @write: writes the device
 *
 * A device without the magic is formatted, which on a blank EEPROM
 * writes the superblock alone.  Version 1 is upgraded in place if
 * CONFIG_LXFS_V1_UPGRADE is set.  One with the magic and another version
 * is left as it is and not mounted, so that a kernel flashed over a
 * newer one does not destroy its files.
 *
 * 挂载经由 @read 与 @write 访问的设备. 没有魔数的设备被格式化, 在空白 EEPROM 上
 * 这只写入超级块. 设置了 CONFIG_LXFS_V1_UPGRADE 时, 版本 1 就地升级. 有魔数而
 * 版本不同的设备保持原样, 不挂载, 使覆盖烧录到较新内核之上的旧内核不会破坏其
 * 文件.
 *
 * Return: LXFS_CLEAN, LXFS_FORMATTED, LXFS_REPAIRED or LXFS_UPGRADED;
 * -EINVAL for an unknown version or version 1 without the upgrade,
 * -ENOSPC for version 1 files that do not fit version 2.
 * 返回 LXFS_CLEAN、LXFS_FORMATTED、LXFS_REPAIRED 或 LXFS_UPGRADED; 版本未知或
 * 未启用升级而遇到版本 1 时返回 -EINVAL, 版本 1 的文件放不进版本 2 时返回
 * -ENOSPC.
 */
int lxfs_init(lxfs_read_fn read, lxfs_write_fn write)
{
	uint8_t sb[SB_VERSION + 1];
	uint8_t blank[DIRENT_SIZE];
	uint8_t i, ret;
	int err;

	dev_read = read;
	dev_write = write;
	mounted = 0;
	dev_read(SB_OFF, sb, sizeof(sb));
	if (!memcmp(sb, lxfs_magic, sizeof(lxfs_magic))) {
		ret = LXFS_CLEAN;
		if (sb[SB_VERSION] == 1) {
			err = lxfs_upgrade();
			if (err)
				return err;
			ret = LXFS_UPGRADED;
		} else if (sb[SB_VERSION] != LXFS_VERSION) {
			return -EINVAL;
		}
		mounted = 1;
		log_scan();
		if (lxfs_check() && ret == LXFS_CLEAN)
			ret = LXFS_REPAIRED;
		find_next_block();
		return ret;
	}

	memset(blank, LXFS_FREE, sizeof(blank));
	for (i = 0; i < LXFS_BLOCKS; i++)
		fat_set(i, LXFS_FREE);
	for (i = 0; i < LXFS_FILES; i++)
		dev_write(DIR_OFF + i * DIRENT_SIZE, blank, sizeof(blank));
	for (i = 0; i < LOG_SLOTS; i++)
		tag_set(i, LXFS_FREE);
	memcpy(sb, lxfs_magic, sizeof(lxfs_magic));
	sb[SB_VERSION] = LXFS_VERSION;
	dev_write(SB_OFF, sb, sizeof(sb));
	log_head = 0;
	log_phase = 0;
	next_block = LXFS_DATA_FIRST;
	mounted = 1;
	return LXFS_FORMATTED;
}
