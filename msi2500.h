/*
 * msi2500.h - hardware definitions for the Mirics MSi2500 on-chip 8051
 *
 * Memory map. MOVC and MOVX mostly see the same address space, but not
 * everywhere: the USB controller is invisible to a code fetch, which reads a
 * fixed FA F5 F5 F5 throughout 0x4000-0x7FFF. RAM, the ROM mirror and the ADC
 * block read identically either way. 
 *
 *   0x0000-0x0FFF  ROM0 / RAM0   (code: the ROM image is mirrored at
 *                                 0x2000 and 0x3000)
 *   0x1000-0x1FFF  RAM1          (this firmware puts its xdata at 0x1800)
 *   0x4000-0x41FF  USB device controller (Mentor MUSB), 0x100 bytes mirrored
 *   0x8000-0xBFFF  USB controller FIFO RAM (EP0 + EP1, P0.2 low)
 *   0xC000-0xDFFF  ADC / tuner / GPIO block, 0x100 bytes mirrored
 *
 * When booting from RAM (BOOTSRC=1) the whole of 0x0000-0x1FFF is writable,
 * so code must stay below the xdata area at 0x1800.
 */
#ifndef MSI2500_H
#define MSI2500_H

#include <stdint.h>

volatile __sbit __at (0x80) P0_0;
volatile __sbit __at (0x81) P0_1;
volatile __sbit __at (0x82) P0_2;

volatile __sfr __at (0x87) PCON;

volatile __sfr  __at (0x88) TCON;


volatile __sfr  __at (0xA8) IE;
volatile __sbit __at (0xA8) EX0;
volatile __sbit __at (0xAF) EA;
volatile __sfr  __at (0xB8) IP;


volatile __sfr __at (0xF0) B;
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Non-standard SFRs                                                  */
/* ------------------------------------------------------------------ */

/* Boot source latch: 0 = boot from internal ROM, 1 = boot from RAM0.   */
volatile __sfr __at (0xC4) BOOTSRC;
/* Writing 1 resets the 8051 core, it restarts from BOOTSRC.            */
volatile __sfr __at (0xC5) CORERESET;

/* ------------------------------------------------------------------ */
/* Control signals driven by the 8051                                 */
/* ------------------------------------------------------------------ */

/* Enable/disable the streaming block */
#define STREAM_ENABLE(on)   (P0_1 = (on))

/* Acknowledge streaming buffers */
#define STREAM_STROBE()     do { P0_0 = 0; P0_0 = 1; } while (0)

/* P0.2 arbitrates the memory the core shares with the USB controller and the
 * capture engine. Taking it LOW maps two windows into xdata. */
#define MEM_BUF_MAP(on)     (P0_2 = !(on))

/* ------------------------------------------------------------------ */
/* USB device controller (Mentor Graphics "MUSB" style register file)  */
/* ------------------------------------------------------------------ */

#define USB_BASE        0x4000

#define USB_FADDR       (*(__xdata volatile uint8_t *)(USB_BASE + 0x00))
#define USB_POWER       (*(__xdata volatile uint8_t *)(USB_BASE + 0x01))
#define USB_INTRTX      (*(__xdata volatile uint8_t *)(USB_BASE + 0x02))
#define USB_INTRTXE_L   (*(__xdata volatile uint8_t *)(USB_BASE + 0x06))
#define USB_INTRTXE_H   (*(__xdata volatile uint8_t *)(USB_BASE + 0x07))
#define USB_INTRRXE_L   (*(__xdata volatile uint8_t *)(USB_BASE + 0x08))
#define USB_INTRRXE_H   (*(__xdata volatile uint8_t *)(USB_BASE + 0x09))
#define USB_INTRUSB     (*(__xdata volatile uint8_t *)(USB_BASE + 0x0A))
#define USB_INTRUSBE    (*(__xdata volatile uint8_t *)(USB_BASE + 0x0B))
/* The USB frame number, incremented by every SOF the host sends.  11 bits, so
   bit 0 of the low byte is a 500 Hz square wave and the whole value wraps every
   2.048 s.  A common register: INDEX does not select it. */
