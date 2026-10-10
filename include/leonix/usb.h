/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LEONIX_USB_H
#define _LEONIX_USB_H

#include <stdint.h>

int usb_init(void);
uint8_t usb_general_interrupt(void);
uint8_t usb_endpoint_interrupt(void);

#endif /* _LEONIX_USB_H */
