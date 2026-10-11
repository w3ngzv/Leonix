// SPDX-License-Identifier: GPL-2.0-only
#include <leonix/lxfs.h>
#include <leonix/stdio.h>
#include <leonix/unistd.h>

#include "bin.h"

/* Free space on the root filesystem; whole blocks only. */
int df_main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	dprintf(STDOUT_FILENO, "%u bytes free\n", lxfs_free());
	return 0;
}
