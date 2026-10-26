/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_FS_H
#define _LEONIX_FS_H

#include <stdint.h>

#define MAX_CHRDEVS	4

/*
 * open() flags, with the values of Linux's asm-generic/fcntl.h, except
 * O_DIRECTORY: Linux's 0x10000 does not fit the 16-bit flags kept here,
 * and 0x8000 is a bit Leonix has no other use for.
 *
 * open() 的标志, 取值与 Linux 的 asm-generic/fcntl.h 相同, 只有 O_DIRECTORY 例
 * 外: Linux 的 0x10000 超出此处 16 位标志的范围, 而 0x8000 是 Leonix 没有其他用
 * 途的位.
 */
#define O_RDONLY	0x0000
#define O_WRONLY	0x0001
#define O_RDWR		0x0002
#define O_ACCMODE	0x0003
#define O_CREAT		0x0040
#define O_TRUNC		0x0200
#define O_APPEND	0x0400
#define O_DIRECTORY	0x8000

/*
 * longest name in a directory, the terminator excluded
 * 目录中名字的最大长度, 不含结束符
 */
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
 *
 * 仿照 Linux 的 struct file_operations. read() 与 write() 返回传送的字节数或负
 * 的 errno; 设备上的 read() 睡眠到至少有一个字节可读, 到达文件末尾时返回 0.
 * readdir() 写入目录中的下一个名字并返回 1, 最后一个名字之后返回 0. peek() 返回
 * 偏移 @off 处的字节, 超出末尾时返回 -1, 不移动 f_pos; 虚拟机以 peek() 取指,
 * 代替 Linux 加载程序所用的 mmap(). 值为 NULL 的成员表示文件不支持该操作. 只允
 * 许任务调用这些操作.
 */
struct file_operations {
	int (*read)(struct file *f, char *buf, uint8_t count);
	int (*write)(struct file *f, const char *buf, uint8_t count);
	int (*readdir)(struct file *f, char *name, uint8_t size);
	int16_t (*peek)(struct file *f, uint16_t off);
	void (*release)(struct file *f);
};

struct file {
	/* NULL while the slot is free; 槽位空闲时为 NULL */
	const struct file_operations *f_op;
	uint16_t f_pos;
	uint16_t f_flags;
	/* at open, for regular files; 普通文件打开时的大小 */
	uint16_t f_size;
	/* the filesystem's own use; 由文件系统自行使用 */
	uint8_t private_data;
};

/*
 * A filesystem mounted at a fixed path.  open() gets the rest of the path
 * after the mount point, starting with '/' or empty for the mount point
 * itself, fills in @f and returns 0 or a negative errno.
 *
 * 挂载在固定路径上的文件系统. open() 得到挂载点之后的路径, 该路径以 '/' 开头,
 * 或在挂载点本身时为空; open() 填写 @f, 返回 0 或负的 errno.
 */
struct mount {
	const char *path;			/* in flash; 存放在 flash 中 */
	int (*open)(struct file *f, const char *rest, uint16_t flags);
	int (*unlink)(const char *rest);
};

int vfs_open(struct file *f, const char *path, uint16_t flags);
int register_chrdev(const char *name, const struct file_operations *fops);
int devfs_open(struct file *f, const char *rest, uint16_t flags);

#endif /* _LEONIX_FS_H */
