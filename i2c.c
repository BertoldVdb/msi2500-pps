/*
 * i2c.c: the bit banged I2C master on GPIO_1 (SDA) and GPIO_2 (SCL)
 */
#include "msi2500.h"
#include "bitbang.h"
#include "core.h"
#include "ep0.h"
#include "i2c.h"
#include "pps.h"
#include "requests.h"

static __xdata uint8_t i2cStatus;       /* 0 = ACKed and the clock ran free */

static void i2cIdle(void)
{
    reg8Shadow &= ~(GPIO_DIR(PIN_SDA | PIN_SCL) | GPIO_VAL(PIN_SDA | PIN_SCL));
    gpioApply();
}

static void sclRelease(void)
{
    uint8_t i;
    reg8Shadow &= ~GPIO_DIR(PIN_SCL);
    gpioApply();
    bitDelay();                         /* the high period, and the rise time */
    for (i = 0; i < 64 && !(MMIO_GPIO_IN & PIN_SCL); i++)
        bitDelay();
}
static void sclLow(void)     { reg8Shadow |=  GPIO_DIR(PIN_SCL); gpioApply(); }
static void sdaRelease(void) { reg8Shadow &= ~GPIO_DIR(PIN_SDA); gpioApply(); }
static void sdaLow(void)     { reg8Shadow |=  GPIO_DIR(PIN_SDA); gpioApply(); }

static void i2cStart(void)
{
    sdaRelease(); sclRelease();
    sdaLow();     bitDelay();
    sclLow();     bitDelay();
}

static void i2cStop(void)
{
    sdaLow();     bitDelay();
    sclRelease();
    sdaRelease(); bitDelay();
}

static uint8_t i2cBit(uint8_t v)
{
    uint8_t r;
    if (v) sdaRelease(); else sdaLow();
    bitDelay();
    sclRelease();
    r = (MMIO_GPIO_IN & PIN_SDA) ? 1 : 0;
    sclLow(); bitDelay();
    return r;
}

/* returns 0 if the slave ACKed */
static uint8_t i2cWriteByte(uint8_t v)
{
    uint8_t i;
    for (i = 0; i < 8; i++) { i2cBit((uint8_t)(v & 0x80)); v = (uint8_t)(v << 1); }
    return i2cBit(1);
}

static uint8_t i2cReadByte(uint8_t ack)
{
    uint8_t i, v = 0;
    for (i = 0; i < 8; i++) v = (uint8_t)((v << 1) | i2cBit(1));
    i2cBit(!ack);                       /* ACK is SDA held low */
    return v;
}

static void i2cRecover(void)
{
    uint8_t i;
    i2cIdle();
    bitDelay();                         /* released lines have to rise before
                                           the first test means anything */
    for (i = 0; i < 9 && !(MMIO_GPIO_IN & PIN_SDA); i++) {
        sclLow();     bitDelay();
        sclRelease();                   /* the high period is inside it now */
    }
    i2cStop();
    i2cStatus = (MMIO_GPIO_IN & PIN_SDA) ? 0 : 1;
}

void i2cWriteXfer(uint8_t n)
{
    uint8_t i;
    usbGuard = GUARD_HELD;
    if (!(setup.wValueH & I2C_REPEAT)) i2cIdle();
    i2cStart();
    i2cStatus = i2cWriteByte((uint8_t)(setup.wValueL << 1));
    for (i = 0; i < n && !i2cStatus; i++)
        i2cStatus = i2cWriteByte(xferBuf[i]);
    if (!(setup.wValueH & I2C_NO_STOP)) i2cStop();
    usbGuard = 2;                       /* done: back to the normal decay */
}

/* the bytes come in the data stage, written once it has all arrived */
void i2cWriteRequest(void)
{
    if (setup.wLength > sizeof xferBuf) { ep0Stall(); return; }
    delayL = setup.wIndexL; delayH = setup.wIndexH;
    if (setup.wLength == 0) {
        i2cWriteXfer(0);
        ep0Ack();
        return;
    }
    ep0StartRx(xferBuf, 0, ACT_I2C_WRITE);
}

void i2cReadRequest(void)
{
    uint8_t n = (uint8_t)setup.wLength, i;

    if (setup.wLength > sizeof xferBuf) { ep0Stall(); return; }
    delayL = setup.wIndexL; delayH = setup.wIndexH;
    usbGuard = GUARD_HELD;
    if (!(setup.wValueH & I2C_REPEAT)) i2cIdle();
    i2cStart();
    i2cStatus = i2cWriteByte((uint8_t)((setup.wValueL << 1) | 1));
    /* NACK the last byte, which is how a slave is told to let go of SDA */
    for (i = 0; i < n; i++)
        xferBuf[i] = i2cReadByte((uint8_t)(i + 1 < n));
    if (!(setup.wValueH & I2C_NO_STOP)) i2cStop();
    usbGuard = 2;

    USB_CSR0 = CSR0_SERV_RXPKTRDY;
    for (i = 0; i < n; i++)
        USB_FIFO0[i & 3] = xferBuf[i];
    setup.wLength = 0;
    USB_CSR0 = CSR0_TXPKTRDY | CSR0_DATAEND;
}

void i2cStatusRequest(void)
{
    /* 0 = ACKed, 1 = a NACK, or after a recovery, SDA still held down. */
    ep0SendByte(i2cStatus);
}

void i2cResetRequest(void)
{
    delayL = setup.wIndexL; delayH = setup.wIndexH;
    usbGuard = GUARD_HELD;
    i2cRecover();
    usbGuard = 2;
    ep0Ack();
}
