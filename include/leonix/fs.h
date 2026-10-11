/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_FS_H
#define _LEONIX_FS_H

#include <stdint.h>

#define MAX_CHRDEVS	4

/*
 * open() flags, with the values of Linux's asm-generic/fcntl.h, except
 * O_DIRECTORY: Linux's 0x10000 does not fit the 16-bit flags kept here,
 * and 0x8000 is a bit Leonix has no other use for.
 */
#define O_RDONLY	0x0000
#define O_WRONLY	0x0001
#define O_RDWR		0x0002
#define O_ACCMODE	0x0003
#define O_CREAT		0x0040
#define O_TRUNC		0x0200
#define O_APPEND	0x0400
#define O_DIRECTORY	0x8000

/* Longest name in a directory, the terminator excluded. */
#define NAME_MAX	12

struct file;

/*
 * After Linux's struct file_operations.  read() and write() return the
 * bytes moved or a negative errno; read() on a device sleeps until at
 * least one byte is there, and returns 0 at the end of a file.
 * readdir() stores the next name in a directory and returns 1, or 0
 * after the last.  peek() returns the byte at offset @off, or -1 past
 * the end, without moving f_pos; the virtual machine fetches code with
 * it, in place of the mmap() a Linux loader would use.  A NULL member
 * is an operation the file cannot do.  Only a task may call them.
 */
struct file_operations {
	int (*read)(struct file *f, char *buf, uint8_t count);
	int (*write)(struct file *f, const char *buf, uint8_t count);
	int (*readdir)(struct file *f, char *name, uint8_t size);
	int16_t (*peek)(struct file *f, uint16_t off);
	void (*release)(struct file *f);
};

struct file {
	const struct file_operations *f_op;	/* NULL while the slot is free */
	uint16_t f_pos;
	uint16_t f_flags;
	uint16_t f_size;			/* at open, for regular files */
	uint8_t private_data;			/* the filesystem's own use */
};

/*
 * A filesystem mounted at a fixed path.  open() gets the rest of the path
 * after the mount point, starting with '/' or empty for the mount point
 * itself, fills in @f and returns 0 or a negative errno.
 */
struct mount {
	const char *path;			/* in flash */
	int (*open)(struct file *f, const char *rest, uint16_t flags);
	int (*unlink)(const char *rest);
};

int vfs_open(struct file *f, const char *path, uint16_t flags);
int register_chrdev(const char *name, const struct file_operations *fops);
int devfs_open(struct file *f, const char *rest, uint16_t flags);

#endif /* _LEONIX_FS_H */
