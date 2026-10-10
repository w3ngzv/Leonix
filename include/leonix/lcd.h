/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_LCD_H
#define _LEONIX_LCD_H

#include <stdint.h>

#define LCD_COLS	16
#define LCD_ROWS	2

int lcd_init(void);
int lcd_clear(void);
int lcd_set_cursor(uint8_t col, uint8_t row);
int lcd_puts(const char *s);
int lcd_puts_P(const char *s);
int lcd_check(void);
uint8_t lcd_address(void);

#endif /* _LEONIX_LCD_H */
