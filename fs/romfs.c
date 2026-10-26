// SPDX-License-Identifier: GPL-2.0-only
/*
 * /bin: a read-only filesystem in flash, packed by tools/mkromfs.py from
 * the programs Leonix_coreutils builds, as Linux carries its first
 * programs in an initramfs.
 *
 * /bin: flash 中的只读文件系统, 由 tools/mkromfs.py 将 Leonix_coreutils
 * 构建出的程序打包而成, 与 Linux 把最初的程序放在 initramfs 中的做法相同.
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

/**
 * romfs_open() - open /bin or a program in it
 * @f: the file to fill in
 * @rest: the path after "/bin": "" or "/" for the directory, or "/name"
 * @flags: open() flags
 *
 * 打开 /bin 或其中的程序. @rest 是 "/bin" 之后的路径, "" 或 "/" 表示目录
 * 本身, "/name" 表示程序.
 *
 * Return: 0, -EROFS for writing or O_CREAT, or -ENOENT if no program has
 * the name.  成功时返回 0, 以写方式或带 O_CREAT 打开时返回 -EROFS, 没有
 * 同名程序时返回 -ENOENT.
 */
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
