// SPDX-License-Identifier: GPL-2.0-only
/*
 * One namespace for every file, the way Linux's VFS joins its
 * filesystems.  Paths are absolute.  A path is handed to the filesystem
 * whose mount point is its longest prefix ending at a '/'; there are no
 * directories inside filesystems, so that is all the walk there is.
 *
 * The root directory lists the mount points.
 */
#include <avr/pgmspace.h>
#include <stddef.h>
#include <string.h>

#include <leonix/errno.h>
#include <leonix/unistd.h>

static const char dev_path[] PROGMEM = "/dev";

static const struct mount mounts[] = {
	{ dev_path, devfs_open, NULL },
};

#define NR_MOUNTS	(sizeof(mounts) / sizeof(mounts[0]))

static struct file files[MAX_FDS];

static int root_readdir(struct file *f, char *name, uint8_t size)
{
	if (f->f_pos >= NR_MOUNTS)
		return 0;
	strlcpy_P(name, mounts[f->f_pos++].path + 1, size);
	return 1;
}

static const struct file_operations root_fops = {
	.readdir = root_readdir,
};

/*
 * The mount point @path falls under, with *rest set past the prefix.
 * NULL for the root directory itself or a path under no mount point.
 */
static const struct mount *lookup_mount(const char *path, const char **rest)
{
	uint8_t i;
	size_t len;

	for (i = 0; i < NR_MOUNTS; i++) {
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

int open(const char *path, uint16_t flags)
{
	const struct mount *mnt;
	const char *rest;
	struct file *f;
	int fd, err;

	if (path[0] != '/')
		return -ENOENT;
	for (fd = 0; fd < MAX_FDS && files[fd].f_op; fd++)
		;
	if (fd == MAX_FDS)
		return -EMFILE;
	f = &files[fd];
	f->f_pos = 0;
	f->f_flags = flags;
	f->private_data = 0;

	if (!strcmp_P(path, PSTR("/"))) {
		f->f_op = &root_fops;
		return fd;
	}
	mnt = lookup_mount(path, &rest);
	if (!mnt)
		return -ENOENT;
	err = mnt->open(f, rest, flags);
	if (err < 0) {
		f->f_op = NULL;
		return err;
	}
	if ((flags & O_DIRECTORY) && !f->f_op->readdir) {
		close(fd);
		return -ENOTDIR;
	}
	return fd;
}

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

int read(int fd, char *buf, uint8_t count)
{
	struct file *f = get_file(fd);

	if (!f || (f->f_flags & O_ACCMODE) == O_WRONLY)
		return -EBADF;
	if (!f->f_op->read)
		return f->f_op->readdir ? -EISDIR : -EINVAL;
	return f->f_op->read(f, buf, count);
}

int write(int fd, const char *buf, uint8_t count)
{
	struct file *f = get_file(fd);

	if (!f || (f->f_flags & O_ACCMODE) == O_RDONLY)
		return -EBADF;
	if (!f->f_op->write)
		return f->f_op->readdir ? -EISDIR : -EINVAL;
	return f->f_op->write(f, buf, count);
}

int readdir(int fd, char *name, uint8_t size)
{
	struct file *f = get_file(fd);

	if (!f)
		return -EBADF;
	if (!f->f_op->readdir)
		return -ENOTDIR;
	return f->f_op->readdir(f, name, size);
}

int unlink(const char *path)
{
	const struct mount *mnt;
	const char *rest;

	mnt = lookup_mount(path, &rest);
	if (!mnt || !rest[0])
		return -ENOENT;
	if (!mnt->unlink)
		return -EROFS;
	return mnt->unlink(rest);
}

/*
 * As in Linux, @newfd is closed first if open.  Unlike Linux, the two
 * descriptors get copies of the file, each with its own position, so a
 * file is to be read or written through one of them only.
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
