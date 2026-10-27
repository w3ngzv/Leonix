// SPDX-License-Identifier: GPL-2.0-only
/*
 * The on-chip EEPROM as the device under lxfs.
 *
 * avr-libc disables interrupts only for the timed EEMPE and EEPE writes
 * (avr-libc 2.0, eeprom_update_r18), so other tasks run while the caller
 * spins on EEPE, about 3.3 ms a byte (datasheet table 5-3).
 * eeprom_update_block() skips bytes that already hold the value.
 *
 * 片上 EEPROM, 作为 lxfs 之下的设备. avr-libc 只在定时写入 EEMPE 与 EEPE
 * 的两条指令期间关中断 (avr-libc 2.0, eeprom_update_r18), 调用者在 EEPE
 * 上忙等时其他任务照常运行, 每写一个字节约 3.3 ms (数据手册表 5-3).
 * eeprom_update_block() 跳过已是目标值的字节.
 */
#include <avr/eeprom.h>

#include <leonix/module.h>
#include <leonix/printk.h>
#include <leonix/lxfs.h>

static void eeprom_dev_read(uint16_t off, void *buf, uint16_t len)
{
	eeprom_read_block(buf, (const void *)off, len);
}

/*
 * TODO: wait for each byte on the EE READY interrupt with a wait queue
 * rather than spinning on EEPE, which counts as busy time.
 *
 * TODO: wear levelling for the directory; lxfs writes the size and first
 * block of a file to the same bytes every time, which costs data blocks
 * to rotate.
 *
 * 待办: 以 EE READY 中断与等待队列等待每个字节写完, 不再在 EEPE 上自旋,
 * 自旋的时间被计为忙碌.
 *
 * 待办: 目录的磨损均衡; lxfs 每次都把文件的大小与首块写在同样的字节上,
 * 轮换这些字节需要占用数据块.
 */
static void eeprom_dev_write(uint16_t off, const void *buf, uint16_t len)
{
	eeprom_update_block(buf, (void *)off, len);
}

/*
 * mount lxfs on the EEPROM, formatting it if it holds none
 * 在 EEPROM 上挂载 lxfs, 其中没有 lxfs 时先格式化
 */
static int eeprom_fs_init(void)
{
	switch (lxfs_init(eeprom_dev_read, eeprom_dev_write)) {
	case LXFS_CLEAN:
		break;
	case LXFS_FORMATTED:
		pr_notice("mkfs lxfs");
		break;
	case LXFS_REPAIRED:
		pr_warn("lxfs repaired");
		break;
	default:
		pr_err("lxfs: unknown");
		break;
	}
	return 0;
}
fs_initcall(eeprom_fs_init);
