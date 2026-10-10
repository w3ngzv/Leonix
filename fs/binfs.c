// SPDX-License-Identifier: GPL-2.0-only
/*
 * /bin: the programs linked into the image, as files.
 *
 * AVR executes from flash only and the application cannot write flash,
 * so a program cannot be loaded at run time; every program is in the
 * image, and /bin lists the table of them.  Opening one works, so that
 * it can be listed and tested for, but there is no content to read.
 */
#include <avr/pgmspace.h>
#include <string.h>

#include <leonix/binfs.h>
#include <leonix/errno.h>

/* The index of program @name in programs[], or -ENOENT. */
static int find(const char *name)
{
	uint8_t i;

	for (i = 0; i < nr_programs; i++)
		if (!strcmp_P(name, pgm_read_ptr(&programs[i].name)))
			return i;
	return -ENOENT;
}

static int binfs_readdir(struct file *f, char *name, uint8_t size)
{
	if (f->f_pos >= nr_programs)
		return 0;
	strlcpy_P(name, pgm_read_ptr(&programs[f->f_pos++].name), size);
	return 1;
}

static const struct file_operations binfs_dir_fops = {
	.readdir = binfs_readdir,
};

static const struct file_operations binfs_file_fops;

/* Mounted at /bin by fs/vfs.c.  @rest is "" or "/", or "/name". */
int binfs_open(struct file *f, const char *rest, uint16_t flags)
{
	int index;

	if (!rest[0] || !strcmp_P(rest, PSTR("/"))) {
		f->f_op = &binfs_dir_fops;
		return 0;
	}
	if ((flags & O_ACCMODE) != O_RDONLY || (flags & O_CREAT))
		return -EROFS;
	index = find(rest + 1);
	if (index < 0)
		return index;
	f->f_op = &binfs_file_fops;
	f->private_data = index;
	return 0;
}

/*
 * Run @argv[0], a name in /bin, and return its exit status, or -ENOENT.
 * The program runs in the caller's task, on its stack, with its file
 * descriptors: the effect of fork(), execve() and wait() in Linux, with
 * no process of its own.
 */
int exec(int argc, char **argv)
{
	int (*entry)(int, char **);
	int index = find(argv[0]);

	if (index < 0)
		return index;
	entry = pgm_read_ptr(&programs[index].main);
	return entry(argc, argv);
}
