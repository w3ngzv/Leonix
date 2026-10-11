/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_ROMFS_H
#define _LEONIX_ROMFS_H

#include <stdint.h>

#include <leonix/fs.h>

/* One file, everything in flash.  The table comes from tools/mkromfs.py. */
struct romfs_entry {
	const char *name;
	const uint8_t *data;
	uint16_t size;
};

extern const struct romfs_entry romfs_entries[];
extern const uint8_t romfs_count;

int romfs_open(struct file *f, const char *rest, uint16_t flags);

#endif /* _LEONIX_ROMFS_H */
