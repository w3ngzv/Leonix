// SPDX-License-Identifier: GPL-2.0-only
/*
 * Character devices, and /dev to reach them by name.  Linux keys them by
 * major and minor number and finds the name through a device node; with
 * no node to hold the number, the name is the key here.  Drivers register
 * before sched_start(), so the table needs no lock.
 */
#include <avr/pgmspace.h>
#include <stddef.h>
#include <string.h>

#include <leonix/errno.h>
#include <leonix/fs.h>

struct chrdev {
	const char *name;	/* in flash */
	const struct file_operations *fops;
};

static struct chrdev chrdevs[MAX_CHRDEVS];
static uint8_t nr_chrdevs;

/* @name is a string in flash.  Returns 0, or -ENOSPC if the table is full. */
int register_chrdev(const char *name, const struct file_operations *fops)
{
	if (nr_chrdevs == MAX_CHRDEVS)
		return -ENOSPC;
	chrdevs[nr_chrdevs].name = name;
	chrdevs[nr_chrdevs].fops = fops;
	nr_chrdevs++;
	return 0;
}

static int devfs_readdir(struct file *f, char *name, uint8_t size)
{
	if (f->f_pos >= nr_chrdevs)
		return 0;
	strlcpy_P(name, chrdevs[f->f_pos++].name, size);
	return 1;
}

static const struct file_operations devfs_dir_fops = {
	.readdir = devfs_readdir,
};

/* Mounted at /dev by fs/vfs.c.  @rest is "" or "/", or "/name". */
int devfs_open(struct file *f, const char *rest, uint16_t flags)
{
	uint8_t i;

	(void)flags;
	if (!rest[0] || !strcmp_P(rest, PSTR("/"))) {
		f->f_op = &devfs_dir_fops;
		return 0;
	}
	for (i = 0; i < nr_chrdevs; i++) {
		if (strcmp_P(rest + 1, chrdevs[i].name))
			continue;
		f->f_op = chrdevs[i].fops;
		return 0;
	}
	return -ENOENT;
}
