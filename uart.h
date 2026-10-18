/*
 * uart.h: the bit banged UART transmitter
 */
#ifndef UART_H
#define UART_H

#include <stdint.h>

/* the data stage has arrived in xferBuf, n bytes */
void uartSend(uint8_t n);

/* request 0x54 */
void uartRequest(void);

#endif /* UART_H */
