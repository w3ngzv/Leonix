// SPDX-License-Identifier: GPL-2.0-only
/*
 * Character devices, and /dev to reach them by name.  Linux keys them by
 * major and minor number; with no device nodes to hold a number, the
 * name is the key.  Drivers register before sched_start(), so the table
 * needs no lock.
 *
 * 字符设备, 以及按名字访问字符设备的 /dev. Linux 以主次设备号区分字符设备;
 * 此处没有保存设备号的设备节点, 因此以名字区分. 驱动在 sched_start() 之前
 * 注册, 设备表因此不需要锁.
 */
#include <leonix/pgmspace.h>
#include <stddef.h>

#include <leonix/errno.h>
#include <leonix/fs.h>
#include <leonix/string.h>

struct chrdev {
	const char *name;	/* in flash */
	const struct file_operations *fops;
};

static struct chrdev chrdevs[MAX_CHRDEVS];
static uint8_t nr_chrdevs;

/**
 * register_chrdev() - make a device appear in /dev
 * @name: its name, a string in flash
 * @fops: its operations
 *
 * 使设备出现在 /dev 中. @name 是 flash 中的字符串, @fops 是设备的操作.
 *
 * Context: before sched_start().  在 sched_start() 之前调用.
 * Return: 0, or -ENOSPC if the table is full.
 * 成功时返回 0, 设备表已满时返回 -ENOSPC.
 */
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

/**
 * devfs_open() - open /dev or a device in it
 * @f: the file to fill in
 * @rest: the path after "/dev": "" or "/" for the directory, or "/name"
 * @flags: unused
 *
 * 打开 /dev 或其中的设备. @rest 是 "/dev" 之后的路径, "" 或 "/" 表示目录
 * 本身, "/name" 表示设备; @flags 不使用.
 *
 * Return: 0, or -ENOENT if no device has the name.
 * 成功时返回 0, 没有同名设备时返回 -ENOENT.
 */
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
