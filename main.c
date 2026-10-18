/*
 * main.c: MSi2500 firmware with some USB compliance fixes, a PPS
 * timestamper and two bit banged peripherals.
 *
 *   core.c: waits, reboot, register writes and their shadows
 *   desc.c: the USB descriptors and the firmware block at 0x0040
 *   ep0.c: endpoint 0 and the request dispatch
 *   usb.c: the USB interrupt and bus reset
 *   stream.c: the streaming endpoint and its interrupt
 *   pps.c: the PPS timestamper
 *   list.c: register lists
 *   i2c.c, uart.c, bitbang.c: the bit banged serial
 *   mem.c: memory access and the call gate for the host
 *
 * requests.h has the requests a host sends.
 */
#include "msi2500.h"
#include "core.h"
#include "list.h"
#include "pps.h"
#include "stream.h"     /* the two interrupt prototypes: sdcc puts the */
#include "usb.h"        /* vectors in the file with main() */

unsigned char _sdcc_external_startup(void)
{
    return 0;
}

void main(void)
{
    USB_POWER = PWR_DISCONNECT;
    usbSettle();

    MEM_BUF_MAP(0);
    streamEnable(0);

    gpioApply();

    USB_INTRUSBE = INTRUSBE_BASE;

    TCON = 0x05;
    IP   = 0x01;
    IE   = 0x85;

    USB_POWER = PWR_RUN;

    for (;;) {
        if (listDue)
            listStep();

        if (ppsPaused && !listRun) {
            EA = 0;
            ppsPaused = 0;
            tickL = 0; tickH = 0; ppsDiv = 1;
            PPS_RUN(1);
            STREAM_GUARD();             /* the first interval is partial */
            EA = 1;
        }

        if (ppsRun) {
            ppsPoll();
        } else {
            EA = 0;
            if (!listDue) {
                EA = 1;                 /* the instruction after this runs before any */
                PCON = 1;               /* interrupt, so a step raised now still wakes us */
            }
            EA = 1;
        }
    }
}
