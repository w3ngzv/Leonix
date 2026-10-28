// SPDX-License-Identifier: GPL-2.0-only
/*
 * The STM32F103's own Flash as the device under lxfs, in place of the
 * AVR's EEPROM.
 *
 * Flash is erased a 1 KiB page at a time, 20 to 40 ms each, and a page
 * stands 10 000 erases (DS5319 tables 28 and 29); it is programmed a
 * half-word at a time, only where the half-word reads 0xffff (PM0075
 * 2.3.3).  Erasing a page for every write of lxfs would wear it out and
 * would lose the whole filesystem to a reset during the erase.  So the
 * area the linker script leaves after the kernel, __lxfs_flash_start to
 * __lxfs_flash_end, holds slots of two pages each:
 *
 *   page 0   an image of the 1 KiB device
 *   page 1   a header, generation and CRC-32 of the image, then a
 *            journal of writes made since: offset, length, the bytes,
 *            and a check half-word programmed last
 *
 * A write goes to RAM first, then to the end of the journal of the
 * current slot.  Once the journal is full, the RAM copy becomes the
 * image of the next slot, under the next generation, and the journal
 * starts over there; the header is programmed after the image, so the
 * old slot counts until the new one is complete.  At mount the slot
 * with the highest valid generation is read and its journal replayed up
 * to the first record whose check fails, which a reset in the middle of
 * programming leaves.  Each write lxfs makes is therefore kept whole or
 * not at all, which is more than the one byte lxfs relies on.
 *
 * The CPU stalls on any Flash read while a program or erase runs
 * (PM0075 2.3.3), interrupt handlers included: a journal record costs
 * a few times 52.5 us, a new slot up to about 120 ms, during which
 * ticks and received bytes may be lost.  If programming fails, under
 * QEMU for one, lxfs carries on in RAM and is gone at the next reset.
 *
 * STM32F103 自身的 Flash, 作为 lxfs 之下的设备, 代替 AVR 的 EEPROM.
 *
 * Flash 以 1 KiB 的页为单位擦除, 每页 20 至 40 ms, 一页可承受 10 000 次擦除
 * (DS5319 表 28 与表 29); 编程以半字为单位, 只能写入读出为 0xffff 的半字
 * (PM0075 2.3.3). 若 lxfs 每次写入都擦除一页, 该页很快磨损, 擦除期间复位还会
 * 丢失整个文件系统. 因此链接脚本在内核之后留出的区域, 即 __lxfs_flash_start 至
 * __lxfs_flash_end, 分为若干个槽, 每槽两页:
 *
 *   第 0 页  1 KiB 设备的映像
 *   第 1 页  文件头, 即代数与映像的 CRC-32, 其后是此后各次写入的日志: 偏移、
 *            长度、字节, 以及最后编程的校验半字
 *
 * 写入先进入 RAM, 再追加到当前槽日志的末尾. 日志写满之后, RAM 中的副本以下一
 * 代数成为下一个槽的映像, 日志在那里重新开始; 文件头在映像之后编程, 因此新槽
 * 完成之前旧槽始终有效. 挂载时读取代数最高的有效槽, 并重放其日志, 直到第一条
 * 校验失败的记录为止, 编程中途的复位会留下这样的记录. lxfs 的每次写入因此要么
 * 完整保留, 要么完全没有, 强于 lxfs 所依赖的单字节保证.
 *
 * 编程或擦除进行期间, CPU 对 Flash 的任何读取都会停顿 (PM0075 2.3.3), 中断处理
 * 程序也不例外: 一条日志记录耗时为 52.5 us 的数倍, 一个新槽至多约 120 ms, 其间
 * 可能丢失节拍与收到的字节. 编程失败时 (例如在 QEMU 中), lxfs 继续在 RAM 中
 * 运行, 下次复位后即消失.
 */
#include <stddef.h>
#include <stdint.h>

#include <asm/sections.h>
#include <asm/stm32f1.h>

