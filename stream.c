/*
 * stream.c: the streaming endpoint 0x81, with the capture engine's interrupt,
 * starting and stopping it, the halt feature and the alternate settings
 */
#include "msi2500.h"
#include "ep0.h"
#include "list.h"
#include "pps.h"
#include "requests.h"
#include "stream.h"
#include "usb.h"

volatile __bit stopPending;          /* stop at the next interrupt, not here */
volatile __bit streamParked;         /* one buffer handed over but not acked */
__bit streamOn = 0;

__xdata uint8_t epHalted = 0;        /* endpoint 0x81 halted by the host */
__xdata uint8_t altSetting;

void streamEnable(uint8_t on)
{
    streamOn = (on != 0);
    STREAM_ENABLE(on);
}

/* Bytes 8-15 of each buffer's header are stale RAM the engine never writes, so
   we use them to mark the buffers */
static const __code uint8_t stampBytes[4] = { 'B', 'V', 'D', 'B' };

static void stampBuffers(void)
{
    __xdata uint8_t *b = (__xdata uint8_t *)0xE000;
    uint8_t i, j;

    MEM_BUF_MAP(1);

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++)
            b[8 + j] = stampBytes[j];

        b[12] = i;                  /* which of the four buffers this is */
        b += 0x400;                 /* 13-15 left spare, still stale RAM */
    }

    MEM_BUF_MAP(0);
}

void streamStop(void)
{
    streamEnable(0);

    usbIndex(1);
    /* Two flushes for double buffered endpoint */
    USB_TXCSRL = TXCSRL_FLUSHFIFO;
    USB_TXCSRL = TXCSRL_FLUSHFIFO;
    usbIndex(0);
}

void streamHalt(uint8_t set)
{
    if (setup.wValueL != FEATURE_ENDPOINT_HALT || setup.wValueH != 0 ||
        (setup.bmRequestType & 0x1F) != 2 ||
        setup.wIndexL != 0x81 || setup.wIndexH != 0 ||
        altSetting != 3) {
        ep0Stall();
        return;
    }

    epHalted = set;

    {
        usbIndex(1);
        if (set) {
            USB_TXCSRL = TXCSRL_SENDSTALL;
        } else {
            /* Clearing a halt must also reset the data toggle (USB 2.0 9.4.5). */
            USB_TXCSRL = TXCSRL_CLRDATATOG;

            /* Whatever the engine handed over while the endpoint was halted is
               still sitting there unacked. Release it. */
            if (streamParked) {
                streamParked = 0;
                USB_TXCSRL = TXCSRL_TXPKTRDY;
            }
        }
        usbIndex(0);
    }

    if (!set && streamOn) {
            STREAM_STROBE();
    }

    ep0Ack();
}

/* Stopped, unhalted and back to alt 0, for a bus reset and SET_CONFIGURATION */
void streamReset(void)
{
    streamStop();
    stopPending = 0;
    streamParked = 0;
    epHalted = 0;
    altSetting = 0;

    usbIndex(1);
    USB_TXCSRH   = 0;
    USB_TXMAXP_H = 0;
    USB_TXMAXP_L = 0;
    usbIndex(0);
}

/* The alternate settings, as the configuration descriptor (desc.c) lists them.
   ep0.c has checked the interface. */
void streamSetInterface(void)
{
    uint8_t alt = setup.wValueL;

    if (setup.wValueH != 0 || alt > 4) {
        ep0Stall();                     /* no such setting, the current one stays */
        return;
    }
    altSetting = alt;

    usbIndex(1);
    switch (alt) {
    case 0:                     /* endpoint off */
        USB_TXCSRH   = 0x00;
        USB_TXMAXP_H = 0x00;
        break;
    case 1:                     /* isochronous, 3 x 1024 per uframe */
        USB_TXCSRH   = TXCSRH_ISO | TXCSRH_FRCDATATOG;
        USB_TXMAXP_H = 0x14;
        break;
    case 2:                     /* isochronous, 1024 per uframe */
        USB_TXCSRH   = TXCSRH_ISO | TXCSRH_FRCDATATOG;
        USB_TXMAXP_H = 0x04;
        break;
    case 3:                     /* bulk, 512 */
        USB_TXCSRH   = TXCSRH_FRCDATATOG;
        USB_TXMAXP_H = 0x3A;
        break;
    case 4:                     /* isochronous, 2 x 1024 per uframe */
        USB_TXCSRH   = TXCSRH_ISO | TXCSRH_FRCDATATOG;
        USB_TXMAXP_H = 0x0C;
        break;
    }
    USB_TXMAXP_L = 0;
    usbIndex(0);

    ep0Ack();
}

void streamStartRequest(void)
{
    irqCount = 0;
    tickL = 0;
    tickH = 0;

    /* Parked, the engine is holding one finished buffer */
    if (!streamParked) streamStop();

    /* Mark the buffers, it is redone on each start as configuring very
     * high sample rates can overwrite them */
    EA = 0;
    stampBuffers();
    EA = 1;
    streamParked = 0;

    streamEnable(1);
    STREAM_STROBE();
    ep0Ack();
}

void streamStopRequest(void)
{
    if (streamOn) {
        stopPending = 1;

        for (volatile uint16_t wait = 65535; wait; wait--) {
            if (!stopPending) break;
        }

        if (stopPending) {
            stopPending = 0;
            streamStop();
        }
    }
    ep0Ack();
}

void streamIsr(void) __interrupt (0) __using (1)
{
    if (!streamOn) {
        return;
    }


    lastTickL = tickL;
    /* Counts down so invert it */
    lastTickH = (uint8_t)(0 - tickH);
    tickL = 0;
    tickH = 0;
    lastSofCount = sofInInterval;
    sofInInterval = 0;
    sofStraddle = inSofLatch;
    tickCarried = !lastTickL;
    if (usbGuard && usbGuard != GUARD_HELD) usbGuard--;
    irqCount++;

    if (listWaiting && !--listCount) {
        listWaiting = 0;
        listDue = 1;                    /* main wakes from idle on this interrupt */
    }

    if (anchorPending) {
        __xdata uint8_t *b = (__xdata uint8_t *)0xE000;
        uint8_t i;
        MEM_BUF_MAP(1);
        for (i = 0; i < 16; i += 4) {
            anchor[i + 0] = b[0];
            anchor[i + 1] = b[1];
            anchor[i + 2] = b[2];
            anchor[i + 3] = b[3];
            b += 0x400;                 /* four 1 kB buffers */
        }
        MEM_BUF_MAP(0);
        irqCount = 0;
        anchorPending = 0;
        anchorValid = 1;
        STREAM_GUARD();                 /* the copy came out of this interval */
    }

    if (stopPending) {
        STREAM_ENABLE(0);
        streamOn = 0;
        stopPending = 0;
        streamParked = 1;
        PPS_RUN(0);                     /* no packets, no PPS run */
        STREAM_GUARD();
    }

    if (epHalted) {
        streamParked = 1;
        STREAM_GUARD();
        return;
    }

    USB_INDEX  = 1;
    USB_TXCSRL = TXCSRL_TXPKTRDY;
    if (streamOn) STREAM_STROBE();
    USB_INDEX  = 0;
}
