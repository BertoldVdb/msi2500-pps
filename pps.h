/*
 * pps.h: the PPS timestamper's state that the host reads, the poll loop's
 * counters, and the guard that says whether a capture can be trusted.
 *
 * The capture is spread over three places for its timing: the poll loop here
 * counts, the streaming interrupt (stream.c) closes each interval, and the USB
 * interrupt (usb.c) latches SOF edges inline.
 */
#ifndef PPS_H
#define PPS_H

#include <stdint.h>

/* PPS state for the host to read */
struct ppsState {
    uint8_t  lastTickL, lastTickH;
    uint32_t irqCount;                  /* packet interrupts since stream start */
    uint8_t  edgeTickL, edgeTickH;
    uint32_t edgeIrq;
    uint8_t  run;                       /* ppsRun, which the poll loop tests as a bit */
    uint8_t  edgeUsbGuard, edgeCount, edgeFirstCarry;
    uint8_t  edgeFrameL, edgeFrameH;    /* USB frame the edge fell in */
    uint8_t  spare;
    uint8_t  ppsDivReload;              /* latch every Nth edge */
    uint8_t  lastSofCount, edgeSofCount, edgeFirstL, edgeFirstH;
};

/* One-shot anchor for the host to read */
struct anchorState {
    uint8_t  anchor[16];
    uint8_t  valid;
};

extern __data struct ppsState ppsBlk;
extern __xdata struct anchorState anchorBlk;

#define lastTickL       ppsBlk.lastTickL
#define lastTickH       ppsBlk.lastTickH
#define irqCount        ppsBlk.irqCount
#define edgeTickL       ppsBlk.edgeTickL
#define edgeTickH       ppsBlk.edgeTickH
#define edgeIrq         ppsBlk.edgeIrq
#define edgeUsbGuard    ppsBlk.edgeUsbGuard
#define edgeCount       ppsBlk.edgeCount
#define edgeFirstCarry  ppsBlk.edgeFirstCarry
#define edgeFrameL      ppsBlk.edgeFrameL
#define edgeFrameH      ppsBlk.edgeFrameH
#define ppsDivReload    ppsBlk.ppsDivReload
#define lastSofCount    ppsBlk.lastSofCount
#define edgeSofCount    ppsBlk.edgeSofCount
#define edgeFirstL      ppsBlk.edgeFirstL
#define edgeFirstH      ppsBlk.edgeFirstH
#define anchor          anchorBlk.anchor
#define anchorValid     anchorBlk.valid

/* ppsRun with its copy for the host */
#define PPS_RUN(v)      do { ppsRun = (v); ppsBlk.run = (v); } while (0)

extern __data uint8_t  tickL, tickH; /* both count down: DJNZ is the cheapest count, and
                                        tickH reaching zero again is 65536 turns with no
                                        packet, the poll loop's own way out */
extern __bit ppsRun;                 /* poll loop runs only while this is set */
extern __bit ppsSrcSof;              /* edges from the SOF interrupt, not GPIO_0 */
extern __data uint8_t sofDiv, sofLastFrame;
/* SOF interrupts since the last streaming interrupt, and the tick the first of
   them fell on. A host places the edge from that first SOF plus a whole number
   of microframes, so what the handlers in between cost never enters the answer. */
extern __data uint8_t sofInInterval;
extern __data uint8_t firstSofL, firstSofH, firstSofCarry;
extern volatile __bit inSofLatch;    /* the USB handler is running: a packet interrupt
                                        taken late in here marks the interval */
extern volatile __bit sofStraddle;   /* ...and a streaming interrupt landed there */
extern volatile __bit tickCarried;   /* the interval opened on the loop's carry */
extern __data uint8_t ppsDiv;                   /* counts down to the Nth edge */

extern volatile __xdata uint8_t anchorPending;   /* the ISR clears it while a handler waits */

/* A USB interrupt freezes the poll loop ~19 us against the streaming one's
   ~2.3 us and leaves `ticks` looking normal, so captures near one are thrown
   away. */
extern __data uint8_t usbGuard;

/* Guard values that are markers rather than a count. The ordinary ones are a
   countdown of streaming interrupts, so they stay plain numbers. */
#define GUARD_HELD      0xFF    /* a bit banged transfer is running */
#define GUARD_CARRY     0xFE    /* the loop was between tickL and its carry */
#define GUARD_STRADDLE  0xFD    /* the interval opened inside a SOF latch */
#define GUARD_BASE      0xFB    /* ...or on the poll loop's carry */

/* The guard says a capture is not to be trusted, and a USB interrupt is not the
   only thing that earns that: the streaming interrupt takes far longer than
   usual on the paths below, and those turns come out of the interval a capture
   is placed against. 0xFF is a bit banged transfer holding it, so leave it. */
#define STREAM_GUARD()  do { if (usbGuard != GUARD_HELD) usbGuard = 2; } while (0)

/* from main, while ppsRun is set */
void ppsPoll(void) __naked;

/* request 0x52 */
void ppsRequest(void);

#endif /* PPS_H */
