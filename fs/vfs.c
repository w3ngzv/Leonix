// SPDX-License-Identifier: GPL-2.0-only
/*
 * One namespace for every file, as Linux's VFS joins its filesystems.  A
 * path goes to the first mount point that is a prefix of it ending at a
 * '/', with "/" last to catch the rest.  No filesystem has directories
 * inside, so that is the whole walk.  A relative path starts at the root,
 * the only working directory.
 *
 * The root directory lists the other mount points, then the files of
 * lxfs, the root filesystem.
 *
 * 所有文件的统一命名空间, 与 Linux 的 VFS 联结各文件系统的方式相同. 路径交给
 * 第一个是其前缀且在 '/' 处结束的挂载点, "/" 排在最后, 接收其余路径. 文件
 * 系统内部没有目录, 路径查找到此为止. 相对路径从根目录开始, 根目录是唯一的
 * 工作目录.
 *
 * 根目录先列出其他挂载点, 再列出根文件系统 lxfs 中的文件.
 */
#include <leonix/pgmspace.h>
#include <stddef.h>
#include <string.h>

#include <leonix/array_size.h>
#include <leonix/procfs.h>
#include <leonix/romfs.h>
#include <leonix/errno.h>
#include <leonix/lxfs.h>
#include <leonix/tty.h>
#include <leonix/unistd.h>

static const char dev_path[] PROGMEM = "/dev";
static const char bin_path[] PROGMEM = "/bin";
static const char proc_path[] PROGMEM = "/proc";
static const char root_path[] PROGMEM = "/";

static const struct mount mounts[] = {
	{ bin_path, romfs_open, NULL },
	{ dev_path, devfs_open, NULL },
	{ proc_path, procfs_open, NULL },
	{ root_path, lxfs_open, lxfs_unlink },	/* last */
};

#define NR_MOUNTS	ARRAY_SIZE(mounts)

static struct file files[MAX_FDS];

/*
 * the path being resolved; one task uses the VFS
 * 正在解析的路径; 只有一个任务使用 VFS
 */
static char abs_path[TTY_LINE + 2];

/*
 * @path made absolute in abs_path, with no "." or ".." and no repeated
 * '/', or NULL if it does not fit.  ".." at the root stays there, as in
 * Linux.
 *
 * 把 @path 转为 abs_path 中的绝对路径, 不含 "." 与 "..", 也没有重复的 '/';
 * 放不下时返回 NULL. 与 Linux 相同, 根目录下的 ".." 仍是根目录.
 */
static const char *absolute(const char *path)
{
	const char *seg;
	uint8_t len = 0, n;

	while (*path) {
		while (*path == '/')
			path++;
		seg = path;
		while (*path && *path != '/')
			path++;
		n = path - seg;
		if (!n || (n == 1 && seg[0] == '.'))
			continue;
		if (n == 2 && seg[0] == '.' && seg[1] == '.') {
			while (len && abs_path[--len] != '/')
				;
			continue;
		}
		if ((size_t)len + 1 + n >= sizeof(abs_path))
			return NULL;
		abs_path[len++] = '/';
		memcpy(abs_path + len, seg, n);
		len += n;
	}
	if (!len)
		abs_path[len++] = '/';
	abs_path[len] = '\0';
	return abs_path;
}

static int root_readdir(struct file *f, char *name, uint8_t size)
{
	if (f->f_pos >= NR_MOUNTS - 1)
		return lxfs_readdir(f, name, size);
	strlcpy_P(name, mounts[f->f_pos++].path + 1, size);
	return 1;
}

static const struct file_operations root_fops = {
	.readdir = root_readdir,
};

/*
 * The mount @path falls under, with *rest set past the prefix; under "/"
 * the whole path is the rest.
 *
 * 返回 @path 所属的挂载点, 并把 *rest 设为前缀之后的部分; 属于 "/" 时整个
 * 路径都是 *rest.
 */
static const struct mount *lookup_mount(const char *path, const char **rest)
{
	uint8_t i;
	size_t len;

	for (i = 0; i < NR_MOUNTS; i++) {
		if (mounts[i].path == root_path) {
			*rest = path;
			return &mounts[i];
		}
		len = strlen_P(mounts[i].path);
		if (strncmp_P(path, mounts[i].path, len))
			continue;
		if (path[len] != '\0' && path[len] != '/')
			continue;
		*rest = path + len;
		return &mounts[i];
	}
	return NULL;
}

static struct file *get_file(int fd)
{
	if (fd < 0 || fd >= MAX_FDS || !files[fd].f_op)
		return NULL;
	return &files[fd];
}

/**
 * vfs_open() - open a path into a struct file
 * @f: the file to fill in, which need not be in the descriptor table
 * @path: the path
 * @flags: open() flags
 *
 * The kernel opens a program's code this way.
 *
 * 以 @flags 打开 @path, 填入 @f; @f 不必位于描述符表中, 内核以这种方式打开
 * 程序的代码.
 *
 * Return: 0, or a negative errno with f->f_op NULL.
 * 成功时返回 0, 失败时返回负的 errno, 并使 f->f_op 为 NULL.
 */
