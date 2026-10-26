// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARMv7-M instruction checks.  isa_check.py recomputes each result from
 * the instruction descriptions in PM0056 chapter 3.  Table 20 of PM0056
 * lists every Cortex-M3 instruction; the DSP and FPU instructions of
 * ARMv7E-M are not in it, so they must fault.
 *
 * ARMv7-M 指令检查. isa_check.py 依 PM0056 第 3 章的指令说明重新计算每个
 * 结果. PM0056 表 20 列出 Cortex-M3 的全部指令, 其中没有 ARMv7E-M 的 DSP 与
 * FPU 指令, 这两类指令因此必须触发故障.
 */
#include "isa.h"
#include "runtime.h"

#define CCR		REG(0xe000ed14)
#define DEMCR		REG(0xe000edfc)
#define DWT_CTRL	REG(0xe0001000)
#define DWT_CYCCNT_ADDR	0xe0001004u

#define CCR_UNALIGN_TRP	(1u << 3)
#define CCR_DIV_0_TRP	(1u << 4)
#define DEMCR_TRCENA	(1u << 24)
#define DWT_CYCCNTENA	1u

#define BB_WORD		REG(SRAM_BASE + BB_OFFSET)
#define BB_ALIAS(bit)	REG(0x22000000u + (BB_OFFSET << 5) + 4u * (bit))
#define ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))

/* op codes, shared with stm32test.py; 操作码, 与 stm32test.py 共用 */
enum {
	OP_UDIV = 1, OP_SDIV, OP_CLZ, OP_RBIT, OP_REV, OP_REV16, OP_REVSH,
	OP_BFI_8_12, OP_BFC_4_8, OP_UBFX_5_9, OP_SBFX_5_9, OP_UBFX_0_32,
	OP_SSAT_8, OP_SSAT_1, OP_SSAT_32, OP_USAT_8, OP_USAT_0, OP_USAT_31,
	OP_UMULL, OP_SMULL, OP_UMLAL, OP_SMLAL, OP_MLA, OP_MLS,
	OP_LSLS, OP_LSRS, OP_ASRS, OP_RORS,
	OP_IT_LT, OP_IT_GT4, OP_IT_HI,
	OP_TBB, OP_TBH,
	OP_EXCL,
	OP_BITBAND,
	OP_UNALIGNED,
	OP_CYC_UDIV, OP_CYC_UMULL,
	OP_FAULT = 0x100,
};

enum {
	F_UDF = OP_FAULT, F_SMLAD, F_QADD, F_UADD8, F_SEL, F_VADD,
	F_BX_EVEN, F_XN_FETCH, F_UDIV0_TRAP, F_SDIV0_NOTRAP, F_LDRD_UNALIGNED,
	F_LDM_UNALIGNED, F_LDR_UNALIGN_TRP, F_LDR_ALIGNED_TRP,
};

static const uint32_t vals[] = {
	0, 1, 2, 3, 7, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff,
	0x12345678, 0x7fffffff, 0x80000000, 0x80000001, 0xfffffffe,
	0xffffffff, 0xdeadbeef, 0x0000fff0, 0xff00ff00, 0xfffffff9,
};
#define NVALS	ARRAY_SIZE(vals)

static const uint32_t shifts[] = { 0, 1, 4, 31, 32, 33, 40, 255, 256, 0x121 };
#define NSHIFTS	ARRAY_SIZE(shifts)

#define OP1(insn, a) ({ uint32_t r_; \
	asm volatile(insn " %0, %1" : "=r"(r_) : "r"(a)); r_; })
#define OP2(insn, a, b) ({ uint32_t r_; \
	asm volatile(insn " %0, %1, %2" : "=r"(r_) : "r"(a), "r"(b)); r_; })

/* clear Q, run, return APSR in *apsr; 清除 Q, 执行, 由 *apsr 返回 APSR */
#define SAT(insn, a, apsr) ({ uint32_t r_, z_ = 0; \
	asm volatile("msr APSR_nzcvq, %2\n\t" insn "\n\tmrs %1, APSR" \
			 : "=&r"(r_), "=&r"(*(apsr)) : "r"(z_), "r"(a) : "cc"); r_; })

#define SHIFT(insn, a, n, apsr) ({ uint32_t r_, z_ = 0; \
	asm volatile("msr APSR_nzcvq, %2\n\t" insn " %0, %3, %4\n\t" \
			 "mrs %1, APSR" \
			 : "=&r"(r_), "=&r"(*(apsr)) : "r"(z_), "0"(a), "r"(n) \
			 : "cc"); r_; })

