/*
 * usb.c: the USB device controller's interrupt, its bus reset and the
 * endpoint index.
 *
 * The interrupt also latches the PPS edges taken from SOF. That part is
 * inline rather than in pps.c, as the time it takes is in what it measures.
 */
#include "msi2500.h"
#include "core.h"
#include "ep0.h"
#include "pps.h"
#include "stream.h"
#include "usb.h"

/* Endpoint 1's registers selected, the streaming interrupt, which selects 1 and
   back to 0 itself, is held off */
void usbIndex(uint8_t ep)
{
    if (ep) {
        EX0 = 0;
        USB_INDEX = ep;
    } else {
        USB_INDEX = 0;
        EX0 = 1;
    }
}

static void usbReset(void)
{
    streamReset();
    PPS_RUN(0);
    ppsSrcSof = 0;
    USB_INTRUSBE = INTRUSBE_BASE;

    tickL = 1;
    ep0Reset();

    USB_INTRRXE_H = 0;
    USB_INTRRXE_L = 0;
    USB_INTRTXE_H = 0;
    USB_INTRTXE_L = INTRTX_EP0;
}

void usbIsr(void) __interrupt (2) __using (2)
{
    uint8_t intrUsb, intrTx, csr0, ep0Work;

    /* Loop until no more IRQs to handle */
    for (;;) {
        inSofLatch = 1;
        intrUsb = USB_INTRUSB;

        if (ppsSrcSof && (intrUsb & INTRUSB_SOF)) {
            uint8_t f = USB_FRAME_L;
            uint8_t idx;
            uint8_t capture = 0;

            if (f != sofLastFrame) {
                sofLastFrame = f;
                if ((f & 1) && --sofDiv == 0) {
                    sofDiv = ppsDivReload;
                    capture = 1;
                }
            }

            EA = 0;
            idx = sofInInterval;
            if (!idx) {
                firstSofL = tickL;
                firstSofH = (uint8_t)(0 - tickH);
                firstSofCarry = (firstSofL ? 0 : 2)
                              | ((usbGuard == 2 || usbGuard == GUARD_HELD) ? 4 : 0);
            }
            sofInInterval++;
            if (capture) {
                edgeTickL = tickL;
                edgeTickH = (uint8_t)(0 - tickH);
                edgeIrq = irqCount;
                edgeSofCount = idx;
                edgeFirstL = firstSofL;
                edgeFirstH = firstSofH;

                edgeFirstCarry = firstSofCarry | (tickL ? 0 : 1);
                edgeUsbGuard = sofStraddle ? (uint8_t) GUARD_STRADDLE
                             : tickCarried ? (uint8_t) GUARD_BASE : 0;
            }
            EA = 1;

            if (capture) {
                edgeFrameL = f;
                edgeFrameH = USB_FRAME_H;
                edgeCount++;
            }
            intrUsb &= ~INTRUSB_SOF;
        }

        intrTx = USB_INTRTX;

        csr0 = USB_CSR0;
        ep0Work = EP0_PENDING(csr0);
        if (!(intrUsb & INTRUSBE_BASE) && !(intrTx & INTRTX_EP0) && !ep0Work) {
            inSofLatch = 0;
            return;
        }

        if (intrUsb & INTRUSB_SUSPEND) {
            mmioWriteNoShadow(MMIO_REG_ANALOG, MMIO_ANALOG_STANDBY);
            USB_POWER = PWR_SUSPEND;
        }

        if (intrUsb & INTRUSB_RESUME) {
            mmioWriteNoShadow(MMIO_REG_ANALOG, reg3Shadow);
            USB_POWER = PWR_RUN;
        }

        if (intrUsb & INTRUSB_RESET) {
            usbReset();
        }

        if ((intrTx & INTRTX_EP0) || ep0Work) {
            ep0Service();
        }

        usbGuard = 2;
    }
}