int vfs_open(struct file *f, const char *path, uint16_t flags)
{
	const struct mount *mnt;
	const char *rest;
	int err;

	path = absolute(path);
	if (!path)
		return -ENAMETOOLONG;
	f->f_pos = 0;
	f->f_flags = flags;
	f->f_size = 0;
	f->private_data = 0;

	if (!strcmp_P(path, PSTR("/"))) {
		f->f_op = &root_fops;
		return 0;
	}
	mnt = lookup_mount(path, &rest);
	err = mnt->open(f, rest, flags);
	if (err < 0) {
		f->f_op = NULL;
		return err;
	}
	if ((flags & O_DIRECTORY) && !f->f_op->readdir) {
		if (f->f_op->release)
			f->f_op->release(f);
		f->f_op = NULL;
		return -ENOTDIR;
	}
	return 0;
}

/**
 * open() - open a file into the lowest free descriptor
 * @path: the path
 * @flags: O_* flags
 *
 * 以 @flags 打开 @path, 放入编号最小的空闲描述符.
 *
 * Return: the descriptor, or a negative errno.
 * 返回描述符, 失败时返回负的 errno.
 */
int open(const char *path, uint16_t flags)
{
	int fd, err;

	for (fd = 0; fd < MAX_FDS && files[fd].f_op; fd++)
		;
	if (fd == MAX_FDS)
		return -EMFILE;
	err = vfs_open(&files[fd], path, flags);
	return err < 0 ? err : fd;
}

/**
 * close() - close a descriptor
 * @fd: the descriptor
 *
 * 关闭描述符 @fd.
 *
 * Return: 0, or -EBADF.
 * 成功时返回 0, @fd 无效时返回 -EBADF.
 */
int close(int fd)
{
	struct file *f = get_file(fd);

	if (!f)
		return -EBADF;
	if (f->f_op->release)
		f->f_op->release(f);
	f->f_op = NULL;
	return 0;
}

/**
 * read() - read from a descriptor
 * @fd: the descriptor
 * @buf: where to store the bytes
 * @count: most bytes to read
 *
 * 从描述符 @fd 读取至多 @count 字节到 @buf.
 *
 * Return: bytes read, 0 at the end of a file, or a negative errno.
 * 返回读到的字节数, 文件末尾时返回 0, 失败时返回负的 errno.
 */
int read(int fd, char *buf, uint8_t count)
{
	struct file *f = get_file(fd);

	if (!f || (f->f_flags & O_ACCMODE) == O_WRONLY)
		return -EBADF;
	if (!f->f_op->read)
		return f->f_op->readdir ? -EISDIR : -EINVAL;
	return f->f_op->read(f, buf, count);
}

/**
 * write() - write to a descriptor
 * @fd: the descriptor
 * @buf: the bytes
 * @count: how many
 *
 * 把 @buf 中的 @count 字节写入描述符 @fd.
 *
 * Return: bytes written, or a negative errno.
 * 返回写入的字节数, 失败时返回负的 errno.
 */
int write(int fd, const char *buf, uint8_t count)
{
	struct file *f = get_file(fd);

	if (!f || (f->f_flags & O_ACCMODE) == O_RDONLY)
		return -EBADF;
	if (!f->f_op->write)
		return f->f_op->readdir ? -EISDIR : -EINVAL;
	return f->f_op->write(f, buf, count);
}

/**
 * readdir() - the next name in a directory
 * @fd: the directory's descriptor
 * @name: where to store the name
 * @size: bytes in @name
 *
 * 把目录描述符 @fd 中的下一个名字写入 @name, @size 是 @name
 * 的字节数.
 *
 * Return: 1, 0 after the last name, or a negative errno.
 * 成功时返回 1, 最后一个名字之后返回 0, 失败时返回负的 errno.
 */
int readdir(int fd, char *name, uint8_t size)
{
	struct file *f = get_file(fd);

	if (!f)
		return -EBADF;
	if (!f->f_op->readdir)
		return -ENOTDIR;
	return f->f_op->readdir(f, name, size);
}

/**
 * unlink() - remove a file
 * @path: the path
 *
 * 删除文件 @path.
 *
 * Return: 0, or a negative errno.
 * 成功时返回 0, 失败时返回负的 errno.
 */
int unlink(const char *path)
{
	const struct mount *mnt;
	const char *rest;

	path = absolute(path);
	if (!path)
		return -ENAMETOOLONG;
	mnt = lookup_mount(path, &rest);
	if (!rest[0] || !strcmp_P(rest, PSTR("/")))
		return -EISDIR;
	if (!mnt->unlink)
		return -EROFS;
	return mnt->unlink(rest);
}

/**
 * dup2() - make a descriptor refer to the file another refers to
 * @oldfd: the descriptor to copy
 * @newfd: the descriptor to set, closed first if open
 *
 * Unlike Linux, each descriptor gets its own copy of the file, with its
 * own position, so a file is to be used through one of them only.
 *
 * 使 @newfd 指向 @oldfd 所指的文件, @newfd 已打开时先关闭. 与 Linux 不同,
 * 两个描述符各得到文件的一份副本, 各有自己的位置, 因此一个文件只应通过其中
 * 一个描述符使用.
 *
 * Return: @newfd, or -EBADF.  返回 @newfd, 描述符无效时返回 -EBADF.
 */
int dup2(int oldfd, int newfd)
{
	struct file *f = get_file(oldfd);

	if (!f || newfd < 0 || newfd >= MAX_FDS)
		return -EBADF;
	if (oldfd == newfd)
		return newfd;
	close(newfd);
	files[newfd] = *f;
	return newfd;
}
