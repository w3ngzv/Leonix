/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_ROMFS_H
#define _LEONIX_ROMFS_H

#include <stdint.h>

#include <leonix/fs.h>

/*
 * one file, all in flash; the table comes from tools/mkromfs.py
 * 一个文件, 全部存放在 flash 中; 该表由 tools/mkromfs.py 生成
 */
struct romfs_entry {
	const char *name;
	const uint8_t *data;
	uint16_t size;
};

extern const struct romfs_entry romfs_entries[];
extern const uint8_t romfs_count;

int romfs_open(struct file *f, const char *rest, uint16_t flags);

#endif /* _LEONIX_ROMFS_H */
