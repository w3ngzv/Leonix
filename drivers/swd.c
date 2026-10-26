// SPDX-License-Identifier: GPL-2.0-only
/*
 * A Serial Wire Debug host on two GPIO pins, enough to identify an STM32F1
 * without a debug probe.  Reading /dev/swd connects to the target, powers
 * up its debug port and prints the SW-DP identification register DPIDR,
 * then the words in swd_words[], one line each.  Among them:
 *
 *   IDCODE  DBGMCU_IDCODE at 0xE0042000, RM0008 31.6.1
 *   FLASH   the flash size register at 0x1FFFF7E0, KiB in the low half,
 *           RM0008 30.1.1
 *   CPUID   the Cortex-M3 CPUID register at 0xE000ED00
 *
 * The addresses are the STM32F1's; on another chip they read whatever
 * lies there, or fault.
 *
 * D8 (PB4) drives SWCLK, D9 (PB5) carries SWDIO, both left as inputs
 * between reads.  The pins swing 5 V into a 3.3 V target: STM32F103 PA13
 * and PA14 are marked FT in DS5319, and the target's 3.3 V high is above
 * the 1.9 V VIH (0.2 VCC + 0.9) the ATmega32U4 has at 5 V, datasheet 7766J.
 * A resistor of about 1 kOhm in the SWDIO line limits the current if both
 * ends drive it at once around a turnaround.
 *
 * Bit order and packet layout follow the Arm Debug Interface v5: the
 * target samples SWDIO on the rising edge of SWCLK and changes it after
 * that edge, so the host reads a bit while SWCLK is low.  SWD has no
 * lowest clock rate, which lets interrupts stretch any cycle.
 *
 * 在两个 GPIO 引脚上实现的 Serial Wire Debug 主机, 不借助调试器即可识别
 * STM32F1. 读取 /dev/swd 时连接目标, 为其调试端口上电, 输出 SW-DP 识别寄存器
 * DPIDR, 再逐行输出 swd_words[] 中的各个字. 其中包括:
 *
 *   IDCODE  0xE0042000 处的 DBGMCU_IDCODE, RM0008 31.6.1
 *   FLASH   0x1FFFF7E0 处的闪存容量寄存器, 低半字为 KiB 数, RM0008 30.1.1
 *   CPUID   0xE000ED00 处的 Cortex-M3 CPUID 寄存器
 *
 * 这些地址属于 STM32F1; 在其他芯片上, 读到的是该处恰好存放的内容, 或者产生故障.
 *
 * D8 (PB4) 驱动 SWCLK, D9 (PB5) 传输 SWDIO, 两次读取之间都设为输入. 引脚以 5 V
 * 摆幅驱动 3.3 V 的目标: DS5319 把 STM32F103 的 PA13 与 PA14 标为 FT, 而目标的
 * 3.3 V 高电平高于 ATmega32U4 在 5 V 下的 VIH 1.9 V (0.2 VCC + 0.9, 数据手册
 * 7766J). SWDIO 线上串联约 1 kOhm 的电阻, 在换向前后两端同时驱动时限制电流.
 *
 * 位序与包格式遵循 Arm Debug Interface v5: 目标在 SWCLK 上升沿采样 SWDIO, 并在
 * 该沿之后改变 SWDIO, 因此主机在 SWCLK 为低时读取一位. SWD 没有时钟频率下限,
 * 中断因此可以拉长任何一个周期.
 */
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <util/delay.h>

#include <leonix/array_size.h>
#include <leonix/fs.h>
#include <leonix/kernel.h>
#include <leonix/module.h>

#define SWCLK		PB4
#define SWDIO		PB5
#define HALF_CYCLE_US	2

#define ACK_OK		1
#define ACK_WAIT	2
#define ACK_FAULT	4
/*
 * ACK_NONE: nothing drove the line.  ERR_PARITY and ERR_POWER are not
 * ACKs: a read whose parity bit was wrong, and a debug port that never
 * powered up.
 *
 * ACK_NONE: 没有设备驱动该线. ERR_PARITY 与 ERR_POWER 不是 ACK, 分别表示奇偶校
 * 验位错误的读取, 以及始终未能上电的调试端口.
 */
#define ACK_NONE	7
#define ERR_PARITY	8
#define ERR_POWER	9

#define DP		0
#define AP		1
#define WRITE		0
#define READ		1

