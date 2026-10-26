// SPDX-License-Identifier: GPL-2.0-only
/*
 * How soon after a RESET of the CRC unit the data register may be used.
 * RM0008 4.3 lets DR be written back to back and read right after a
 * write, but gives no time for the RESET bit in CRC_CR to take effect.
 * On the board this project was written on, an access to DR in the very
 * next instruction after RESET sees the state from before the RESET,
 * while the sequence of HAL_CRC_Calculate() in stm32f1xx-hal-driver
 * V1.1.10 computes correctly.  crcreset_check.py names what each record
 * shows; the results from other boards, an ST part above all, tell
 * whether this is how the silicon behaves.
 *
 * The sequences under test are written in assembly so that the compiler
 * cannot put other instructions between the accesses.  Before each case
 * DR is loaded with the CRC of PRELOAD, so that a RESET that did not
 * take effect can be told apart from a write that was lost.
 *
 * CRC 单元 RESET 之后多久可以访问数据寄存器. RM0008 4.3 允许连续写入 DR, 也
 * 允许写入后立即读取, 但没有给出 CRC_CR 中 RESET 位生效所需的时间. 在本项目
 * 所用的板子上, RESET 的下一条指令访问 DR 时看到的是 RESET 之前的状态, 而
 * stm32f1xx-hal-driver V1.1.10 中 HAL_CRC_Calculate() 的指令顺序计算正确.
 * 各条记录的含义由 crcreset_check.py 说明; 其他板子, 尤其是 ST 原厂芯片的
 * 结果, 可以说明这是否是芯片本身的行为.
 *
 * 被测的指令顺序用汇编写出, 编译器因此无法在两次访问之间插入其他指令. 每种
 * 情形之前, DR 先装入 PRELOAD 的 CRC, 以区分未生效的 RESET 与丢失的写入.
 */
#include "runtime.h"

#define RCC_AHBENR	REG(0x40021014u)
#define AHB_CRC		(1u << 6)
#define CRC_BASE	0x40023000u
#define CRC_DR		REG(CRC_BASE + 0x00u)
#define CRC_CR		REG(CRC_BASE + 0x08u)
#define CR_RESET	1u
#define PRELOAD		0x11111111u
#define WORD		0x12345678u
#define REPEATS		4
#define NR_HAL_WORDS	64
#define SETTLE_CYCLES	100

/* op codes, shared with crcreset_check.py; 操作码, 与 crcreset_check.py 共用 */
enum {
	C_ENABLE_WRITE = 0x400,
	C_ENABLE_READBACK_WRITE,
	C_RESET_WRITE,
	C_RESET_NOPS_WRITE,
	C_RESET_READ,
	C_HAL,
};

#define NOPS8	"nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\t"

/* DR = CRC(PRELOAD), returned as read back; DR 置为 CRC(PRELOAD) 并读回 */
static uint32_t preload(void)
{
	RCC_AHBENR |= AHB_CRC;
	(void)RCC_AHBENR;
	delay_cycles(SETTLE_CYCLES);
	CRC_CR = CR_RESET;
	delay_cycles(SETTLE_CYCLES);
	CRC_DR = PRELOAD;
	return CRC_DR;
}

static void clock_off(void)
{
	RCC_AHBENR &= ~AHB_CRC;
	(void)RCC_AHBENR;
	delay_cycles(SETTLE_CYCLES);
}

