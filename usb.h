/*
 * usb.h: the USB device controller's interrupt, its bus reset and the
 * endpoint index
 */
#ifndef USB_H
#define USB_H

#include <stdint.h>
#include "msi2500.h"

#define INTRUSBE_BASE   (INTRUSB_SUSPEND | INTRUSB_RESUME | INTRUSB_RESET)

/* The vector: main.c sees this prototype */
void usbIsr(void) __interrupt (2) __using (2);

/* select endpoint ep's registers. On 1 the streaming interrupt is held off */
void usbIndex(uint8_t ep);

#endif /* USB_H */
