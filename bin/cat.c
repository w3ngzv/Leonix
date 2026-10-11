// SPDX-License-Identifier: GPL-2.0-only
#include <leonix/stdio.h>
#include <leonix/unistd.h>

#include "bin.h"

/*
 * Small, because cat runs on the shell's stack and the deepest path
 * through it, an error message, formats a line below this buffer.
 */
#define CAT_CHUNK	16

/* Copy each file named to standard output. */
int cat_main(int argc, char **argv)
{
	char buf[CAT_CHUNK];
	int fd, n, i, status = 0;

	for (i = 1; i < argc; i++) {
		fd = open(argv[i], O_RDONLY);
		if (fd < 0) {
			dprintf(STDOUT_FILENO, "cat: %s: error %d\n", argv[i], -fd);
			status = 1;
			continue;
		}
		while ((n = read(fd, buf, sizeof(buf))) > 0)
			write(STDOUT_FILENO, buf, n);
		if (n < 0) {
			dprintf(STDOUT_FILENO, "cat: %s: error %d\n", argv[i], -n);
			status = 1;
		}
		close(fd);
	}
	return status;
}
