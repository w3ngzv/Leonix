/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_FS_H
#define _LEONIX_FS_H

#include <stdint.h>

#define MAX_CHRDEVS	4

/*
 * A character device, after Linux's struct file_operations but with
 * neither files nor offsets: a device has one stream each way.
 *
 * read() sleeps until at least one byte is there and returns how many it
 * stored, at most @count.  write() sleeps until every byte has been taken
 * and returns @count.  Either returns -1 on a device that cannot do it.
 * Only a task may call them.
 */
struct file_operations {
	int (*read)(char *buf, uint8_t count);
	int (*write)(const char *buf, uint8_t count);
};

int register_chrdev(const char *name, const struct file_operations *fops);
const struct file_operations *chrdev_get(const char *name);
const char *chrdev_name(uint8_t index);

#endif /* _LEONIX_FS_H */
