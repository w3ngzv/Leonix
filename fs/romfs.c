// SPDX-License-Identifier: GPL-2.0-only
/*
 * /bin: a read-only filesystem in flash, built from user/bin by
 * tools/mkromfs.py, the way Linux carries its first programs in an
 * initramfs.  The files are the programs of the virtual machine; their
 * bytes can be read like any file's.
 */
#include <avr/pgmspace.h>
#include <string.h>

#include <leonix/errno.h>
#include <leonix/romfs.h>

static const uint8_t *entry_data(uint8_t index)
{
	return pgm_read_ptr(&romfs_entries[index].data);
}

static int16_t romfs_peek(struct file *f, uint16_t off)
{
	if (off >= f->f_size)
		return -1;
	return pgm_read_byte(entry_data(f->private_data) + off);
}

static int romfs_read(struct file *f, char *buf, uint8_t count)
{
	uint8_t n;

	if (f->f_pos >= f->f_size)
		return 0;
	if (count > f->f_size - f->f_pos)
		count = f->f_size - f->f_pos;
	for (n = 0; n < count; n++)
		buf[n] = romfs_peek(f, f->f_pos + n);
	f->f_pos += count;
	return count;
}

static const struct file_operations romfs_file_fops = {
	.read = romfs_read,
	.peek = romfs_peek,
};

static int romfs_readdir(struct file *f, char *name, uint8_t size)
{
	if (f->f_pos >= romfs_count)
		return 0;
	strlcpy_P(name, pgm_read_ptr(&romfs_entries[f->f_pos++].name), size);
	return 1;
}

static const struct file_operations romfs_dir_fops = {
	.readdir = romfs_readdir,
};

/* Mounted at /bin by fs/vfs.c.  @rest is "" or "/", or "/name". */
int romfs_open(struct file *f, const char *rest, uint16_t flags)
{
	uint8_t i;

	if (!rest[0] || !strcmp_P(rest, PSTR("/"))) {
		f->f_op = &romfs_dir_fops;
		return 0;
	}
	if ((flags & O_ACCMODE) != O_RDONLY || (flags & O_CREAT))
		return -EROFS;
	for (i = 0; i < romfs_count; i++) {
		if (strcmp_P(rest + 1, pgm_read_ptr(&romfs_entries[i].name)))
			continue;
		f->f_op = &romfs_file_fops;
		f->private_data = i;
		f->f_size = pgm_read_word(&romfs_entries[i].size);
		return 0;
	}
	return -ENOENT;
}