/*
 * DP registers, by their A[3:2] byte address
 * DP 寄存器, 按其 A[3:2] 字节地址
 */
#define DP_DPIDR	0x0	/* read; 读 */
#define DP_ABORT	0x0	/* write; 写 */
#define DP_CTRL_STAT	0x4
#define DP_SELECT	0x8
#define DP_RDBUFF	0xc

/*
 * AHB-AP registers in bank 0
 * 第 0 组中的 AHB-AP 寄存器
 */
#define AP_CSW		0x0
#define AP_TAR		0x4
#define AP_DRW		0xc

#define ABORT_CLEAR_ALL	0x0000001eUL	/* STKCMPCLR, STKERRCLR, WDERRCLR, ORUNERRCLR */
#define PWRUP_REQ	0x50000000UL	/* CSYSPWRUPREQ | CDBGPWRUPREQ */
#define PWRUP_ACK	0xa0000000UL	/* CSYSPWRUPACK | CDBGPWRUPACK */
#define PWRUP_TRIES	16
/*
 * CSW_WORD: 32-bit accesses, no auto-increment
 * CSW_WORD: 32 位访问, 地址不自动递增
 */
#define CSW_WORD	0x23000002UL

#define JTAG_TO_SWD	0xe79e
/*
 * a line reset needs at least 50 ones
 * 线复位至少需要 50 个 1
 */
#define RESET_ONES	56

/*
 * Words read through the AHB-AP, one line each after DPIDR.  ROMPIn are
 * the peripheral ID registers of the Cortex-M3 ROM table, where RM0008
 * 31.6.4 places the JEDEC-106 code of the chip's designer.  UIDn is the
 * 96-bit unique ID (RM0008 30.2), OPTn the option bytes at 0x1FFFF800.
 *
 * 经由 AHB-AP 读取的字, 在 DPIDR 之后每字一行. ROMPIn 是 Cortex-M3 ROM 表的外设
 * ID 寄存器, RM0008 31.6.4 把芯片设计者的 JEDEC-106 代码放在其中. UIDn 是 96 位
 * 唯一 ID (RM0008 30.2), OPTn 是 0x1FFFF800 处的选项字节.
 */
struct swd_word {
	char name[7];
	uint32_t addr;
};

static const struct swd_word swd_words[] PROGMEM = {
	{ "IDCODE", 0xe0042000UL },
	{ "FLASH ", 0x1ffff7e0UL },
	{ "CPUID ", 0xe000ed00UL },
	{ "ROMPI4", 0xe00fffd0UL },
	{ "ROMPI0", 0xe00fffe0UL },
	{ "ROMPI1", 0xe00fffe4UL },
	{ "ROMPI2", 0xe00fffe8UL },
	{ "ROMPI3", 0xe00fffecUL },
	{ "UID0  ", 0x1ffff7e8UL },
	{ "UID1  ", 0x1ffff7ecUL },
	{ "UID2  ", 0x1ffff7f0UL },
	{ "OPT0  ", 0x1ffff800UL },
	{ "OPT1  ", 0x1ffff804UL },
};

#define NR_SWD_WORDS	ARRAY_SIZE(swd_words)

/*
 * The line after swd_words[] decodes the ROM table's PIDR into the
 * designer's JEP106 continuation and identity codes and the part number,
 * fields as in CoreSight IHI 0029F B2.2: DES_0 PIDR1[7:4], DES_1
 * PIDR2[2:0], DES_2 PIDR4[3:0], PART PIDR1[3:0]:PIDR0.  JEP106AB lists
 * 0x20 in bank one, continuation 0, as SGS/Thomson, now ST.
 *
 * swd_words[] 之后的一行把 ROM 表的 PIDR 解码为设计者的 JEP106 延续码与识别码
 * 以及部件号, 字段按 CoreSight IHI 0029F B2.2: DES_0 为 PIDR1[7:4], DES_1 为
 * PIDR2[2:0], DES_2 为 PIDR4[3:0], PART 为 PIDR1[3:0]:PIDR0. JEP106AB 把第一
 * 组、延续码 0 中的 0x20 列为 SGS/Thomson, 即现在的 ST.
 */
#define ROM_PIDR0	0xe00fffe0UL
#define ROM_PIDR4	0xe00fffd0UL
#define PIDR_JEDEC	0x08

