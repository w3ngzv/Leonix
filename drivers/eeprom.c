// SPDX-License-Identifier: GPL-2.0-only
/*
 * The on-chip EEPROM as the device under lxfs.
 *
 * avr-libc's routines wait on EEPE with interrupts as the caller had
 * them, and disable them only for the two timed writes of EEMPE and EEPE
 * (seen in the avr-libc 2.0 disassembly of eeprom_update_r18).  The
 * caller spins about 3.3 ms per byte written, datasheet table 5-3,
 * while other tasks still run.  eeprom_update_block() skips bytes that already
 * hold the value, which saves both the time and the wear.
 */
#include <avr/eeprom.h>

#include <leonix/module.h>
#include <leonix/printk.h>
#include <leonix/lxfs.h>

static void eeprom_dev_read(uint16_t off, void *buf, uint16_t len)
{
	eeprom_read_block(buf, (const void *)off, len);
}

static void eeprom_dev_write(uint16_t off, const void *buf, uint16_t len)
{
	eeprom_update_block(buf, (void *)off, len);
}

/* Mount lxfs on the EEPROM, formatting it if it holds none. */
static int eeprom_fs_init(void)
{
	switch (lxfs_init(eeprom_dev_read, eeprom_dev_write)) {
	case LXFS_CLEAN:
		break;
	case LXFS_FORMATTED:
		printk("mkfs lxfs");
		break;
	case LXFS_REPAIRED:
		printk("lxfs repaired");
		break;
	default:
		printk("lxfs: unknown version, not mounted");
		break;
	}
	return 0;
}
fs_initcall(eeprom_fs_init);