#include <leonix/lxfs.h>
#include <leonix/module.h>
#include <leonix/printk.h>
#include <leonix/string.h>

#define PAGE_BYTES	1024u
#define SLOT_BYTES	(2 * PAGE_BYTES)
#define ERASED_WORD	0xffffffffu
#define ERASED_HALF	0xffffu

/*
 * the CRC-32 of IEEE 802.3, bit-reflected
 * IEEE 802.3 的 CRC-32, 按位反射
 */
#define CRC32_POLY	0xedb88320u

struct slot_header {
	/* of the image, then of gen; 映像与 gen 的 CRC */
	uint32_t crc;
	uint32_t gen;		/* programmed last; 最后编程 */
};

#define HEADER_BYTES	sizeof(struct slot_header)

/*
 * A journal record: offset and length half-words, the bytes padded to
 * a half-word with 0xff, and the check.
 *
 * 一条日志记录: 偏移与长度两个半字, 以 0xff 补齐到半字的字节, 以及校验半字.
 */
#define REC_HEAD	4u
#define REC_BYTES(len)	(REC_HEAD + (((len) + 1u) & ~1u) + 2u)

_Static_assert(LXFS_SIZE == PAGE_BYTES, "an image fills one page");

static uint8_t image[LXFS_SIZE];
static uint8_t nr_slots;
static uint8_t slot;		/* the current slot; 当前槽 */
static uint32_t gen;		/* its generation; 当前槽的代数 */
/* next free byte of its page 1; 第 1 页的下一个空闲字节 */
static uint16_t log_end;
static uint8_t flash_ok;

static const uint8_t *slot_image(uint8_t n)
{
	return __lxfs_flash_start + n * SLOT_BYTES;
}

static const uint8_t *slot_log(uint8_t n)
{
	return slot_image(n) + PAGE_BYTES;
}

