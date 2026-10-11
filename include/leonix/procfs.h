/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_PROCFS_H
#define _LEONIX_PROCFS_H

#include <leonix/fs.h>

int procfs_open(struct file *f, const char *rest, uint16_t flags);

#endif /* _LEONIX_PROCFS_H */
