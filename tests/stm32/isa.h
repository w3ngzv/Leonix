/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Leaf functions in isa_leaf.S; isa_leaf.S 中的叶函数.
 */
#ifndef TESTS_STM32_ISA_H
#define TESTS_STM32_ISA_H

#include <stdint.h>

uint32_t tbb_case(uint32_t idx);
uint32_t tbh_case(uint32_t idx);

uint32_t t_udf(uint32_t a, uint32_t b, uint32_t c);
uint32_t t_smlad(uint32_t a, uint32_t b, uint32_t c);
uint32_t t_qadd(uint32_t a, uint32_t b, uint32_t c);
uint32_t t_uadd8(uint32_t a, uint32_t b, uint32_t c);
uint32_t t_sel(uint32_t a, uint32_t b, uint32_t c);
uint32_t t_vadd(uint32_t a, uint32_t b, uint32_t c);
uint32_t t_bx(uint32_t a, uint32_t b, uint32_t c);
uint32_t t_udiv(uint32_t a, uint32_t b, uint32_t c);
uint32_t t_sdiv(uint32_t a, uint32_t b, uint32_t c);
uint32_t t_ldrd(uint32_t a, uint32_t b, uint32_t c);
uint32_t t_ldm(uint32_t a, uint32_t b, uint32_t c);
uint32_t t_ldr(uint32_t a, uint32_t b, uint32_t c);
uint32_t t_blx(uint32_t a, uint32_t b, uint32_t c);

#endif
