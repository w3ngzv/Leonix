// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host test of fs/lxfs.c, run by "make check".  The EEPROM is a 1 KiB
 * array that starts out 0xff, as a blank part does, and every byte the
 * filesystem writes is counted, so the test also catches a change that
 * writes more of the EEPROM than it used to.
 */
#include <stdio.h>
#include <string.h>

#include <leonix/errno.h>
#include <leonix/lxfs.h>

#define EEPROM_SIZE	1024

static uint8_t eeprom[EEPROM_SIZE];
static unsigned int writes;
static int fails;

static void ee_read(uint16_t off, void *buf, uint16_t len)
{
	memcpy(buf, eeprom + off, len);
}

/* As avr-libc's eeprom_update_block(): unchanged bytes are not written. */
static void ee_write(uint16_t off, const void *buf, uint16_t len)
{
	const uint8_t *p = buf;
	uint16_t i;

	for (i = 0; i < len; i++) {
		if (eeprom[off + i] != p[i]) {
			eeprom[off + i] = p[i];
			writes++;
		}
	}
}

#define CHECK(cond) do { if (!(cond)) { \
	printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)

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

int main(void)
{
	struct file f;
	char buf[256], name[NAME_MAX + 1];
	uint16_t free_before;
	int i, n;

	memset(eeprom, 0xff, sizeof(eeprom));
	CHECK(lxfs_init(ee_read, ee_write) == 1);
	CHECK(writes == 5);			/* the superblock alone */
	CHECK(lxfs_init(ee_read, ee_write) == 0);
	free_before = lxfs_free();
	CHECK(free_before == 736);
	CHECK(count_files() == 0);

	/* create, write across a block boundary, read back */
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

	/* append, then read in odd-sized pieces */
	CHECK(fs_open(&f, "/notes", O_WRONLY | O_APPEND) == 0);
	CHECK(f.f_op->write(&f, "XYZ", 3) == 3);
	CHECK(fs_open(&f, "/notes", O_RDONLY) == 0);
	n = 0;
	while ((i = f.f_op->read(&f, buf + 100 + n, 7)) > 0)
		n += i;
	CHECK(n == 43 && !memcmp(buf + 100 + 40, "XYZ", 3));

	/* writing anywhere but the end is refused */
	CHECK(fs_open(&f, "/notes", O_WRONLY) == 0);
	CHECK(f.f_op->write(&f, "q", 1) == -EINVAL);

	/* truncate gives the blocks back */
	CHECK(fs_open(&f, "/notes", O_WRONLY | O_TRUNC) == 0);
	CHECK(lxfs_free() == free_before);

	/* names */
	CHECK(fs_open(&f, "/missing", O_RDONLY) == -ENOENT);
	CHECK(fs_open(&f, "/abcdefghijklm", O_WRONLY | O_CREAT) == -ENAMETOOLONG);
	CHECK(fs_open(&f, "/abcdefghijkl", O_WRONLY | O_CREAT) == 0);
	CHECK(fs_open(&f, "/abcdefghijkl", O_RDONLY) == 0);
	CHECK(fs_open(&f, "/a/b", O_WRONLY | O_CREAT) == -ENOENT);
	CHECK(count_files() == 2);

	/* unlink */
	CHECK(lxfs_unlink("/abcdefghijkl") == 0);
	CHECK(lxfs_unlink("/abcdefghijkl") == -ENOENT);
	CHECK(count_files() == 1);

	/* fill the data area, then one byte more */
	CHECK(fs_open(&f, "/big", O_WRONLY | O_CREAT) == 0);
	memset(buf, 'B', sizeof(buf));
	n = 0;
	while ((i = f.f_op->write(&f, buf, 200)) > 0)
		n += i;
	CHECK(n == free_before && i == -ENOSPC);
	CHECK(lxfs_free() == 0);
	CHECK(lxfs_unlink("/big") == 0);
	CHECK(lxfs_free() == free_before);

	/* the directory has LXFS_FILES slots */
	for (i = 0; i < 15; i++) {
		snprintf(name, sizeof(name), "/f%d", i);
		CHECK(fs_open(&f, name, O_WRONLY | O_CREAT) == 0);
	}
	CHECK(fs_open(&f, "/one-more", O_WRONLY | O_CREAT) == -ENOSPC);

	/* a second mount of the same bytes sees the same files */
	CHECK(lxfs_init(ee_read, ee_write) == 0);
	CHECK(count_files() == 16);

	if (fails)
		return 1;
	printf("lxfs: all passed, %u EEPROM bytes written\n", writes);
	return 0;
}
