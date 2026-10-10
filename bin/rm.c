// SPDX-License-Identifier: GPL-2.0-only
#include <leonix/stdio.h>
#include <leonix/unistd.h>

#include "bin.h"

int rm_main(int argc, char **argv)
{
	int i, err, status = 0;

	for (i = 1; i < argc; i++) {
		err = unlink(argv[i]);
		if (err < 0) {
			dprintf(STDOUT_FILENO, "rm: %s: error %d\n", argv[i], -err);
			status = 1;
		}
	}
	return status;
}