static uint32_t crc32(uint32_t crc, const void *p, uint16_t len)
{
	const uint8_t *b = p;
	uint8_t k;

	crc = ~crc;
	while (len--) {
		crc ^= *b++;
		for (k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (CRC32_POLY & -(crc & 1));
	}
	return ~crc;
}

static uint32_t image_crc(const uint8_t *data, uint32_t g)
{
	return crc32(crc32(0, data, LXFS_SIZE), &g, sizeof(g));
}

/*
 * The check of a record, never 0xffff, so that an erased half-word
 * cannot pass for it.
 *
 * 记录的校验值, 永远不是 0xffff, 使擦除状态的半字无法冒充校验值.
 */
static uint16_t record_check(uint16_t off, uint16_t len, const uint8_t *data)
{
	uint16_t head[2] = { off, len };
	uint16_t check = crc32(crc32(0, head, sizeof(head)), data, len);

	return check == ERASED_HALF ? ERASED_HALF - 1 : check;
}

static void flash_wait(void)
{
	while (FLASH_SR & FLASH_SR_BSY)
		;
}

/*
 * PM0075 2.3.2; a wrong key would lock the FPEC until reset
 * PM0075 2.3.2; 错误的密钥会锁住 FPEC 直到复位
 */
static void flash_unlock(void)
{
	if (FLASH_CR & FLASH_CR_LOCK) {
		FLASH_KEYR = FLASH_KEY1;
		FLASH_KEYR = FLASH_KEY2;
	}
}

static void flash_lock(void)
{
	FLASH_CR |= FLASH_CR_LOCK;
}

/*
 * the page erase of PM0075 2.3.4, then the check it asks for
 * PM0075 2.3.4 的页擦除, 随后按其要求检查
 */
static int flash_erase(const uint8_t *page)
{
	const uint32_t *w = (const uint32_t *)page;
	uint16_t i;

	flash_wait();
	FLASH_CR |= FLASH_CR_PER;
	FLASH_AR = (uint32_t)(uintptr_t)page;
	FLASH_CR |= FLASH_CR_STRT;
	flash_wait();
	FLASH_CR &= ~FLASH_CR_PER;
	FLASH_SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPRTERR;
	for (i = 0; i < PAGE_BYTES / sizeof(*w); i++)
		if (w[i] != ERASED_WORD)
			return -1;
	return 0;
}

/*
 * the half-word programming of PM0075 2.3.3, then the check
 * PM0075 2.3.3 的半字编程, 随后检查
 */
static int flash_program(const uint8_t *addr, uint16_t value)
{
	volatile uint16_t *half = (volatile uint16_t *)addr;

	flash_wait();
	FLASH_CR |= FLASH_CR_PG;
	*half = value;
	flash_wait();
	FLASH_CR &= ~FLASH_CR_PG;
	FLASH_SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPRTERR;
	return *half == value ? 0 : -1;
}

/*
 * Program @len bytes from @data at @addr, which is erased, the last odd
 * byte padded.  Half-words of 0xffff are already there and are skipped.
 *
 * 从已擦除的 @addr 起编程 @data 中的 @len 字节, 末尾的奇数字节补齐. 值为
 * 0xffff 的半字已经存在, 因此跳过.
 */
static int flash_program_bytes(const uint8_t *addr, const uint8_t *data,
			       uint16_t len)
{
	uint16_t i, half;

	for (i = 0; i < len; i += 2) {
		half = data[i] | (i + 1 < len ? data[i + 1] : 0xff) << 8;
		if (half != ERASED_HALF && flash_program(addr + i, half) < 0)
			return -1;
	}
	return 0;
}

/*
 * Append image[@off..@off + @len) to the journal of the current slot.
 * Returns -1 when the record does not fit or programming fails, and the
 * journal then counts as full.
 *
 * 把 image[@off..@off + @len) 追加到当前槽的日志. 记录放不下或编程失败时返回
 * -1, 此后日志视为已满.
 */
static int journal_append(uint16_t off, uint16_t len)
{
	const uint8_t *rec = slot_log(slot) + log_end;
	uint16_t data_bytes = REC_BYTES(len) - REC_HEAD - 2;
	int err = -1;

	if (log_end + REC_BYTES(len) > PAGE_BYTES)
		return -1;
	flash_unlock();
	if (flash_program(rec, off) == 0 &&
	    flash_program(rec + 2, len) == 0 &&
	    flash_program_bytes(rec + REC_HEAD, image + off, len) == 0 &&
	    flash_program(rec + REC_HEAD + data_bytes,
			  record_check(off, len, image + off)) == 0)
		err = 0;
	flash_lock();
	log_end = err ? PAGE_BYTES : log_end + REC_BYTES(len);
	return err;
}

/*
 * Write the RAM copy as the image of the next slot.  Page 1 is erased
 * first, so the slot is invalid until its header is programmed at the
 * end, gen last.
 *
 * 把 RAM 中的副本写为下一个槽的映像. 先擦除第 1 页, 因此在最后编程文件头 (gen
 * 在最后) 之前该槽无效.
 */
static int slot_write_next(void)
{
	uint8_t next = (slot + 1) % nr_slots;
	const uint8_t *log = slot_log(next);
	struct slot_header hdr = { image_crc(image, gen + 1), gen + 1 };
	int err = -1;

	flash_unlock();
	if (flash_erase(log) == 0 && flash_erase(slot_image(next)) == 0 &&
	    flash_program_bytes(slot_image(next), image, LXFS_SIZE) == 0 &&
	    flash_program_bytes(log, (const uint8_t *)&hdr, HEADER_BYTES) == 0)
		err = 0;
	flash_lock();
	if (err)
		return err;
	slot = next;
	gen = hdr.gen;
	log_end = HEADER_BYTES;
	return 0;
}

static void flash_dev_read(uint16_t off, void *buf, uint16_t len)
{
	memcpy(buf, image + off, len);
}

static void flash_dev_write(uint16_t off, const void *buf, uint16_t len)
{
	if (!memcmp(image + off, buf, len))
		return;
	memcpy(image + off, buf, len);
	if (!flash_ok)
		return;
	if (journal_append(off, len) < 0 && slot_write_next() < 0) {
		flash_ok = 0;
		printk("lxfs in RAM");
	}
}

/*
 * Apply the journal of slot @n to image[], up to its end or the first
 * record that fails its check, and set log_end there.  After a failed
 * record the rest of the page cannot be programmed reliably, so the
 * journal counts as full.
 *
 * 把槽 @n 的日志应用到 image[], 直到日志末尾或第一条校验失败的记录, 并把
 * log_end 设在该处. 校验失败的记录之后, 该页其余部分无法可靠编程, 因此日志视为
 * 已满.
 */
static void journal_replay(uint8_t n)
{
	const uint8_t *log = slot_log(n);
	const uint16_t *head;
	uint16_t pos = HEADER_BYTES;
	uint16_t off, len, check;

	while (pos + REC_HEAD <= PAGE_BYTES) {
		head = (const uint16_t *)(log + pos);
		off = head[0];
		len = head[1];
		if (off == ERASED_HALF && len == ERASED_HALF)
			break;
		if (!len || len > LXFS_SIZE || off > LXFS_SIZE - len ||
		    pos + REC_BYTES(len) > PAGE_BYTES)
			goto torn;
		check = *(const uint16_t *)(log + pos + REC_BYTES(len) - 2);
		if (check != record_check(off, len, log + pos + REC_HEAD))
			goto torn;
		memcpy(image + off, log + pos + REC_HEAD, len);
		pos += REC_BYTES(len);
	}
	log_end = pos;
	return;
torn:
	log_end = PAGE_BYTES;
}

/*
 * The slot with the highest valid generation, or -1 if none is valid,
 * as on a board never written.
 *
 * 代数最高的有效槽; 没有有效槽时 (例如从未写入过的板子) 返回 -1.
 */
static int slot_newest(void)
{
	const struct slot_header *hdr;
	int best = -1;
	uint8_t n;

	for (n = 0; n < nr_slots; n++) {
		hdr = (const struct slot_header *)slot_log(n);
		if (hdr->gen == ERASED_WORD ||
		    hdr->crc != image_crc(slot_image(n), hdr->gen))
			continue;
		if (best < 0 ||
		    hdr->gen > ((const struct slot_header *)slot_log(best))->gen)
			best = n;
	}
	return best;
}

/*
 * Load the newest slot into RAM and mount lxfs on it.  With no valid
 * slot the RAM copy starts erased, as a blank EEPROM reads, and the
 * journal of the last slot counts as full, so the first write starts
 * slot 0.
 *
 * 把最新的槽读入 RAM, 并在其上挂载 lxfs. 没有有效槽时, RAM 副本初始为擦除状态,
 * 与空白 EEPROM 的读数相同; 最后一个槽的日志视为已满, 因此第一次写入从槽 0
 * 开始.
 */
static int flash_fs_init(void)
{
	int newest;

	nr_slots = (__lxfs_flash_end - __lxfs_flash_start) / SLOT_BYTES;
	if (nr_slots < 2) {
		printk("no lxfs flash");
		return -1;
	}
	newest = slot_newest();
	if (newest < 0) {
		memset(image, 0xff, sizeof(image));
		slot = nr_slots - 1;
		gen = 0;
		log_end = PAGE_BYTES;
	} else {
		slot = newest;
		gen = ((const struct slot_header *)slot_log(slot))->gen;
		memcpy(image, slot_image(slot), sizeof(image));
		journal_replay(slot);
	}
	flash_ok = 1;

	switch (lxfs_init(flash_dev_read, flash_dev_write)) {
	case LXFS_CLEAN:
		break;
	case LXFS_FORMATTED:
		printk("mkfs lxfs");
		break;
	case LXFS_REPAIRED:
		printk("lxfs repaired");
		break;
	default:
		printk("lxfs: unknown");
		break;
	}
	return 0;
}
fs_initcall(flash_fs_init);
