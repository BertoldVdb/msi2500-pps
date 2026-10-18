/*
 * ep0.h: endpoint 0, with the setup packet, its data stages and the request
 * dispatch
 */
#ifndef EP0_H
#define EP0_H

#include <stdint.h>

#define EP0_MAXPACKET           64

/* Endpoint 0 driver state */
#define EP0_IDLE                0
#define EP0_TX                  1       /* sending a long IN transfer */
#define EP0_RX                  2       /* collecting an OUT data stage */

/* What to do when an OUT data stage finishes */
#define ACT_NONE                0
#define ACT_UART                1
#define ACT_I2C_WRITE           2
#define ACT_REG_LIST            3

#define XFER_BUF_LEN            64

/* The active setup packet. */
/* EP0 bookkeeping: cold next to the capture path, and the 128 bytes sdcc can
   address directly are the scarce ones, so these live in xdata. */
struct setupPacket {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint8_t  wValueL;
    uint8_t  wValueH;
    uint8_t  wIndexL;
    uint8_t  wIndexH;
    uint16_t wLength;
};

extern __xdata struct setupPacket setup;
extern __xdata uint8_t ep0State;

/* Work for ep0Service(), tested by the USB interrupt on each pass */
#define EP0_PENDING(c) ((((c) & (CSR0_RXPKTRDY | CSR0_SETUPEND | CSR0_SENTSTALL)) != 0) \
                        || (ep0State == EP0_TX && !((c) & CSR0_TXPKTRDY)))

extern __xdata uint8_t *__data ep0Ptr;  /* where an OUT data stage goes, and how far it got */
extern __code  uint8_t *__data ep0CodePtr;  /* what an IN data stage sends */
extern __xdata uint8_t ep0TypeByte;     /* sent in place of byte 1, a descriptor's type */
extern __xdata uint8_t xferBuf[XFER_BUF_LEN];   /* the bit banged transfers' and memory reads' */

void ep0StartTx(uint16_t length);
void ep0StartRx(__xdata uint8_t *to, uint8_t remap, uint8_t action);
void ep0Stall(void);
void ep0Ack(void);
void ep0SendByte(uint8_t value);

/* from the USB interrupt */
void ep0Service(void);
void ep0Reset(void);

#endif /* EP0_H */
