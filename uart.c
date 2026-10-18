/*
 * uart.c: the bit banged UART transmitter on GPIO_2
 */
#include "msi2500.h"
#include "bitbang.h"
#include "core.h"
#include "ep0.h"
#include "pps.h"
#include "uart.h"

/* It shares GPIO_2 with the I2C master's SCL. SDA is released first, and with
   SDA high no level on SCL makes a START, so a slave on the bus sees nothing
   it answers to. */
void uartSend(uint8_t n)
{
    uint8_t i, k, b;

    usbGuard = GUARD_HELD;
    reg8Shadow &= ~GPIO_DIR(PIN_SDA);   /* release SDA: no START is possible */
    reg8Shadow |=  GPIO_DIR(PIN_SCL);
    reg8Shadow |=  GPIO_VAL(PIN_SCL);   /* idle high */
    gpioApply();
    bitDelay();
    bitDelay();
    for (k = 0; k < n; k++) {
        b = xferBuf[k];
        reg8Shadow &= ~GPIO_VAL(PIN_SCL); gpioApply(); bitDelay();
        for (i = 0; i < 8; i++) {
            if (b & 1) reg8Shadow |=  GPIO_VAL(PIN_SCL);
            else       reg8Shadow &= ~GPIO_VAL(PIN_SCL);
            gpioApply(); bitDelay();
            b = (uint8_t)(b >> 1);
        }
        reg8Shadow |=  GPIO_VAL(PIN_SCL); gpioApply(); bitDelay();
    }
    usbGuard = 2;                       /* done: back to the normal decay */
}

/* the bytes come in the data stage, sent once it has all arrived */
void uartRequest(void)
{
    if (setup.wLength > sizeof xferBuf) { ep0Stall(); return; }
    delayL = setup.wValueL; delayH = setup.wValueH;
    if (setup.wLength == 0) {
        ep0Ack();
        return;
    }
    ep0StartRx(xferBuf, 0, ACT_UART);
}
