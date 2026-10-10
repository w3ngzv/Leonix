// SPDX-License-Identifier: GPL-2.0-only
#include <string.h>

#include <leonix/unistd.h>

#include "bin.h"

/* The arguments, a space between each, then a newline. */
int echo_main(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (i > 1)
			write(STDOUT_FILENO, " ", 1);
		write(STDOUT_FILENO, argv[i], strlen(argv[i]));
	}
	write(STDOUT_FILENO, "\n", 1);
	return 0;
}
