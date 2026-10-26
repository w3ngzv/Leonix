// SPDX-License-Identifier: GPL-2.0-only
/*
 * Vector table, fault handler and result records for tests/stm32.
 *
 * A record is six words: op, three inputs, two outputs.  The host
 * recomputes what each record should hold, so the target never encodes
 * an expected value.  A full page of records parks the target in
 * PAGE_FULL until the host has copied the page and written PAGE_ACK.
 *
 * Faulting checks run as leaf functions with LR intact.  The handler
 * records CFSR and HFSR, clears them and resumes at LR, so the faulting
 * instruction behaves as a return to the caller.
 *
 * tests/stm32 的向量表、故障处理程序与结果记录.
 *
 * 每条记录六个字: 操作码、三个输入、两个输出. 每条记录应有的值由主机重新
 * 计算, 目标端不写入任何期望值. 一页记录写满后, 目标停在 PAGE_FULL, 直到
 * 主机复制该页并写入 PAGE_ACK.
 *
 * 会触发故障的检查以保留 LR 的叶函数运行. 故障处理程序记录 CFSR 与 HFSR,
 * 清除二者后从 LR 处继续执行, 触发故障的指令因此相当于返回调用者.
 */
#include "runtime.h"

#define CFSR		REG(0xe000ed28)
#define HFSR		REG(0xe000ed2c)
#define SHCSR		REG(0xe000ed24)
#define SYST_CSR	REG(0xe000e010)
#define SYST_RVR	REG(0xe000e014)
#define SYST_CVR	REG(0xe000e018)

/* MemManage, BusFault, UsageFault enables; 三种故障的使能位 */
#define SHCSR_ENABLES	((1u << 16) | (1u << 17) | (1u << 18))
#define SYST_ENABLE	1u
#define SYST_CORECLK	(1u << 2)
#define SYST_COUNTFLAG	(1u << 16)
#define SYST_MAX	0x00ffffffu
#define XPSR_T		(1u << 24)

uint32_t last_cfsr, last_hfsr;
static uint32_t nrec, phase;

void set_phase(uint32_t p)
{
	phase = p;
	HDR[H_STATE] = p;
}

void rec(uint32_t op, uint32_t a, uint32_t b, uint32_t c,
	 uint32_t lo, uint32_t hi)
{
	volatile uint32_t *r;

	if (nrec >= REC_MAX) {
		HDR[H_STATE] = PAGE_FULL;
		while (HDR[H_STATE] != PAGE_ACK)
			;
		HDR[H_STATE] = phase;
		nrec = 0;
		HDR[H_COUNT] = 0;
	}
	r = REC_BASE + nrec * REC_WORDS;
	r[0] = op;
	r[1] = a;
	r[2] = b;
	r[3] = c;
	r[4] = lo;
	r[5] = hi;
	HDR[H_COUNT] = ++nrec;
}

uint32_t fault_count(void)
{
	return HDR[H_FAULTS];
}

/**
 * wait_set() - poll a register until the masked bits equal @want
 * @addr: register address
 * @mask: bits to compare
 * @want: value the masked bits must reach
 * @limit: number of reads before giving up
 *
 * 轮询寄存器, 直到被屏蔽后的位等于 @want.
 *
 * Return: reads left when the bits matched, 0 on timeout.
 * 返回值: 位相符时剩余的读取次数, 超时为 0.
 */
uint32_t wait_set(uint32_t addr, uint32_t mask, uint32_t want, uint32_t limit)
{
	while (limit) {
		if ((REG(addr) & mask) == want)
			return limit;
		limit--;
	}
	return 0;
}

/**
 * delay_cycles() - busy-wait for a number of HCLK cycles on SysTick
 * @cycles: cycles to wait, any 32-bit value
 *
 * 以 SysTick 忙等待指定数目的 HCLK 周期.
 */
void delay_cycles(uint32_t cycles)
{
	uint32_t chunk;

	while (cycles) {
		chunk = cycles > SYST_MAX ? SYST_MAX : cycles;
		SYST_CSR = 0;
		SYST_RVR = chunk - 1;
		SYST_CVR = 0;
		SYST_CSR = SYST_CORECLK | SYST_ENABLE;
		while (!(SYST_CSR & SYST_COUNTFLAG))
			;
		cycles -= chunk;
	}
	SYST_CSR = 0;
}

void fault_c(uint32_t *frame)
{
	last_cfsr = CFSR;
	last_hfsr = HFSR;
	CFSR = last_cfsr;
	HFSR = last_hfsr;
	frame[6] = frame[5] & ~1u;	/* PC = LR */
	frame[7] |= XPSR_T;
	HDR[H_FAULTS]++;
}

asm(".text\n\t.align 1\n\t.thumb_func\n\t.global fault_entry\n"
	"fault_entry:\n\tmrs r0, msp\n\tb fault_c");

void reset_handler(void)
{
	uint32_t i;

	for (i = 0; i < 4; i++)
		HDR[i] = 0;
	HDR[H_MAGIC] = MAGIC;
	SHCSR |= SHCSR_ENABLES;
	suite_main();
	HDR[H_STATE] = DONE;
	for (;;)
		;
}

#define NR_VECTORS	16

__attribute__((section(".vectors"), used))
static const uint32_t vectors[NR_VECTORS] = {
	STACK_TOP,
	(uint32_t)reset_handler,
	(uint32_t)fault_entry, (uint32_t)fault_entry,	/* NMI, HardFault */
	(uint32_t)fault_entry, (uint32_t)fault_entry,	/* MemManage, BusFault */
	(uint32_t)fault_entry, 0, 0, 0, 0,		/* UsageFault */
	(uint32_t)fault_entry, (uint32_t)fault_entry, 0,
	(uint32_t)fault_entry, (uint32_t)fault_entry,
};
