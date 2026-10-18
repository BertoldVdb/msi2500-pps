/*
 * core.c: the 8051 core's waits and reboot, and the writes to the ADC / tuner /
 * GPIO block with the shadows of the registers this firmware reads back
 */
#include "msi2500.h"
#include "core.h"
#include "ep0.h"
#include "list.h"
#include "pps.h"

__xdata uint8_t rebootPending;

__xdata uint32_t reg3Shadow = MMIO_ANALOG_STANDBY;
__xdata uint32_t reg8Shadow = MMIO_GPIO_IDLE;

/* n * 256 turns of a DJNZ, about n * 17 us, and 0 is 256. The one short wait
   here, and what the USB settle is built from. */
static void delay(uint8_t n) __naked
{
    (void) n;                           /* in DPL */
    __asm
        ; ACC and B: nothing the caller keeps live across a call, unlike R6/R7,
        ; which the compiler would trust a naked function to leave alone
        mov     b,dpl
    00001$:
        clr     a
    00002$:
        djnz    acc,00002$
        djnz    b,00001$
        ret
    __endasm;
}

/* long enough for a host to see a disconnect: 60 x 256 x 256 turns, ~260 ms */
void usbSettle(void)
{
    uint8_t i;

    for (i = 0; i < 60; i++)
        delay(0);
}

void reboot(uint8_t fromRam)
{
    IE = 0;
    USB_INTRUSBE  = 0;
    USB_INTRTXE_L = 0;
    USB_INTRRXE_L = 0;
    (void) USB_INTRUSB;
    (void) USB_INTRTX;
    USB_POWER = PWR_DISCONNECT;
    usbSettle();
    BOOTSRC = fromRam;
    CORERESET = 1;
}


void mmioWriteNoShadow(uint8_t reg, uint32_t val)
{
    uint8_t irq = EA;

    EA = 0;
    MMIO_WR_REG  = reg;
    MMIO_WR_LOW  = (uint8_t)val;
    MMIO_WR_MID  = (uint8_t)(val >> 8);
    MMIO_WR_HIGH = (uint8_t)(val >> 16);
    EA = irq;
}

/* A direct mirror of libmirisdr's mirisdr_write_reg(): the same register
   number and the same 24 bit value, numbered the same way */
void mmioWrite(uint8_t reg, uint32_t val)
{
    mmioWriteNoShadow(reg, val);

    switch (reg & 0x1F) {
    case MMIO_REG_ANALOG:
        reg3Shadow = val;
        break;
    case MMIO_REG_GPIO:
        reg8Shadow = val;
        break;
    }
}

/* the GPIO shadow out to the pins */
void gpioApply(void)
{
    mmioWrite(MMIO_REG_GPIO, reg8Shadow);
}

void bootRequest(void)
{
    ep0Ack();
    rebootPending = (setup.wValueL & 1) + 1;
    PPS_RUN(0);                         /* main takes the reboot if no interrupt does */
}

void regWriteRequest(void)
{
    /* a running list owns the registers: a write could land between its words, or
       reach the tuner port or the SPI master while the other is on the shared lines */
    if (listRun) { ep0Stall(); return; }
    mmioWrite(setup.wValueL,
              (uint32_t)setup.wValueH
              | ((uint32_t)setup.wIndexL << 8)
              | ((uint32_t)setup.wIndexH << 16));
    if (setup.wValueL == MMIO_REG_TUNER) portGap();    /* a list may start next */
    ep0Ack();
}