static void check_alu(void)
{
	uint32_t i, j, r, apsr;

	for (i = 0; i < NVALS; i++)
		for (j = 0; j < NVALS; j++) {
			rec(OP_UDIV, vals[i], vals[j], 0,
			    OP2("udiv", vals[i], vals[j]), 0);
			rec(OP_SDIV, vals[i], vals[j], 0,
			    OP2("sdiv", vals[i], vals[j]), 0);
		}

	for (i = 0; i < NVALS; i++) {
		uint32_t a = vals[i], b = vals[NVALS - 1 - i];

		rec(OP_CLZ, a, 0, 0, OP1("clz", a), 0);
		rec(OP_RBIT, a, 0, 0, OP1("rbit", a), 0);
		rec(OP_REV, a, 0, 0, OP1("rev", a), 0);
		rec(OP_REV16, a, 0, 0, OP1("rev16", a), 0);
		rec(OP_REVSH, a, 0, 0, OP1("revsh", a), 0);

		r = a;
		asm volatile("bfi %0, %1, #8, #12" : "+r"(r) : "r"(b));
		rec(OP_BFI_8_12, a, b, 0, r, 0);
		r = a;
		asm volatile("bfc %0, #4, #8" : "+r"(r));
		rec(OP_BFC_4_8, a, 0, 0, r, 0);
		asm volatile("ubfx %0, %1, #5, #9" : "=r"(r) : "r"(a));
		rec(OP_UBFX_5_9, a, 0, 0, r, 0);
		asm volatile("sbfx %0, %1, #5, #9" : "=r"(r) : "r"(a));
		rec(OP_SBFX_5_9, a, 0, 0, r, 0);
		asm volatile("ubfx %0, %1, #0, #32" : "=r"(r) : "r"(a));
		rec(OP_UBFX_0_32, a, 0, 0, r, 0);

		r = SAT("ssat %0, #8, %3", a, &apsr);
		rec(OP_SSAT_8, a, 0, 0, r, apsr);
		r = SAT("ssat %0, #1, %3", a, &apsr);
		rec(OP_SSAT_1, a, 0, 0, r, apsr);
		r = SAT("ssat %0, #32, %3", a, &apsr);
		rec(OP_SSAT_32, a, 0, 0, r, apsr);
		r = SAT("usat %0, #8, %3", a, &apsr);
		rec(OP_USAT_8, a, 0, 0, r, apsr);
		r = SAT("usat %0, #0, %3", a, &apsr);
		rec(OP_USAT_0, a, 0, 0, r, apsr);
		r = SAT("usat %0, #31, %3", a, &apsr);
		rec(OP_USAT_31, a, 0, 0, r, apsr);
	}

	for (i = 0; i < NVALS; i++)
		for (j = i; j < NVALS; j += 3) {
			uint32_t a = vals[i], b = vals[j], c = vals[(i + j) % NVALS];
			uint32_t lo, hi;

			asm volatile("umull %0, %1, %2, %3"
					 : "=&r"(lo), "=&r"(hi) : "r"(a), "r"(b));
			rec(OP_UMULL, a, b, 0, lo, hi);
			asm volatile("smull %0, %1, %2, %3"
					 : "=&r"(lo), "=&r"(hi) : "r"(a), "r"(b));
			rec(OP_SMULL, a, b, 0, lo, hi);
			lo = c;
			hi = c ^ 0x5a5a5a5au;
			asm volatile("umlal %0, %1, %2, %3"
					 : "+r"(lo), "+r"(hi) : "r"(a), "r"(b));
			rec(OP_UMLAL, a, b, c, lo, hi);
			lo = c;
			hi = c ^ 0x5a5a5a5au;
			asm volatile("smlal %0, %1, %2, %3"
					 : "+r"(lo), "+r"(hi) : "r"(a), "r"(b));
			rec(OP_SMLAL, a, b, c, lo, hi);
			asm volatile("mla %0, %1, %2, %3"
					 : "=r"(lo) : "r"(a), "r"(b), "r"(c));
			rec(OP_MLA, a, b, c, lo, 0);
			asm volatile("mls %0, %1, %2, %3"
					 : "=r"(lo) : "r"(a), "r"(b), "r"(c));
			rec(OP_MLS, a, b, c, lo, 0);
		}

	for (i = 0; i < NVALS; i++)
		for (j = 0; j < NSHIFTS; j++) {
			uint32_t a = vals[i], n = shifts[j];

			r = SHIFT("lsls", a, n, &apsr);
			rec(OP_LSLS, a, n, 0, r, apsr);
			r = SHIFT("lsrs", a, n, &apsr);
			rec(OP_LSRS, a, n, 0, r, apsr);
			r = SHIFT("asrs", a, n, &apsr);
			rec(OP_ASRS, a, n, 0, r, apsr);
			r = SHIFT("rors", a, n, &apsr);
			rec(OP_RORS, a, n, 0, r, apsr);
		}
}

