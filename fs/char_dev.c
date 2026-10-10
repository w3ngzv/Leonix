// SPDX-License-Identifier: GPL-2.0-only
/*
 * Character devices by name.  Linux keys them by major and minor number
 * and finds the name through a device node in /dev; with no filesystem
 * here, the name is the key.  Drivers register before sched_start(), so
 * the table needs no lock.
 */
#include <avr/pgmspace.h>
#include <stddef.h>
#include <string.h>

#include <leonix/fs.h>

struct chrdev {
	const char *name;	/* in flash */
	const struct file_operations *fops;
};

static struct chrdev chrdevs[MAX_CHRDEVS];
static uint8_t nr_chrdevs;

/* @name is a string in flash.  Returns 0, or -1 if the table is full. */
int register_chrdev(const char *name, const struct file_operations *fops)
{
	if (nr_chrdevs == MAX_CHRDEVS)
		return -1;
	chrdevs[nr_chrdevs].name = name;
	chrdevs[nr_chrdevs].fops = fops;
	nr_chrdevs++;
	return 0;
}

/* @name is a string in SRAM.  Returns NULL if no device has that name. */
const struct file_operations *chrdev_get(const char *name)
{
	uint8_t i;

	for (i = 0; i < nr_chrdevs; i++)
		if (!strcmp_P(name, chrdevs[i].name))
			return chrdevs[i].fops;
	return NULL;
}

/* The name of device @index, in flash, or NULL past the last one. */
const char *chrdev_name(uint8_t index)
{
	return index < nr_chrdevs ? chrdevs[index].name : NULL;
}
