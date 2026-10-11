// SPDX-License-Identifier: GPL-2.0-only
#include <leonix/printk.h>
#include <leonix/stdio.h>
#include <leonix/unistd.h>

#include "bin.h"

/* Every record the kernel log still holds, oldest first. */
int dmesg_main(int argc, char **argv)
{
	struct log_record rec;
	uint16_t newest = log_newest();
	uint16_t seq = newest > LOG_RECORDS ? newest - LOG_RECORDS + 1 : 1;

	(void)argc;
	(void)argv;
	for (; seq && seq <= newest; seq++)
		if (log_read(seq, &rec) == 0)
			dprintf(STDOUT_FILENO, "[%5u] %s\n", rec.secs, rec.text);
	return 0;
}