#define USB_FRAME_L     (*(__xdata volatile uint8_t *)(USB_BASE + 0x0C))
#define USB_FRAME_H     (*(__xdata volatile uint8_t *)(USB_BASE + 0x0D))
#define USB_INDEX       (*(__xdata volatile uint8_t *)(USB_BASE + 0x0E))
#define USB_TXMAXP_L    (*(__xdata volatile uint8_t *)(USB_BASE + 0x10))
#define USB_TXMAXP_H    (*(__xdata volatile uint8_t *)(USB_BASE + 0x11))
#define USB_CSR0        (*(__xdata volatile uint8_t *)(USB_BASE + 0x12))
/* Same address as CSR0, but that is what offset 0x12 means when INDEX
 * selects an endpoint other than 0.                                    */
#define USB_TXCSRL      (*(__xdata volatile uint8_t *)(USB_BASE + 0x12))
#define USB_TXCSRH      (*(__xdata volatile uint8_t *)(USB_BASE + 0x13))
#define USB_COUNT0      (*(__xdata volatile uint8_t *)(USB_BASE + 0x18))
#define USB_FIFO0       ((__xdata volatile uint8_t *)(USB_BASE + 0x20))

/* The EP0 FIFO is a four byte wide window; every access advances the
 * FIFO pointer, so a byte stream is written/read as FIFO0[n & 3].      */
#define USB_FIFO0_BYTE(n)   USB_FIFO0[(n) & 3]

/* POWER */
#define PWR_ENSUSPEND   0x01
#define PWR_HSMODE      0x10
#define PWR_HSENAB      0x20
#define PWR_SOFTCONN    0x40

#define PWR_RUN         (PWR_SOFTCONN | PWR_HSENAB)              /* 0x60 */
#define PWR_SUSPEND     (PWR_SOFTCONN | PWR_HSENAB | PWR_ENSUSPEND) /* 0x61 */
#define PWR_DISCONNECT  (PWR_HSENAB)                             /* 0x20 */

/* INTRUSB */
#define INTRUSB_SUSPEND 0x01
#define INTRUSB_RESUME  0x02
#define INTRUSB_RESET   0x04
#define INTRUSB_SOF     0x08

/* INTRTX */
#define INTRTX_EP0      0x01

/* CSR0 (peripheral mode) */
#define CSR0_RXPKTRDY       0x01
#define CSR0_TXPKTRDY       0x02
#define CSR0_SENTSTALL      0x04
#define CSR0_DATAEND        0x08
#define CSR0_SETUPEND       0x10
#define CSR0_SENDSTALL      0x20
#define CSR0_SERV_RXPKTRDY  0x40
#define CSR0_SERV_SETUPEND  0x80

/* TXCSRL */
#define TXCSRL_TXPKTRDY     0x01
#define TXCSRL_FLUSHFIFO    0x08
#define TXCSRL_SENDSTALL    0x10
#define TXCSRL_CLRDATATOG   0x40

/* TXCSRH */
#define TXCSRH_FRCDATATOG   0x10
#define TXCSRH_ISO          0x40

/* ------------------------------------------------------------------ */
/* ADC / tuner / GPIO block                                            */
/* ------------------------------------------------------------------ */

#define MMIO_WR_REG     (*(__xdata volatile uint8_t *)0xC000)
#define MMIO_WR_LOW     (*(__xdata volatile uint8_t *)0xC001)   /* val[7:0]   */
#define MMIO_WR_MID     (*(__xdata volatile uint8_t *)0xC002)   /* val[15:8]  */
#define MMIO_WR_HIGH    (*(__xdata volatile uint8_t *)0xC003)   /* val[23:16] */
#define MMIO_RD(n)      ((__xdata volatile uint8_t *)(0xC000 + 4 * (n)))

#define MMIO_GPIO_IN    (*MMIO_RD(6))

/* Write register numbers used by this firmware */
#define MMIO_REG_ANALOG     0x03  

/* This value is written on USB suspend. Copied from libmirisdr */
#define MMIO_ANALOG_STANDBY 0x010000UL

#define MMIO_REG_TUNER      0x09    /* the MSi001's serial port, ~2.15 us a word */
#define MMIO_REG_GPIO       0x08    /* val bit 7 picks the GPIO supply, 1.8 V or 1.2 V,
                                     * val[15:12] = direction (1 = output),
                                     * val[11:8]  = output value         */

/* What the shadow starts at: supply bit set, every pin an input */
#define MMIO_GPIO_IDLE      0x000080UL

#endif /* MSI2500_H */