static void check_flow(void)
{
	uint32_t i, j, r;

	for (i = 0; i < NVALS; i += 2)
		for (j = 0; j < NVALS; j += 3) {
			uint32_t a = vals[i], b = vals[j];

			asm volatile("cmp %1, %2\n\tite lt\n\t"
					 "movlt %0, #1\n\tmovge %0, #2"
					 : "=r"(r) : "r"(a), "r"(b) : "cc");
			rec(OP_IT_LT, a, b, 0, r, 0);
			asm volatile("movs %0, #0\n\tcmp %1, %2\n\t"
					 "ittee gt\n\t"
					 "addgt %0, %0, #1\n\taddgt %0, %0, #2\n\t"
					 "addle %0, %0, #4\n\taddle %0, %0, #8"
					 : "=&r"(r) : "r"(a), "r"(b) : "cc");
			rec(OP_IT_GT4, a, b, 0, r, 0);
			asm volatile("cmp %1, %2\n\tite hi\n\t"
					 "movhi %0, #1\n\tmovls %0, #2"
					 : "=r"(r) : "r"(a), "r"(b) : "cc");
			rec(OP_IT_HI, a, b, 0, r, 0);
		}

	for (i = 0; i < 4; i++) {
		rec(OP_TBB, i, 0, 0, tbb_case(i), 0);
		rec(OP_TBH, i, 0, 0, tbh_case(i), 0);
	}
}

static void check_memory(void)
{
	volatile uint32_t word = 0x13579bdfu;
	uint32_t v, s1, s2, s3, i;

	/*
	 * STREX succeeds after LDREX and fails after CLREX or with no
	 * reservation.
	 *
	 * LDREX 之后 STREX 成功, CLREX 之后或没有保留时 STREX 失败.
	 */
	asm volatile("ldrex %0, [%4]\n\t"
			 "strex %1, %5, [%4]\n\t"
			 "clrex\n\t"
			 "strex %2, %6, [%4]\n\t"
			 "strex %3, %6, [%4]"
			 : "=&r"(v), "=&r"(s1), "=&r"(s2), "=&r"(s3)
			 : "r"(&word), "r"(0x2468ace0u), "r"(0xbad0bad0u)
			 : "memory");
	rec(OP_EXCL, v, s1, s2, s3, word);

	/* bit-band alias, RM0008 3.3.2; 位带别名, 见 RM0008 3.3.2 */
	BB_WORD = 0;
	BB_ALIAS(5) = 1;
	BB_ALIAS(31) = 0xfffffffe;	/* only bit 0 counts; 只有第 0 位有效 */
	BB_ALIAS(0) = 0xffffffff;
	rec(OP_BITBAND, BB_WORD, BB_ALIAS(5), BB_ALIAS(6), BB_ALIAS(0), 0);
	BB_ALIAS(5) = 0;
	rec(OP_BITBAND, BB_WORD, BB_ALIAS(5), BB_ALIAS(6), BB_ALIAS(0), 1);

	for (i = 0; i < 16; i++)
		SCRATCH[i] = (uint8_t)(i + 1);
	asm volatile("ldr %0, [%1]" : "=r"(v) : "r"(SCRATCH + 1));
	asm volatile("ldrh %0, [%1]" : "=r"(s1) : "r"(SCRATCH + 3));
	asm volatile("ldrsh %0, [%1]" : "=r"(s2) : "r"(SCRATCH + 3));
	rec(OP_UNALIGNED, 1, 3, s2, v, s1);
}

static void try(uint32_t op, uint32_t (*fn)(uint32_t, uint32_t, uint32_t),
		uint32_t a, uint32_t b, uint32_t c)
{
	uint32_t before = HDR[H_FAULTS], r;

	last_cfsr = last_hfsr = 0;
	r = fn(a, b, c);
	rec(op, a, HDR[H_FAULTS] - before, r, last_cfsr, last_hfsr);
}

static void even_target(void)
{
}