/*
 * link: ACK_OK once the AHB-AP is usable.  rom_pidr: PIDR0 to PIDR3,
 * then PIDR4.
 *
 * link: AHB-AP 可用之后为 ACK_OK. rom_pidr: 依次为 PIDR0 至 PIDR3, 然后是
 * PIDR4.
 */
static uint8_t link;
static char line[24];
static uint8_t rom_pidr[5];

static void write_bits(uint32_t v, uint8_t n)
{
	while (n--) {
		if (v & 1)
			PORTB |= 1 << SWDIO;
		else
			PORTB &= ~(1 << SWDIO);
		v >>= 1;
		_delay_us(HALF_CYCLE_US);
		PORTB |= 1 << SWCLK;
		_delay_us(HALF_CYCLE_US);
		PORTB &= ~(1 << SWCLK);
	}
}

static uint32_t read_bits(uint8_t n)
{
	uint32_t v = 0;
	uint8_t i;

	for (i = 0; i < n; i++) {
		_delay_us(HALF_CYCLE_US);
		if (PINB & (1 << SWDIO))
			v |= (uint32_t)1 << i;
		PORTB |= 1 << SWCLK;
		_delay_us(HALF_CYCLE_US);
		PORTB &= ~(1 << SWCLK);
	}
	return v;
}

/*
 * Low before output, so the pull-up is never switched on into the target.
 *
 * 先置低再设为输出, 使上拉电阻永远不会接到目标上.
 */
static void swdio_release(void)
{
	PORTB &= ~(1 << SWDIO);
	DDRB &= ~(1 << SWDIO);
}

static void swdio_drive(void)
{
	DDRB |= 1 << SWDIO;
}

/*
 * One SWD transaction.  @addr is the register's byte address, of which
 * only A[3:2] travel.  Returns ACK_OK, the ACK the target sent, or
 * ERR_PARITY for a read that arrived damaged.
 *
 * 一次 SWD 事务. @addr 是寄存器的字节地址, 只传送其中的 A[3:2]. 返回 ACK_OK、
 * 目标发回的 ACK, 或在读取的数据损坏时返回 ERR_PARITY.
 */
static uint8_t transfer(uint8_t apndp, uint8_t rnw, uint8_t addr,
			uint32_t *data)
{
	uint8_t req, ack;
	uint32_t v;

	req = apndp << 1 | rnw << 2 | (addr & 0x0c) << 1;
	if (__builtin_parity(req))
		req |= 1 << 5;
	/*
	 * start and park; stop is 0
	 * start 位与 park 位; stop 位为 0
	 */
	req |= 1 | 1 << 7;

	swdio_drive();
	write_bits(req, 8);
	swdio_release();
	read_bits(1);			/* turnaround; 换向周期 */
	ack = read_bits(3);
	if (ack != ACK_OK) {
		read_bits(1);
		swdio_drive();
		write_bits(0, 8);
		return ack;
	}
	if (rnw) {
		v = read_bits(32);
		if (read_bits(1) != (uint32_t)__builtin_parityl(v))
			ack = ERR_PARITY;
		*data = v;
		read_bits(1);
		swdio_drive();
	} else {
		read_bits(1);
		swdio_drive();
		write_bits(*data, 32);
		write_bits(__builtin_parityl(*data), 1);
	}
	/*
	 * idle cycles to finish the transaction
	 * 结束事务所需的空闲周期
	 */
	write_bits(0, 8);
	return ack;
}

static uint8_t dp_write(uint8_t addr, uint32_t v)
{
	return transfer(DP, WRITE, addr, &v);
}

/*
 * AP reads are posted: the value comes back from the following RDBUFF.
 *
 * AP 读取是延迟提交的: 读到的值由随后的 RDBUFF 读取返回.
 */
static uint8_t mem_read(uint32_t addr, uint32_t *v)
{
	uint8_t ack;

	ack = transfer(AP, WRITE, AP_TAR, &addr);
	if (ack == ACK_OK)
		ack = transfer(AP, READ, AP_DRW, v);
	if (ack == ACK_OK)
		ack = transfer(DP, READ, DP_RDBUFF, v);
	return ack;
}

static void line_reset(void)
{
	write_bits(0xffffffffUL, 32);
	write_bits(0xffffffffUL, RESET_ONES - 32);
}

