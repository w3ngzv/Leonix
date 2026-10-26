/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Leaf functions for isa.c, declared in isa.h.  Those for the fault
 * checks take r0 to r2, return r0 and leave LR alone, so that the fault
 * handler can resume at LR.  The DSP and FPU instructions are assembled
 * for ARMv7E-M; on a Cortex-M3 they must fault.
 *
 * isa.c 所用的叶函数, 声明见 isa.h. 故障检查所用的函数以 r0 至 r2 为参数,
 * 以 r0 返回, 不改动 LR, 故障处理程序因此可以从 LR 处继续执行. DSP 与 FPU
 * 指令按 ARMv7E-M 汇编; 在 Cortex-M3 上必须触发故障.
 */
	.syntax unified
	.thumb
	.text

	.macro leaf name
	.align 1
	.global \name
	.thumb_func
\name:
	.endm

leaf tbb_case
	tbb	[pc, r0]
1:	.byte	(2f - 1b) / 2, (3f - 1b) / 2, (4f - 1b) / 2, (5f - 1b) / 2
2:	movs	r0, #10
	bx	lr
3:	movs	r0, #20
	bx	lr
4:	movs	r0, #30
	bx	lr
5:	movs	r0, #40
	bx	lr

leaf tbh_case
	tbh	[pc, r0, lsl #1]
1:	.hword	(2f - 1b) / 2, (3f - 1b) / 2, (4f - 1b) / 2, (5f - 1b) / 2
2:	movs	r0, #11
	bx	lr
3:	movs	r0, #21
	bx	lr
4:	movs	r0, #31
	bx	lr
5:	movs	r0, #41
	bx	lr

leaf t_udf
	udf	#0
	bx	lr

	.arch	armv7e-m
	.fpu	fpv4-sp-d16
leaf t_smlad
	smlad	r0, r0, r1, r2
	bx	lr
leaf t_qadd
	qadd	r0, r0, r1
	bx	lr
leaf t_uadd8
	uadd8	r0, r0, r1
	bx	lr
leaf t_sel
	sel	r0, r0, r1
	bx	lr
leaf t_vadd
	vmov	s0, r0
	vmov	s1, r1
	vadd.f32 s0, s0, s1
	vmov	r0, s0
	bx	lr
	.fpu	softvfp
	.arch	armv7-m

leaf t_bx
	bx	r0
	bx	lr
leaf t_udiv
	udiv	r0, r0, r1
	bx	lr
leaf t_sdiv
	sdiv	r0, r0, r1
	bx	lr
leaf t_ldrd
	ldrd	r0, r1, [r0]
	bx	lr
leaf t_ldm
	ldm	r0, {r0, r1}
	bx	lr
leaf t_ldr
	ldr	r0, [r0]
	bx	lr

/* BLX overwrites LR, so it is saved; BLX 会覆盖 LR, 因此先保存 */
leaf t_blx
	push	{lr}
	blx	r0
	pop	{pc}