/* enable the clock, RESET, write WORD, read; 开时钟, RESET, 写入, 读取 */
static uint32_t enable_write(int readback)
{
	uint32_t r, t;

	clock_off();
	if (readback)
		asm volatile("ldr %[t], [%[rcc]]\n\t"
			     "orr %[t], %[t], %[en]\n\t"
			     "str %[t], [%[rcc]]\n\t"
			     "ldr %[t], [%[rcc]]\n\t"
			     "str %[one], [%[crc], #8]\n\t"
			     "str %[x], [%[crc]]\n\t"
			     "ldr %[r], [%[crc]]"
			     : [r] "=&r"(r), [t] "=&r"(t)
			     : [rcc] "r"(&RCC_AHBENR), [crc] "r"(CRC_BASE),
			       [en] "r"(AHB_CRC), [one] "r"(CR_RESET),
			       [x] "r"(WORD)
			     : "memory");
	else
		asm volatile("ldr %[t], [%[rcc]]\n\t"
			     "orr %[t], %[t], %[en]\n\t"
			     "str %[t], [%[rcc]]\n\t"
			     "str %[one], [%[crc], #8]\n\t"
			     "str %[x], [%[crc]]\n\t"
			     "ldr %[r], [%[crc]]"
			     : [r] "=&r"(r), [t] "=&r"(t)
			     : [rcc] "r"(&RCC_AHBENR), [crc] "r"(CRC_BASE),
			       [en] "r"(AHB_CRC), [one] "r"(CR_RESET),
			       [x] "r"(WORD)
			     : "memory");
	return r;
}

/* clock already on: RESET, then write WORD at once or after eight NOPs */
static uint32_t reset_write(int nops)
{
	uint32_t r;

	if (nops)
		asm volatile("str %[one], [%[crc], #8]\n\t"
			     NOPS8
			     "str %[x], [%[crc]]\n\t"
			     "ldr %[r], [%[crc]]"
			     : [r] "=&r"(r)
			     : [crc] "r"(CRC_BASE), [one] "r"(CR_RESET),
			       [x] "r"(WORD)
			     : "memory");
	else
		asm volatile("str %[one], [%[crc], #8]\n\t"
			     "str %[x], [%[crc]]\n\t"
			     "ldr %[r], [%[crc]]"
			     : [r] "=&r"(r)
			     : [crc] "r"(CRC_BASE), [one] "r"(CR_RESET),
			       [x] "r"(WORD)
			     : "memory");
	return r;
}

/* clock already on: RESET, then read DR at once; RESET 后立即读取 DR */
static uint32_t reset_read(void)
{
	uint32_t r;

	asm volatile("str %[one], [%[crc], #8]\n\t"
		     "ldr %[r], [%[crc]]"
		     : [r] "=&r"(r)
		     : [crc] "r"(CRC_BASE), [one] "r"(CR_RESET)
		     : "memory");
	return r;
}

/*
 * The order of HAL_CRC_Calculate(): CR |= RESET, then every word from
 * memory into DR, then DR read.  This one is in C, as HAL is.
 *
 * HAL_CRC_Calculate() 的顺序: CR |= RESET, 随后从存储器逐字写入 DR, 最后读取
 * DR. 与 HAL 相同, 该函数用 C 编写.
 */
static __attribute__((noinline)) uint32_t hal_sequence(const uint32_t *buf,
							uint32_t len)
{
	uint32_t i;

	CRC_CR |= CR_RESET;
	for (i = 0; i < len; i++)
		CRC_DR = buf[i];
	return CRC_DR;
}

void suite_main(void)
{
	uint32_t buf[NR_HAL_WORDS];
	uint32_t i, p;

	for (i = 0; i < NR_HAL_WORDS; i++)
		buf[i] = i * 0x9e3779b9u;

	set_phase(1);
	for (i = 0; i < REPEATS; i++) {
		p = preload();
		rec(C_ENABLE_WRITE, i, p, 0, enable_write(0), 0);
		p = preload();
		rec(C_ENABLE_READBACK_WRITE, i, p, 0, enable_write(1), 0);
		p = preload();
		rec(C_RESET_WRITE, i, p, 0, reset_write(0), 0);
		p = preload();
		rec(C_RESET_NOPS_WRITE, i, p, 0, reset_write(1), 0);
		p = preload();
		rec(C_RESET_READ, i, p, 0, reset_read(), 0);
		p = preload();
		rec(C_HAL, i, p, NR_HAL_WORDS,
		    hal_sequence(buf, NR_HAL_WORDS), 0);
	}
}