static void check_faults(void)
{
	try(F_UDF, t_udf, 0, 0, 0);
	try(F_SMLAD, t_smlad, 0x00030002, 0x00050004, 7);
	try(F_QADD, t_qadd, 0x7fffffff, 1, 0);
	try(F_UADD8, t_uadd8, 0x01ff7f80, 0x01018080, 0);
	try(F_SEL, t_sel, 0x11111111, 0x22222222, 0);
	try(F_VADD, t_vadd, 0x3f800000, 0x40000000, 0);	/* 1.0f + 2.0f */
	try(F_BX_EVEN, t_bx, (uint32_t)even_target & ~1u, 0, 0);
	try(F_XN_FETCH, t_blx, 0xe0000001u, 0, 0);

	CCR |= CCR_DIV_0_TRP;
	try(F_UDIV0_TRAP, t_udiv, 100, 0, 0);
	CCR &= ~CCR_DIV_0_TRP;
	try(F_SDIV0_NOTRAP, t_sdiv, 100, 0, 0);

	try(F_LDRD_UNALIGNED, t_ldrd, (uint32_t)(SCRATCH + 1), 0, 0);
	try(F_LDM_UNALIGNED, t_ldm, (uint32_t)(SCRATCH + 1), 0, 0);
	CCR |= CCR_UNALIGN_TRP;
	try(F_LDR_UNALIGN_TRP, t_ldr, (uint32_t)(SCRATCH + 1), 0, 0);
	try(F_LDR_ALIGNED_TRP, t_ldr, (uint32_t)(SCRATCH + 4), 0, 0);
	CCR &= ~CCR_UNALIGN_TRP;
}

/*
 * Cycle counts from DWT_CYCCNT.  The same sequence with a MOV in place of
 * the measured instruction gives the overhead; the record keeps both raw
 * deltas and the host subtracts.  QEMU has no DWT, so there both read 0.
 *
 * 由 DWT_CYCCNT 计周期. 把被测指令换成 MOV 的同一序列给出额外开销; 记录保留
 * 两个原始差值, 由主机相减. QEMU 没有 DWT, 两者在该模拟器中均为 0.
 */
static void check_timing(void)
{
	static const uint32_t divs[][2] = {
		{ 0, 1 }, { 1, 1 }, { 0xff, 1 }, { 0xffff, 1 }, { 0xffffff, 1 },
		{ 0xffffffff, 1 }, { 0xffffffff, 0xffff }, { 0xffffffff, 0xffffffff },
		{ 0x80000000, 3 }, { 100, 0 }, { 7, 0x80000000 },
	};
	uint32_t i, t0, t1, t2, t3, q, lo, hi;
	volatile uint32_t *cyc = (volatile uint32_t *)DWT_CYCCNT_ADDR;

	DEMCR |= DEMCR_TRCENA;
	DWT_CTRL |= DWT_CYCCNTENA;

	for (i = 0; i < ARRAY_SIZE(divs); i++) {
		uint32_t a = divs[i][0], b = divs[i][1];

		asm volatile("ldr %0, [%3]\n\tudiv %1, %4, %5\n\tldr %2, [%3]"
				 : "=&r"(t0), "=&r"(q), "=&r"(t1)
				 : "r"(cyc), "r"(a), "r"(b));
		asm volatile("ldr %0, [%3]\n\tmov %1, %4\n\tldr %2, [%3]"
				 : "=&r"(t2), "=&r"(q), "=&r"(t3)
				 : "r"(cyc), "r"(a), "r"(b));
		rec(OP_CYC_UDIV, a, b, 0, t1 - t0, t3 - t2);
	}
	for (i = 0; i < NVALS; i += 3) {
		uint32_t a = vals[i], b = vals[NVALS - 1 - i];

		asm volatile("ldr %0, [%4]\n\tumull %1, %2, %5, %6\n\t"
				 "ldr %3, [%4]"
				 : "=&r"(t0), "=&r"(lo), "=&r"(hi), "=&r"(t1)
				 : "r"(cyc), "r"(a), "r"(b));
		asm volatile("ldr %0, [%3]\n\tmov %1, %4\n\tldr %2, [%3]"
				 : "=&r"(t2), "=&r"(q), "=&r"(t3)
				 : "r"(cyc), "r"(a), "r"(b));
		rec(OP_CYC_UMULL, a, b, 0, t1 - t0, t3 - t2);
	}
}

void suite_main(void)
{
	set_phase(1);
	check_alu();
	set_phase(2);
	check_flow();
	set_phase(3);
	check_memory();
	set_phase(4);
	check_faults();
	set_phase(5);
	check_timing();
}
