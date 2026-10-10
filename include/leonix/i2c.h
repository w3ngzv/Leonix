/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_I2C_H
#define _LEONIX_I2C_H

#include <stdint.h>

void i2c_init(void);
int i2c_write(uint8_t addr, const uint8_t *buf, uint8_t len);
int i2c_read(uint8_t addr, uint8_t *buf, uint8_t len);

#endif /* _LEONIX_I2C_H */
