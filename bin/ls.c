// SPDX-License-Identifier: GPL-2.0-only
#include <leonix/stdio.h>
#include <leonix/unistd.h>

#include "bin.h"

/* The names in each directory given, one per line; / without one. */
int ls_main(int argc, char **argv)
{
	char name[NAME_MAX + 1];
	const char *path = argc > 1 ? argv[1] : "/";
	int fd;

	fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		dprintf(STDOUT_FILENO, "ls: %s: error %d\n", path, -fd);
		return 1;
	}
	while (readdir(fd, name, sizeof(name)) > 0)
		dprintf(STDOUT_FILENO, "%s\n", name);
	close(fd);
	return 0;
}