static uint8_t power_up(void)
{
	uint32_t ctrl = 0;
	uint8_t i, ack;

	ack = dp_write(DP_ABORT, ABORT_CLEAR_ALL);
	if (ack == ACK_OK)
		ack = dp_write(DP_CTRL_STAT, PWRUP_REQ);
	for (i = 0; ack == ACK_OK && i < PWRUP_TRIES; i++) {
		ack = transfer(DP, READ, DP_CTRL_STAT, &ctrl);
		if ((ctrl & PWRUP_ACK) == PWRUP_ACK)
			break;
	}
	if (ack == ACK_OK && (ctrl & PWRUP_ACK) != PWRUP_ACK)
		ack = ERR_POWER;
	if (ack == ACK_OK)
		ack = dp_write(DP_SELECT, 0);
	if (ack == ACK_OK) {
		ctrl = CSW_WORD;
		ack = transfer(AP, WRITE, AP_CSW, &ctrl);
	}
	return ack;
}

/*
 * Switches the target to SWD and reads DPIDR into @v; sets link to the
 * outcome of powering up the debug port.
 *
 * 把目标切换到 SWD, 把 DPIDR 读入 @v; 并把 link 设为调试端口上电的结果.
 */
static uint8_t connect(uint32_t *v)
{
	uint8_t ack;

	line_reset();
	write_bits(JTAG_TO_SWD, 16);
	line_reset();
	write_bits(0, 8);
	ack = transfer(DP, READ, DP_DPIDR, v);
	link = ack == ACK_OK ? power_up() : ack;
	return ack;
}

static const char *status_text(uint8_t s)
{
	switch (s) {
	case ACK_WAIT:
		return PSTR("wait");
	case ACK_FAULT:
		return PSTR("fault");
	case ACK_NONE:
		return PSTR("no reply");
	case ERR_PARITY:
		return PSTR("parity error");
	default:
		return PSTR("no power-up");
	}
}

static uint8_t __attribute__((noinline)) jep106_line(void)
{
	uint32_t v = 0;
	uint8_t i, st = link;

	DDRB |= 1 << SWCLK;
	swdio_drive();
	for (i = 0; st == ACK_OK && i < sizeof(rom_pidr); i++) {
		st = mem_read(i < 4 ? ROM_PIDR0 + 4 * i : ROM_PIDR4, &v);
		rom_pidr[i] = v;
	}
	swdio_release();
	DDRB &= ~(1 << SWCLK);

	if (st != ACK_OK)
		return snprintk(line, sizeof(line), "JEP106 %S\n",
				status_text(st));
	if (!(rom_pidr[2] & PIDR_JEDEC))
		return snprintk(line, sizeof(line), "JEP106 none\n");
	return snprintk(line, sizeof(line), "JEP106 %u %02x part %03x\n",
			rom_pidr[4] & 0x0f,
			(rom_pidr[2] & 0x07) << 4 | rom_pidr[1] >> 4,
			(rom_pidr[1] & 0x0f) << 8 | rom_pidr[0]);
}

/*
 * Line @n into line[]: DPIDR for line 0, which also connects, one line
 * per swd_words[] entry, then the decoded JEP106 line.  The pins are
 * driven only inside a call.
 *
 * 把第 @n 行写入 line[]: 第 0 行为 DPIDR, 同时建立连接; 随后 swd_words[] 每项
 * 一行; 最后是解码后的 JEP106 行. 引脚只在调用期间被驱动.
 */
static uint8_t __attribute__((noinline)) swd_line(uint8_t n)
{
	const char *name;
	uint32_t v = 0;
	uint8_t st;

	if (n > NR_SWD_WORDS + 1)
		return 0;
	if (n == NR_SWD_WORDS + 1)
		return jep106_line();
	DDRB |= 1 << SWCLK;
	swdio_drive();
	if (n == 0) {
		name = PSTR("DPIDR ");
		st = connect(&v);
	} else {
		name = swd_words[n - 1].name;
		st = link;
		if (st == ACK_OK)
			st = mem_read(pgm_read_dword(&swd_words[n - 1].addr),
				      &v);
	}
	swdio_release();
	DDRB &= ~(1 << SWCLK);

	if (st == ACK_OK)
		return snprintk(line, sizeof(line), "%S %08lx\n", name, v);
	if (st < ERR_PARITY && st != ACK_WAIT && st != ACK_FAULT &&
	    st != ACK_NONE)
		return snprintk(line, sizeof(line), "%S ack %u\n", name, st);
	return snprintk(line, sizeof(line), "%S %S\n", name, status_text(st));
}

