/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ARM_USART_H
#define _ASM_ARM_USART_H

#include <stdint.h>

void usart_panic_write(const char *s, uint8_t len);

#endif /* _ASM_ARM_USART_H */
