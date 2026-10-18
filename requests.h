/*
 * requests.h: the USB requests this firmware answers, and the entries of a
 * register list (request 0x5A). This is the protocol a host speaks.
 */
#ifndef REQUESTS_H
#define REQUESTS_H

/* ------------------------------------------------------------------ */
/* Standard requests */
/* ------------------------------------------------------------------ */

#define REQ_GET_STATUS          0x00
#define REQ_CLEAR_FEATURE       0x01
#define REQ_SET_FEATURE         0x03
#define REQ_SET_ADDRESS         0x05
#define REQ_GET_DESCRIPTOR      0x06
#define REQ_SET_DESCRIPTOR      0x07
#define REQ_GET_CONFIGURATION   0x08
#define REQ_SET_CONFIGURATION   0x09
#define REQ_GET_INTERFACE       0x0A
#define REQ_SET_INTERFACE       0x0B
#define REQ_SYNCH_FRAME         0x0C

#define FEATURE_ENDPOINT_HALT   0

#define DESC_DEVICE             1
#define DESC_CONFIGURATION      2
#define DESC_STRING             3
#define DESC_DEVICE_QUALIFIER   6
#define DESC_OTHER_SPEED_CONFIG 7

/* ------------------------------------------------------------------ */
/* Vendor requests */
/* ------------------------------------------------------------------ */

/* The stock ones, see msi2500/cmd.txt */
#define REQ_BOOT                0x40    /* wValue bit0: 0 = ROM, 1 = RAM */
#define REQ_WRITE_REG           0x41    /* wValue/wIndex = MMIO reg+data */
#define REQ_READ_MEM            0x42    /* wIndex + 0xC000 (wrapping: 0x4000 up is 0x0000 up), up to
                                           64 B at once. wValue bit 0 maps the capture memory in,
                                           bit 1 reads internal RAM at wIndexL instead */
#define REQ_START_STREAM        0x43
#define REQ_WRITE_MEM           0x44    /* wValue = address, data stage */
#define REQ_STOP_STREAM         0x45

/* libmirisdr documents other requests, but my device doesn't respond to them.
 * We keep this region free and put our own requests starting from 0x51 */

#define REQ_PPS_ENABLE          0x52    /* bit 0 run, bit 1 take an anchor */
#define REQ_UART_TX             0x54    /* wValue = bit delay, data = bytes */
#define REQ_I2C_WRITE           0x55    /* wValue = addr|flags, wIndex = delay */
#define REQ_I2C_READ            0x56    /* same, data stage comes back */
#define REQ_I2C_STATUS          0x57    /* one byte, 0 = the last op was ACKed */
#define REQ_I2C_RESET           0x58    /* wIndex = delay: recover a stuck bus */
#define REQ_CALL                0x59    /* wValue = address, entry state in callCtx */
#define REQ_REG_LIST            0x5A    /* wValue bit 0 bank, bit 1 queue it, wIndex = port
                                           gap (0 default), data = list entries, none = stop */

/* wValueH flag shared by the two I2C transfer requests */
#define I2C_NO_STOP             0x01    /* leave the bus for a repeated start */
#define I2C_REPEAT              0x02    /* this transfer IS the repeated start */

/* ------------------------------------------------------------------ */
/* Register list entries */
/* ------------------------------------------------------------------ */

/* A list is 4 byte entries, [register or command, low, mid, high]. A register
   below 0x20 is written as request 0x41 would. A command takes the 16 bit
   argument in low and mid. */
#define LIST_WAIT_US            0x80    /* busy wait, microseconds, 16 bit */
#define LIST_WAIT_IRQ           0x81    /* streaming interrupts, 16 bit, counted from
                                           where the wait starts: a hop that ran late still
                                           gets them all, and the hops after it move */
#define LIST_REPEAT             0x82    /* back to the start, n more passes, 0 = forever */
#define LIST_SWITCH             0x83    /* to the other bank, if it is queued */
#define LIST_WAIT_SPI           0x84    /* the SPI master's done flags, at most n polls (~0.5 us
                                           each, 0 = 65536). A timeout stops the list */

#endif /* REQUESTS_H */