/*
 * private_data is the line number, f_pos the offset in it, as in /proc.
 * A line is made once, when its first byte is read, since making it
 * talks to the target.
 *
 * 与 /proc 相同, private_data 是行号, f_pos 是行内偏移. 生成一行需要与目标通信,
 * 因此每行只在读取其第一个字节时生成一次.
 */
static int line_read(struct file *f, char *buf, uint8_t count,
		     uint8_t (*make)(uint8_t))
{
	uint8_t len, n;

	len = f->f_pos ? strlen(line) : make(f->private_data);
	if (!len)
		return 0;
	n = len - f->f_pos;
	if (n > count)
		n = count;
	memcpy(buf, line + f->f_pos, n);
	f->f_pos += n;
	if (f->f_pos == len) {
		f->f_pos = 0;
		f->private_data++;
	}
	return n;
}

static int swd_read(struct file *f, char *buf, uint8_t count)
{
	return line_read(f, buf, count, swd_line);
}

static const struct file_operations swd_fops = {
	.read = swd_read,
};

/*
 * /dev/dap: memory access and run control on the target for a script on
 * the host.  The shell's echo is the only writer, so each written line
 * is one command, numbers in hex:
 *
 *   c        connect and power up the debug port
 *   a ADDR   the word address that w and reads start from
 *   w VALUE  write a word there and step to the next word
 *   n COUNT  how many words a read prints
 *   h        halt the core
 *   g PC     run from PC: Thumb state, SP at the top of 20 KiB of SRAM,
 *            PRIMASK set
 *   x        reset the target
 *   p        power the debug port down: clear CDBGPWRUPREQ and
 *            CSYSPWRUPREQ and wait for both ACKs to fall
 *   f        time FREQ_PERIODS rising edges on D13 (PC7, ICP3)
 *
 * A read prints "ADDR VALUE" lines, after f the line "F SUM", SUM being
 * the Timer3 counts at F_CPU over FREQ_PERIODS periods, after p the line
 * "P CTRL/STAT" with the last value read.  Only DP registers are reached
 * while the port is down; c powers it up again.  The first failed
 * command since the last read makes the next read print "err N" alone,
 * N an ACK or ERR_* code, so a script can check once after many writes.
 *
 * /dev/dap: 供主机上的脚本访问目标内存并控制其运行. 唯一的写入者是 shell 的
 * echo, 因此写入的每一行是一条命令, 数字为十六进制:
 *
 *   c        连接并为调试端口上电
 *   a ADDR   w 与读取的起始字地址
 *   w VALUE  在该地址写入一个字, 并前进到下一个字
 *   n COUNT  一次读取输出的字数
 *   h        使内核停机
 *   g PC     从 PC 开始运行: Thumb 状态, SP 位于 20 KiB SRAM 的顶端,
 *            PRIMASK 置位
 *   x        复位目标
 *   p        关闭调试端口电源: 清除 CDBGPWRUPREQ 与 CSYSPWRUPREQ, 并等待
 *            两个 ACK 都变低
 *   f        计时 D13 (PC7, ICP3) 上的 FREQ_PERIODS 个上升沿
 *
 * 读取输出若干 "ADDR VALUE" 行; f 之后输出 "F SUM" 一行, SUM 是 FREQ_PERIODS 个
 * 周期内以 F_CPU 计数的 Timer3 计数和; p 之后输出 "P CTRL/STAT" 一行, 其值为最
 * 后读到的值. 端口关闭期间只能访问 DP 寄存器, c 会重新为其上电. 自上次读取以来
 * 第一条失败的命令使下一次读取只输出 "err N", N 为 ACK 或 ERR_* 代码, 脚本因此
 * 可以在多次写入之后只检查一次.
 */
#define DHCSR		0xe000edf0UL
#define DCRSR		0xe000edf4UL
#define DCRDR		0xe000edf8UL
#define AIRCR		0xe000ed0cUL
#define DHCSR_HALT	0xa05f0003UL	/* DBGKEY | C_HALT | C_DEBUGEN */
#define DHCSR_RUN	0xa05f0001UL	/* DBGKEY | C_DEBUGEN */
#define DHCSR_OFF	0xa05f0000UL
#define AIRCR_RESET	0x05fa0004UL	/* VECTKEY | SYSRESETREQ */
#define DCRSR_WRITE	0x00010000UL
#define REG_SP		13
#define REG_PC		15
#define REG_XPSR	16
/*
 * REG_PRIMASK selects PRIMASK with BASEPRI, FAULTMASK and CONTROL
 * REG_PRIMASK 同时选中 PRIMASK、BASEPRI、FAULTMASK 与 CONTROL
 */
