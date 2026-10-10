// SPDX-License-Identifier: GPL-2.0-only
/*
 * One namespace for every file, the way Linux's VFS joins its
 * filesystems.  A path not starting with '/' is taken from the root,
 * which is the only working directory there is.  A path goes to the first mount
 * point that is a prefix of it ending at a '/', with "/" last to catch
 * the rest; there are no directories inside filesystems, so that is all
 * the walk there is.
 *
 * The root directory lists the other mount points, then the files of
 * lxfs, which is always the root filesystem.
 */
#include <avr/pgmspace.h>
#include <stddef.h>
#include <string.h>

#include <leonix/binfs.h>
#include <leonix/errno.h>
#include <leonix/lxfs.h>
#include <leonix/tty.h>
#include <leonix/unistd.h>

static const char dev_path[] PROGMEM = "/dev";
static const char bin_path[] PROGMEM = "/bin";
static const char root_path[] PROGMEM = "/";

static const struct mount mounts[] = {
	{ bin_path, binfs_open, NULL },
	{ dev_path, devfs_open, NULL },
	{ root_path, lxfs_open, lxfs_unlink },	/* last */
};

#define NR_MOUNTS	(sizeof(mounts) / sizeof(mounts[0]))

static struct file files[MAX_FDS];

/* A relative path with the root put in front.  One task uses the VFS. */
static char abs_path[TTY_LINE + 2];

static const char *absolute(const char *path)
{
	if (path[0] == '/')
		return path;
	abs_path[0] = '/';
	strlcpy(abs_path + 1, path, sizeof(abs_path) - 1);
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
 * The mount point @path falls under, with *rest set past the prefix;
 * for "/" the whole path is the rest.
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

int open(const char *path, uint16_t flags)
{
	const struct mount *mnt;
	const char *rest;
	struct file *f;
	int fd, err;

	path = absolute(path);
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

	path = absolute(path);
	mnt = lookup_mount(path, &rest);
	if (!rest[0] || !strcmp_P(rest, PSTR("/")))
		return -EISDIR;
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
