/*
 * bitbang.h: the pins and the bit period the bit banged serial shares, for a
 * UART transmitter (uart.c) and an I2C master (i2c.c).
 *
 * GPIO_1 = SDA, GPIO_2 = SCL, and the UART transmits on GPIO_2 too. They
 * coexist on the one wire: see the note at uartSend for why neither disturbs
 * the other.
 */
#ifndef BITBANG_H
#define BITBANG_H

#include <stdint.h>

#define PIN_SDA     0x02                /* GPIO_1 */
#define PIN_SCL     0x04                /* GPIO_2 */

#define GPIO_DIR(pin)   ((uint32_t)(pin) << 12)     /* val[15:12] */
#define GPIO_VAL(pin)   ((uint32_t)(pin) << 8)      /* val[11:8] */

extern __data uint8_t delayL, delayH;   /* bit period, from the host */

void bitDelay(void) __naked;

#endif /* BITBANG_H */
