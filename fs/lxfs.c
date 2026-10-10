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
 *        the number of the next block of the same file
 *   40   directory, LXFS_FILES entries: name, first block, size
 *   288  data, blocks LXFS_DATA_FIRST to LXFS_BLOCKS - 1
 *
 * A blank EEPROM reads 0xff throughout, so LXFS_FREE is 0xff and a free
 * directory entry starts with 0xff: formatting writes only the
 * superblock.  The device is reached through two functions given to
 * lxfs_init(), which lets tests/test_lxfs.c run this file on the host.
 *
 * A file is written at its end only: O_APPEND, O_TRUNC, or a file that
 * is empty.  Data goes to the device before the size that covers it, so
 * a reset in the middle of a write leaves the file as it was, with at
 * worst a block that no file reaches.  Nothing finds such blocks again;
 * that is a fsck this filesystem does not have.
 *
 * Only one task may use the filesystem, which holds for the shell and
 * the programs it runs.
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
#define DIR_OFF			(FAT_OFF + LXFS_BLOCKS)

struct lxfs_dirent {
	char name[NAME_MAX];		/* not terminated when NAME_MAX long */
	uint8_t first;			/* LXFS_FREE while the file is empty */
	uint8_t size_lo, size_hi;
};

#define DIRENT_SIZE		sizeof(struct lxfs_dirent)
#define DIR_END			(DIR_OFF + LXFS_FILES * DIRENT_SIZE)
#define LXFS_DATA_FIRST		((DIR_END + LXFS_BLOCK - 1) / LXFS_BLOCK)

_Static_assert(LXFS_DATA_FIRST < LXFS_BLOCKS, "no room left for data");
_Static_assert(LXFS_BLOCKS < LXFS_END, "block numbers collide with markers");

static const char lxfs_magic[4] = { 'L', 'X', 'F', 'S' };

static lxfs_read_fn dev_read;
static lxfs_write_fn dev_write;

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

static void dirent_put(uint8_t index, const struct lxfs_dirent *de)
{
	dev_write(DIR_OFF + index * DIRENT_SIZE, de, DIRENT_SIZE);
}

static uint16_t dirent_size(const struct lxfs_dirent *de)
{
	return de->size_lo | (uint16_t)de->size_hi << 8;
}

static uint8_t dirent_used(const struct lxfs_dirent *de)
{
	return (uint8_t)de->name[0] != LXFS_FREE;
}

/* Give every block of a chain back. */
static void free_chain(uint8_t block)
{
	uint8_t next;

	while (block != LXFS_FREE && block != LXFS_END) {
		next = fat_get(block);
		fat_set(block, LXFS_FREE);
		block = next;
	}
}

/* The directory index of @name, or -ENOENT. */
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

/* Check @rest, "/name", and return the name, or NULL with *err set. */
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

/* A free data block, marked as the end of a chain, or LXFS_FREE. */
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
	uint16_t size;
	uint8_t last, block, off, n, done = 0;

	dirent_get(f->private_data, &de);
	size = dirent_size(&de);
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
	de.size_lo = size;
	de.size_hi = size >> 8;
	dirent_put(f->private_data, &de);
	f->f_pos = size;
	return done ? done : -ENOSPC;
}

static const struct file_operations lxfs_file_fops = {
	.read = lxfs_read,
	.write = lxfs_write,
};

/*
 * The next name in the directory.  The cursor is private_data, so that
 * fs/vfs.c can list its mount points first through f_pos and then hand
 * the same open file here.
 */
int lxfs_readdir(struct file *f, char *name, uint8_t size)
{
	struct lxfs_dirent de;
	uint8_t len;

	while (f->private_data < LXFS_FILES) {
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

/* Mounted at / by fs/vfs.c.  @rest is "/" or "/name". */
int lxfs_open(struct file *f, const char *rest, uint16_t flags)
{
	struct lxfs_dirent de;
	const char *name;
	int index, err;

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
		dirent_put(index, &de);
	} else if ((flags & O_TRUNC) && (flags & O_ACCMODE) != O_RDONLY) {
		dirent_get(index, &de);
		free_chain(de.first);
		de.first = LXFS_FREE;
		de.size_lo = de.size_hi = 0;
		dirent_put(index, &de);
	}

	f->f_op = &lxfs_file_fops;
	f->private_data = index;
	if (flags & O_APPEND) {
		dirent_get(index, &de);
		f->f_pos = dirent_size(&de);
	}
	return 0;
}

int lxfs_unlink(const char *rest)
{
	struct lxfs_dirent de;
	const char *name;
	int index, err;

	name = rest_to_name(rest, &err);
	if (!name)
		return err;
	index = find(name);
	if (index < 0)
		return index;
	dirent_get(index, &de);
	free_chain(de.first);
	memset(&de, LXFS_FREE, sizeof(de));
	dirent_put(index, &de);
	return 0;
}

/* Free data bytes, whole blocks only. */
uint16_t lxfs_free(void)
{
	uint16_t free = 0;
	uint8_t b;

	for (b = LXFS_DATA_FIRST; b < LXFS_BLOCKS; b++)
		if (fat_get(b) == LXFS_FREE)
			free += LXFS_BLOCK;
	return free;
}

/*
 * Attach the device.  A device without the magic is formatted, which on
 * a blank EEPROM writes the superblock alone.  Returns 1 if it was
 * formatted, 0 if a filesystem was there already.
 */
int lxfs_init(lxfs_read_fn read, lxfs_write_fn write)
{
	uint8_t sb[sizeof(lxfs_magic) + 1];
	uint8_t blank[DIRENT_SIZE];
	uint8_t i;

	dev_read = read;
	dev_write = write;
	dev_read(SB_OFF, sb, sizeof(sb));
	if (!memcmp(sb, lxfs_magic, sizeof(lxfs_magic)) &&
	    sb[sizeof(lxfs_magic)] == LXFS_VERSION)
		return 0;

	memset(blank, LXFS_FREE, sizeof(blank));
	for (i = 0; i < LXFS_BLOCKS; i++)
		fat_set(i, LXFS_FREE);
	for (i = 0; i < LXFS_FILES; i++)
		dev_write(DIR_OFF + i * DIRENT_SIZE, blank, sizeof(blank));
	memcpy(sb, lxfs_magic, sizeof(lxfs_magic));
	sb[sizeof(lxfs_magic)] = LXFS_VERSION;
	dev_write(SB_OFF, sb, sizeof(sb));
	return 1;
}
