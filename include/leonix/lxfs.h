/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_LXFS_H
#define _LEONIX_LXFS_H

#include <stdint.h>

#include <leonix/fs.h>

/*
 * bytes of the device lxfs lives on, the AVR's whole EEPROM
 * lxfs 所在设备的字节数, 即 AVR 的全部 EEPROM
 */
#define LXFS_SIZE		1024

/* Move @len bytes between @buf and the device at byte @off. */
typedef void (*lxfs_read_fn)(uint16_t off, void *buf, uint16_t len);
typedef void (*lxfs_write_fn)(uint16_t off, const void *buf, uint16_t len);

/* lxfs_init() returns one of these, or a negative errno. */
#define LXFS_CLEAN		0
#define LXFS_FORMATTED		1
#define LXFS_REPAIRED		2

int lxfs_init(lxfs_read_fn read, lxfs_write_fn write);
int lxfs_open(struct file *f, const char *rest, uint16_t flags);
int lxfs_readdir(struct file *f, char *name, uint8_t size);
int lxfs_unlink(const char *rest);
uint16_t lxfs_free(void);

#endif /* _LEONIX_LXFS_H */
