// SPDX-License-Identifier: GPL-2.0-only
#include <leonix/stdio.h>
#include <leonix/unistd.h>

#include "bin.h"

int help_main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	dprintf(STDOUT_FILENO, "programs: ls /bin\n");
	dprintf(STDOUT_FILENO, "redirect: cmd > file, cmd >> file\n");
	return 0;
}