#define REG_PRIMASK	20
#define XPSR_THUMB	0x01000000UL
#define TARGET_SP	0x20005000UL

#define ERR_SYNTAX	10
#define ERR_NOSIGNAL	11
#define FREQ_PERIODS	64
/*
 * FREQ_TIMEOUT: Timer3 overflows, 4.1 ms each
 * FREQ_TIMEOUT: Timer3 溢出次数, 每次 4.1 ms
 */
#define FREQ_TIMEOUT	25

static char cmd[16];
static uint8_t cmd_len;
static uint32_t dap_addr, freq_sum, ctrl_stat;
static uint8_t dap_count, dap_freq, dap_power;
static uint8_t dap_err = ACK_OK;

static uint8_t mem_write(uint32_t addr, uint32_t v)
{
	uint8_t ack;

	ack = transfer(AP, WRITE, AP_TAR, &addr);
	if (ack == ACK_OK)
		ack = transfer(AP, WRITE, AP_DRW, &v);
	return ack;
}

static uint8_t core_reg_write(uint8_t reg, uint32_t v)
{
	uint8_t ack;

	ack = mem_write(DCRDR, v);
	if (ack == ACK_OK)
		ack = mem_write(DCRSR, DCRSR_WRITE | reg);
	return ack;
}

static uint8_t run_from(uint32_t pc)
{
	uint8_t ack;

	ack = mem_write(DHCSR, DHCSR_HALT);
	if (ack == ACK_OK)
		ack = core_reg_write(REG_PC, pc);
	if (ack == ACK_OK)
		ack = core_reg_write(REG_XPSR, XPSR_THUMB);
	if (ack == ACK_OK)
		ack = core_reg_write(REG_SP, TARGET_SP);
	if (ack == ACK_OK)
		ack = core_reg_write(REG_PRIMASK, 1);
	if (ack == ACK_OK)
		ack = mem_write(DHCSR, DHCSR_RUN);
	return ack;
}

/*
 * Sums FREQ_PERIODS periods of the signal on ICP3, Timer3 running at
 * F_CPU.  A period must be under 65536 counts, about 4 ms.  PC7 also
 * drives the L LED buffer and goes back to an output afterwards.
 *
 * 累加 ICP3 上信号的 FREQ_PERIODS 个周期, Timer3 以 F_CPU 计数. 一个周期必须少
 * 于 65536 个计数, 约 4 ms. PC7 同时驱动 L LED 的缓冲器, 测量结束后恢复为输出.
 */
static uint8_t measure_freq(void)
{
	uint16_t last = 0, now;
	uint8_t edges = 0, idle = 0;

	DDRC &= ~(1 << DDC7);
	TCCR3A = 0;
	TCCR3B = (1 << ICNC3) | (1 << ICES3) | (1 << CS30);
	TIFR3 = (1 << ICF3) | (1 << TOV3);
	freq_sum = 0;
	while (edges <= FREQ_PERIODS && idle < FREQ_TIMEOUT) {
		if (TIFR3 & (1 << TOV3)) {
			TIFR3 = 1 << TOV3;
			idle++;
		}
		if (!(TIFR3 & (1 << ICF3)))
			continue;
		now = ICR3;
		TIFR3 = 1 << ICF3;
		if (edges)
			freq_sum += (uint16_t)(now - last);
		last = now;
		edges++;
		idle = 0;
	}
	TCCR3B = 0;
	DDRC |= 1 << DDC7;
	return edges > FREQ_PERIODS ? ACK_OK : ERR_NOSIGNAL;
}

static uint8_t power_down(void)
{
	uint8_t i, ack;

	ack = dp_write(DP_CTRL_STAT, 0);
	for (i = 0; ack == ACK_OK && i < PWRUP_TRIES; i++) {
		ack = transfer(DP, READ, DP_CTRL_STAT, &ctrl_stat);
		if (!(ctrl_stat & PWRUP_ACK))
			return ack;
	}
	return ack == ACK_OK ? ERR_POWER : ack;
}

/*
 * @s points after the command letter; returns 0 if no hex number follows.
 *
 * @s 指向命令字母之后; 其后没有十六进制数时返回 0.
 */
static uint8_t parse_hex(const char *s, uint32_t *v)
{
	uint8_t digits = 0;
	char c;

	while (*s == ' ')
		s++;
	*v = 0;
	while ((c = *s++)) {
		if (c >= '0' && c <= '9')
			c -= '0';
		else if (c >= 'a' && c <= 'f')
			c -= 'a' - 10;
		else
			return 0;
		*v = *v << 4 | c;
		digits++;
	}
	return digits;
}

static uint8_t __attribute__((noinline)) dap_command(void)
{
	uint32_t v = 0, dpidr;
	uint8_t ack = ACK_OK;

	if (strchr_P(PSTR("awng"), cmd[0]) && !parse_hex(cmd + 1, &v))
		return ERR_SYNTAX;
	dap_freq = 0;
	dap_power = 0;
	switch (cmd[0]) {
	case 'a':
		dap_addr = v;
		return ACK_OK;
	case 'n':
		dap_count = v;
		return ACK_OK;
	case 'f':
		dap_freq = 1;
		return measure_freq();
	}

	DDRB |= 1 << SWCLK;
	swdio_drive();
	switch (cmd[0]) {
	case 'c':
		ack = connect(&dpidr);
		if (ack == ACK_OK)
			ack = link;
		break;
	case 'w':
		ack = mem_write(dap_addr, v);
		dap_addr += 4;
		break;
	case 'h':
		ack = mem_write(DHCSR, DHCSR_HALT);
		break;
	case 'g':
		ack = run_from(v);
		break;
	case 'p':
		dap_power = 1;
		ack = power_down();
		break;
	case 'x':
		mem_write(DHCSR, DHCSR_OFF);
		/*
		 * the reset takes no ACK
		 * 复位不会返回 ACK
		 */
		mem_write(AIRCR, AIRCR_RESET);
		break;
	default:
		ack = ERR_SYNTAX;
	}
	swdio_release();
	DDRB &= ~(1 << SWCLK);
	return ack;
}

static int dap_write(struct file *f, const char *buf, uint8_t count)
{
	uint8_t i, st;

	(void)f;
	for (i = 0; i < count; i++) {
		if (buf[i] != '\n') {
			if (cmd_len < sizeof(cmd) - 1)
				cmd[cmd_len++] = buf[i];
			continue;
		}
		cmd[cmd_len] = '\0';
		if (cmd_len) {
			st = dap_command();
			if (dap_err == ACK_OK)
				dap_err = st;
		}
		cmd_len = 0;
	}
	return count;
}

static uint8_t __attribute__((noinline)) dap_line(uint8_t n)
{
	uint32_t addr, v;
	uint8_t st;

	if (dap_err != ACK_OK) {
		if (n) {
			dap_err = ACK_OK;
			return 0;
		}
		return snprintk(line, sizeof(line), "err %u\n", dap_err);
	}
	if (dap_freq)
		return n ? 0 : snprintk(line, sizeof(line), "F %lu\n", freq_sum);
	if (dap_power)
		return n ? 0 : snprintk(line, sizeof(line), "P %08lx\n",
					ctrl_stat);
	if (n >= dap_count)
		return 0;
	addr = dap_addr + 4 * (uint32_t)n;
	DDRB |= 1 << SWCLK;
	swdio_drive();
	st = mem_read(addr, &v);
	swdio_release();
	DDRB &= ~(1 << SWCLK);
	if (st != ACK_OK)
		return snprintk(line, sizeof(line), "%08lx err %u\n", addr, st);
	return snprintk(line, sizeof(line), "%08lx %08lx\n", addr, v);
}

static int dap_read(struct file *f, char *buf, uint8_t count)
{
	return line_read(f, buf, count, dap_line);
}

static const struct file_operations dap_fops = {
	.read = dap_read,
	.write = dap_write,
};

static int swd_init(void)
{
	int err;

	err = register_chrdev(PSTR("swd"), &swd_fops);
	if (!err && IS_ENABLED(CONFIG_SWD_DAP))
		err = register_chrdev(PSTR("dap"), &dap_fops);
	return err;
}
module_init(swd_init);
