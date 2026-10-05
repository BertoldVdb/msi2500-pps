/*
 * main.c - MSi2500 firmware with some USB compliance fixes, a PPS
 * timestamper and two bit banged peripherals.
 */
#include "msi2500.h"

/* ------------------------------------------------------------------ */
/* USB protocol constants                                              */
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

/* Vendor requests, see msi2500/cmd.txt */
#define REQ_BOOT                0x40    /* wValue bit0: 0 = ROM, 1 = RAM  */
#define REQ_WRITE_REG           0x41    /* wValue/wIndex = MMIO reg+data  */
#define REQ_READ_MEM            0x42    /* wIndex + 0xC000, returns 4 B   */
#define REQ_START_STREAM        0x43
#define REQ_WRITE_MEM           0x44    /* wValue = address, data stage   */
#define REQ_STOP_STREAM         0x45

/* libmirisdr documents other requests, but my device doesn't respond to them.
 * We keep this region free and put our own requests starting from 0x51 */

#define REQ_PPS_TIME            0x51    /* read the PPS timestamp counters  */
#define REQ_PPS_ENABLE          0x52    /* bit 0 run, bit 1 take an anchor */
#define REQ_PPS_ANCHOR          0x53    /* read the captured sample counters  */
#define REQ_UART_TX             0x54    /* wValue = bit delay, data = bytes   */
#define REQ_I2C_WRITE           0x55    /* wValue = addr|flags, wIndex = delay */
#define REQ_I2C_READ            0x56    /* same, data stage comes back        */
#define REQ_I2C_STATUS          0x57    /* one byte, 0 = the last op was ACKed */
#define REQ_I2C_RESET           0x58    /* wIndex = delay: recover a stuck bus */
#define REQ_CALL                0x59    /* wValue = address, entry state in callCtx */

/* wValueH flag shared by the two I2C transfer requests */
#define I2C_NO_STOP             0x01    /* leave the bus for a repeated start */
#define I2C_REPEAT              0x02    /* this transfer IS the repeated start */

/* What to do when an OUT data stage finishes */
#define ACT_NONE                0
#define ACT_UART                1
#define ACT_I2C_WRITE           2

#define FEATURE_ENDPOINT_HALT   0

#define DESC_DEVICE             1
#define DESC_CONFIGURATION      2
#define DESC_STRING             3
#define DESC_DEVICE_QUALIFIER   6
#define DESC_OTHER_SPEED_CONFIG 7

#define EP0_MAXPACKET           64

#define INTRUSBE_BASE   (INTRUSB_SUSPEND | INTRUSB_RESUME | INTRUSB_RESET)

/* Endpoint 0 driver state */
#define EP0_IDLE                0
#define EP0_TX                  1       /* sending a long IN transfer   */
#define EP0_RX                  2       /* collecting an OUT data stage */

/* ------------------------------------------------------------------ */
/* Descriptors                                                        */
/* ------------------------------------------------------------------ */

/* A host loading this image patches the two ids below into it first, so the
   device keeps the identity it had. */
static __code uint8_t deviceDesc[18] = {
    18, DESC_DEVICE,
    0x00, 0x02,             /* bcdUSB 2.00                              */
    0xFF, 0xFF, 0xFF,       /* vendor specific class/subclass/protocol  */
    EP0_MAXPACKET,
    0xD0, 0x16,             /* idVendor  0x16D0                         */
    0x8C, 0x15,             /* idProduct 0x158C                         */
    0x00, 0x01,             /* bcdDevice                                */
    1, 2, 0,                /* iManufacturer, iProduct, no serial       */
    1                       /* bNumConfigurations                       */
};

static __code uint8_t stringLang[4] = {
    4, DESC_STRING, 0x09, 0x04      /* LANGID 0x0409, US English */
};

static __code uint8_t stringVendor[22] = {
    22, DESC_STRING,
    'B', 0, 'e', 0, 'r', 0, 't', 0, 'o', 0, 'l', 0, 'd', 0,
    'V', 0, 'd', 0, 'b', 0
};

static __code uint8_t stringProduct[24] = {
    24, DESC_STRING,
    'M', 0, 'S', 0, 'I', 0, '2', 0, '5', 0, '0', 0, '0', 0,
    '-', 0, 'P', 0, 'P', 0, 'S', 0
};

static __code uint8_t stringSerial[26] = { 0, DESC_STRING };

/* This block is read by the driver to identify the firmware */
struct fwInfo {
    uint8_t  magic[4];
    uint8_t  id[8];                     /* filled in after linking */
    __code uint8_t *deviceDesc;
    __code uint8_t *stringSerial;
    uint8_t  spare[8];
};

__code __at (0x0040) struct fwInfo fwBlock = {
    { 'B', 'V', 'D', 'B' },
    { 0, 0, 0, 0, 0, 0, 0, 0 },
    deviceDesc,
    stringSerial,
    { 0, 0, 0, 0, 0, 0, 0, 0 }
};

static __code uint8_t qualifierDesc[10] = {
    10, DESC_DEVICE_QUALIFIER,
    0x00, 0x02,
    0xFF, 0xFF, 0xFF,
    8,
    1, 0
};

/* High speed configuration: one interface with five alternate settings.
 *   alt 0 - no endpoints (idle)
 *   alt 1 - isochronous IN, 3 x 1024 bytes per microframe
 *   alt 2 - isochronous IN, 1024 bytes per microframe
 *   alt 3 - bulk IN, 512 bytes
 *   alt 4 - isochronous IN, 2 x 1024 bytes per microframe
 */
static __code uint8_t configDesc[82] = {
    9, DESC_CONFIGURATION, 82, 0, 1, 1, 0, 0x80, 250,   /* 500 mA */
    9, 4, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0,
    9, 4, 0, 1, 1, 0xFF, 0xFF, 0xFF, 0,
    7, 5, 0x81, 0x01, 0x00, 0x14, 1,
    9, 4, 0, 2, 1, 0xFF, 0xFF, 0xFF, 0,
    7, 5, 0x81, 0x01, 0x00, 0x04, 1,
    9, 4, 0, 3, 1, 0xFF, 0xFF, 0xFF, 0,
    7, 5, 0x81, 0x02, 0x00, 0x02, 0,
    9, 4, 0, 4, 1, 0xFF, 0xFF, 0xFF, 0,
    7, 5, 0x81, 0x01, 0x00, 0x0C, 1
};

/* Full speed configuration, not useful but required by USB */
static __code uint8_t otherSpeedDesc[18] = {
    9, DESC_CONFIGURATION, 18, 0, 1, 1, 0, 0x80, 250,
    9, 4, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0
};

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static __xdata uint8_t configuration = 1;
static __xdata uint8_t altSetting;
static __xdata uint8_t ep0State;

static __xdata uint8_t rebootPending;   /* 0 none, else target + 1: see
                                           reboot() and request 0x40     */
/* PPS related state */
static __data uint8_t  tickL, tickH; /* both count down: DJNZ is the cheapest count, and
                                        tickH reaching zero again is 65536 turns with no
                                        packet, the poll loop's own way out */
static __data uint8_t  lastTickL, lastTickH;
static __data uint32_t irqCount;     /* packet interrupts since stream start */
static volatile __bit stopPending;   /* stop at the next interrupt, not here  */
static volatile __bit streamParked;  /* one buffer handed over but not acked  */
static __data uint8_t  edgeTickL, edgeTickH;
static __data uint32_t edgeIrq;
static __bit ppsRun;                 /* poll loop runs only while this is set */
static __bit ppsSrcSof;              /* edges from the SOF interrupt, not GPIO_0 */
static __data uint8_t sofDiv, sofLastFrame;
/* SOF interrupts since the last streaming interrupt, and the tick the first of
   them fell on.  A host places the edge from that first SOF plus a whole number
   of microframes, so what the handlers in between cost never enters the answer. */
static __data uint8_t sofInInterval, lastSofCount, edgeSofCount;
static __data uint8_t firstSofL, firstSofH, firstSofCarry;
static __data uint8_t edgeFirstL, edgeFirstH, edgeFirstCarry;
static volatile __bit inSofLatch;    /* the USB handler is running: a packet interrupt
                                        taken late in here marks the interval */
static volatile __bit sofStraddle;   /* ...and a streaming interrupt landed there */
static volatile __bit tickCarried;   /* the interval opened on the loop's carry */
static __data uint8_t edgeCount;
static __data uint8_t edgeFrameL, edgeFrameH;   /* USB frame the edge fell in */
static __data uint8_t ppsDiv, ppsDivReload;     /* latch every Nth edge        */

/* One-shot anchor.  The header's sample counter lives in the capture buffers at
   0xE000, readable only while they are mapped, and reading it mid stream
   corrupts a few samples - which the next blocks heal. */
static __xdata uint8_t anchor[16];
static volatile __xdata uint8_t anchorPending;   /* the ISR clears it while a handler waits */
static __xdata uint8_t anchorValid;

/* A USB interrupt freezes the poll loop ~19 us against the streaming one's
   ~2.3 us and leaves `ticks` looking normal, so captures near one are thrown
   away. */
static __data uint8_t usbGuard;
static __data uint8_t edgeUsbGuard;

/* Guard values that are markers rather than a count.  The ordinary ones are a
   countdown of streaming interrupts, so they stay plain numbers. */
#define GUARD_HELD      0xFF    /* a bit banged transfer is running           */
#define GUARD_CARRY     0xFE    /* the loop was between tickL and its carry   */
#define GUARD_STRADDLE  0xFD    /* the interval opened inside a SOF latch     */
#define GUARD_BASE      0xFB    /* ...or on the poll loop's carry             */

/* The guard says a capture is not to be trusted, and a USB interrupt is not the
   only thing that earns that: the streaming interrupt takes far longer than
   usual on the paths below, and those turns come out of the interval a capture
   is placed against.  0xFF is a bit banged transfer holding it, so leave it. */
#define STREAM_GUARD()  do { if (usbGuard != GUARD_HELD) usbGuard = 2; } while (0)

static __xdata uint8_t epHalted = 0;   /* endpoint 0x81 halted by the host */

/* Shadows of host written values to reg 3 and 8 */
static __xdata uint32_t reg3Shadow = MMIO_ANALOG_STANDBY;
static __xdata uint32_t reg8Shadow = MMIO_GPIO_IDLE;

/* The active setup packet. */
/* EP0 bookkeeping: cold next to the capture path, and the 128 bytes sdcc can
   address directly are the scarce ones, so these live in xdata. */
static __xdata struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint8_t  wValueL;
    uint8_t  wValueH;
    uint8_t  wIndexL;
    uint8_t  wIndexH;
    uint16_t wLength;
} setup;

static __xdata uint8_t *__data ep0Ptr;
static __xdata uint8_t ep0Remap;   /* 0x44: map the capture memory in */

static __code  uint8_t *__data ep0CodePtr;
static __xdata uint16_t ep0Sent;
static __xdata uint8_t ep0TypeByte;

/* ------------------------------------------------------------------ */
/* MMIO helpers                                                        */
/* ------------------------------------------------------------------ */
/* The call gate runs once, when a host asks for it, so it pays the movx to keep
   the directly addressed bytes for the capture path. */
static __xdata uint16_t callAddr;
static __xdata uint8_t  callR0, callR1, callA, callB, callDpl, callDph;
static __xdata uint8_t  callInA, callInB, callInDpl, callInDph;

/* The entry state for REQ_CALL, which the host writes with request 0x44:
   A, B, DPL, DPH, R0, R1, then two spare.  The Makefile keeps --xram-size
   below it so the allocator cannot grow into these eight bytes. */
__xdata __at (0x1FF8) uint8_t callCtx[8];

static void doCall(void)
{
    __asm
        mov     dptr,#00099$
        push    dpl
        push    dph
        /* xdata, so every one of these needs dptr and acc. The callee's own
           dptr and acc are therefore set last, out of registers. */
        mov     dptr,#_callAddr
        movx    a,@dptr
        push    acc
        inc     dptr
        movx    a,@dptr
        push    acc
        mov     dptr,#_callR0
        movx    a,@dptr
        mov     r0,a
        mov     dptr,#_callR1
        movx    a,@dptr
        mov     r1,a
        mov     dptr,#_callInB
        movx    a,@dptr
        mov     b,a
        mov     dptr,#_callInDpl
        movx    a,@dptr
        mov     r2,a
        mov     dptr,#_callInDph
        movx    a,@dptr
        mov     r3,a
        mov     dptr,#_callInA
        movx    a,@dptr
        mov     r4,a
        mov     dpl,r2
        mov     dph,r3
        mov     a,r4
        ret
    00099$:
        /* the callee's results are in a, b, dptr, r0 and r1 -> stash the ones
           that writing xdata would overwrite before touching dptr */
        mov     r2,a
        mov     r3,b
        mov     r4,dpl
        mov     r5,dph
        mov     dptr,#_callA
        mov     a,r2
        movx    @dptr,a
        mov     dptr,#_callB
        mov     a,r3
        movx    @dptr,a
        mov     dptr,#_callDpl
        mov     a,r4
        movx    @dptr,a
        mov     dptr,#_callDph
        mov     a,r5
        movx    @dptr,a
        mov     dptr,#_callR0
        mov     a,r0
        movx    @dptr,a
        mov     dptr,#_callR1
        mov     a,r1
        movx    @dptr,a
    __endasm;
}

/* n * 256 turns of a DJNZ, about n * 17 us; 0 is 256. The one short wait
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
static void usbSettle(void)
{
    uint8_t i;

    for (i = 0; i < 60; i++)
        delay(0);
}

static void reboot(uint8_t fromRam)
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


static void mmioWriteNoShadow(uint8_t reg, uint32_t val)
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
static void mmioWrite(uint8_t reg, uint32_t val)
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


/* ------------------------------------------------------------------- */
/* Bit banged serial: a UART transmitter and an I2C master             */
/*                                                                     */
/* GPIO_1 = SDA, GPIO_2 = SCL, and the UART transmits on GPIO_2 too.   */
/* They coexist on the one wire: see the note above uartSend for why   */
/* neither disturbs the other.                                         */
/* ------------------------------------------------------------------- */

#define PIN_SDA     0x02                /* GPIO_1 */
#define PIN_SCL     0x04                /* GPIO_2 */

static __data uint8_t delayL, delayH;   /* bit period, from the host   */
static __data uint8_t dcL, dcH;

#define GPIO_DIR(pin)   ((uint32_t)(pin) << 12)     /* val[15:12] */
#define GPIO_VAL(pin)   ((uint32_t)(pin) << 8)      /* val[11:8]  */
static __xdata uint8_t i2cStatus;       /* 0 = ACKed and the clock ran free */
static __xdata uint8_t ep0Action;
static __xdata uint8_t xferBuf[64];

static void bitDelay(void) __naked
{
    __asm
        mov     _dcL,_delayL
        mov     _dcH,_delayH
        inc     _dcH
    00001$:
        djnz    _dcL,00001$             ; 2 cycles
        djnz    _dcH,00001$
        ret
    __endasm;
}

static void gpioApply(void)
{
    mmioWrite(MMIO_REG_GPIO, reg8Shadow);
}

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

static void i2cWriteXfer(uint8_t n)
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

static void uartSend(uint8_t n)
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

/* ------------------------------------------------------------------ */
/* Endpoint 0 data stages                                              */
/* ------------------------------------------------------------------ */

/* Push up to one full packet from ep0Ptr into the endpoint 0 FIFO. */
static void ep0SendChunk(void)
{
    uint16_t i, n = setup.wLength;

    if (n > EP0_MAXPACKET) n = EP0_MAXPACKET;

    for (i = 0; i < n; i++) {
        uint8_t v = *ep0CodePtr++;
        if (ep0Sent++ == 1) v = ep0TypeByte;
        USB_FIFO0_BYTE(i) = v;
    }

    /* what is left decides: nothing more to send ends the transfer */
    setup.wLength -= n;
    USB_CSR0 = setup.wLength ? CSR0_TXPKTRDY : (CSR0_TXPKTRDY | CSR0_DATAEND);
    ep0State = setup.wLength ? EP0_TX : EP0_IDLE;
}

static void ep0StartTx(uint16_t length)
{
    ep0Sent = 0;
    if (length < setup.wLength)
        setup.wLength = length;

    USB_CSR0 = CSR0_SERV_RXPKTRDY;
    ep0SendChunk();
}

static void ep0Stall(void)
{
    USB_CSR0 = CSR0_SERV_RXPKTRDY | CSR0_SENDSTALL;
}

static void ep0Ack(void)
{
    USB_CSR0 = CSR0_SERV_RXPKTRDY | CSR0_DATAEND;
}

/* Return a single byte in the data stage (GET_CONFIGURATION etc). */
static void ep0SendByte(uint8_t value)
{
    USB_CSR0 = CSR0_SERV_RXPKTRDY;
    USB_FIFO0[0] = value;
    USB_CSR0 = CSR0_TXPKTRDY | CSR0_DATAEND;
}

/* ------------------------------------------------------------------ */
/* Standard requests                                                   */
/* ------------------------------------------------------------------ */

static void handleGetDescriptor(void)
{
    uint16_t length;

    ep0TypeByte = setup.wValueH;

    switch (setup.wValueH) {
    case DESC_DEVICE:
        /* Reboot if we connected at full-speed to have another try */
        if (!(USB_POWER & PWR_HSMODE))
            reboot(0);

        ep0CodePtr = deviceDesc;
        length = deviceDesc[0];
        break;

    case DESC_CONFIGURATION:
        ep0CodePtr = configDesc;
        length = configDesc[2] | ((uint16_t)configDesc[3] << 8);
        break;

    case DESC_STRING:
        if (setup.wValueL == 0) {
            ep0CodePtr = stringLang;
            length = stringLang[0];
        } else if (setup.wValueL == 1) {
            ep0CodePtr = stringVendor;
            length = stringVendor[0];
        } else if (setup.wValueL == 2) {
            ep0CodePtr = stringProduct;
            length = stringProduct[0];
        } else if (setup.wValueL == 3 && stringSerial[0]) {
            ep0CodePtr = stringSerial;
            length = stringSerial[0];
        } else {
            ep0Stall();                 /* no such string */
            return;
        }
        break;

    case DESC_DEVICE_QUALIFIER:
        ep0CodePtr = qualifierDesc;
        length = qualifierDesc[0];
        break;

    case DESC_OTHER_SPEED_CONFIG:
        ep0CodePtr = otherSpeedDesc;
        length = otherSpeedDesc[2] | ((uint16_t)otherSpeedDesc[3] << 8);
        break;

    default:
        ep0Stall();
        return;
    }

    ep0StartTx(length);
}

static __bit streamOn = 0;

static void streamEnable(uint8_t on)
{
    streamOn = (on != 0);
    STREAM_ENABLE(on);
}

static void usbIndex(uint8_t ep)
{
    if (ep) {
        EX0 = 0;
        USB_INDEX = ep;
    } else {
        USB_INDEX = 0;
        EX0 = 1;
    }
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

        b[12] = i;                  /* which of the four buffers this is   */
        b += 0x400;                 /* 13-15 left spare, still stale RAM   */
    }

    MEM_BUF_MAP(0);
}

static void streamStop(void)
{
    streamEnable(0);

    usbIndex(1);
    /* Two flushes for double buffered endpoint */
    USB_TXCSRL = TXCSRL_FLUSHFIFO;
    USB_TXCSRL = TXCSRL_FLUSHFIFO;
    usbIndex(0);
}

static void handleEndpointHalt(uint8_t set)
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

static void handleSetInterface(void)
{
    uint8_t alt;

    if (setup.wIndexH != 0 || setup.wIndexL > 1) {
        ep0Stall();
        return;
    }

    alt = setup.wValueL;
    altSetting = alt;

    if (alt > 4) {
        ep0Stall();
        return;
    }
    usbIndex(1);
    switch (alt) {
    case 0:                     /* endpoint off                     */
        USB_TXCSRH   = 0x00;
        USB_TXMAXP_H = 0x00;
        break;
    case 1:                     /* isochronous, 3 x 1024 per uframe */
        USB_TXCSRH   = TXCSRH_ISO | TXCSRH_FRCDATATOG;
        USB_TXMAXP_H = 0x14;
        break;
    case 2:                     /* isochronous, 1024 per uframe     */
        USB_TXCSRH   = TXCSRH_ISO | TXCSRH_FRCDATATOG;
        USB_TXMAXP_H = 0x04;
        break;
    case 3:                     /* bulk, 512                        */
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

/* ------------------------------------------------------------------ */
/* Vendor requests                                                     */
/* ------------------------------------------------------------------ */

static void handleReadMemory(void)
{
    __xdata uint8_t *p;
    uint8_t a, b, c, d;

    USB_CSR0 = CSR0_SERV_RXPKTRDY;

    p = (__xdata uint8_t *)(uint16_t)
        (0xC000 + (setup.wIndexL | ((uint16_t)setup.wIndexH << 8)));

    if (setup.wValueL & 1) {
        /* This unmaps the USB controller FIFO memory so can only be done while the USB is idle */
        EA = 0;
        MEM_BUF_MAP(1);
        a = p[0]; b = p[1]; c = p[2]; d = p[3];
        MEM_BUF_MAP(0);
        EA = 1;
    } else {
        a = p[0]; b = p[1]; c = p[2]; d = p[3];
    }

    USB_FIFO0[0] = a;
    USB_FIFO0[1] = b;
    USB_FIFO0[2] = c;
    USB_FIFO0[3] = d;

    USB_CSR0 = CSR0_TXPKTRDY | CSR0_DATAEND;
}

/* ------------------------------------------------------------------ */
/* Setup packet dispatch                                               */
/* ------------------------------------------------------------------ */

static void handleSetup(void)
{
    uint8_t i;
    __xdata uint8_t *raw = (__xdata uint8_t *)&setup;

    for (i = 0; i < 8; i++)
        raw[i] = USB_FIFO0_BYTE(i);
   
    switch (setup.bRequest) {
    case REQ_GET_STATUS: {
        uint8_t status = 0;

        if ((setup.bmRequestType & 0x1F) == 2 && setup.wIndexL == 0x81) {
            usbIndex(1);
            if (USB_TXCSRL & TXCSRL_SENDSTALL)
                status = 1;
            usbIndex(0);
        }

        USB_CSR0 = CSR0_SERV_RXPKTRDY;
        USB_FIFO0[0] = status;
        USB_FIFO0[1] = 0;
        setup.wLength = 0;
        USB_CSR0 = CSR0_TXPKTRDY | CSR0_DATAEND;
        break;
    }

    case REQ_CLEAR_FEATURE:
        handleEndpointHalt(0);
        break;

    case REQ_SET_FEATURE:
        handleEndpointHalt(1);
        break;

    case REQ_SET_ADDRESS:
        USB_FADDR = setup.wValueL & 0x7F;
        ep0Ack();
        break;

    case REQ_GET_DESCRIPTOR:
        handleGetDescriptor();
        break;

    case REQ_SET_DESCRIPTOR:
        ep0Stall();
        break;

    case REQ_GET_CONFIGURATION:
        ep0SendByte(configuration);
        break;

    case REQ_SET_CONFIGURATION:
        if (setup.wValueH != 0 || setup.wValueL > 1) {
            ep0Stall();
        } else {
            configuration = setup.wValueL;
            ep0Ack();
        }
        break;

    case REQ_GET_INTERFACE:
        if (setup.wIndexH != 0 || setup.wIndexL > 1)
            ep0Stall();
        else
            ep0SendByte(altSetting);
        break;

    case REQ_SET_INTERFACE:
        handleSetInterface();
        break;

    case REQ_SYNCH_FRAME:
        ep0Stall();
        break;

    case REQ_BOOT:
        ep0Ack();
        rebootPending = (setup.wValueL & 1) + 1;
        ppsRun = 0;                     /* main takes the reboot if no interrupt does */
        break;

    case REQ_WRITE_REG:
        mmioWrite(setup.wValueL,
                  (uint32_t)setup.wValueH
                  | ((uint32_t)setup.wIndexL << 8)
                  | ((uint32_t)setup.wIndexH << 16));
        ep0Ack();
        break;

    case REQ_READ_MEM:
        handleReadMemory();
        break;

    case REQ_START_STREAM:
        irqCount = 0;
        tickL = 0;
        tickH = 0;

        /* Parked, the engine is holding one finished buffer */
        if (!streamParked) streamStop();

	/* Mark the buffers, it is redone on each start as configuring very
	 * high sample rates can overwriten them */
        EA = 0;
        stampBuffers();
        EA = 1;
        streamParked = 0;

        streamEnable(1);
        STREAM_STROBE();
        ep0Ack();
        break;

    case REQ_WRITE_MEM:
        ep0Ptr = (__xdata uint8_t *)(uint16_t)
                 (setup.wValueL | ((uint16_t)setup.wValueH << 8));
        ep0Remap = setup.wIndexL & 1;
        ep0Action = ACT_NONE;
        USB_CSR0 = CSR0_SERV_RXPKTRDY;
        ep0State = EP0_RX;
        break;

    case REQ_STOP_STREAM:
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
        break;


    case REQ_PPS_TIME: {
        /* Sample everything with IRQ disabled */
        uint8_t  sLastL, sLastH, sGuard, sCount, sFrameL, sFrameH;
        uint8_t  sEdgeL, sEdgeH, sLastSof, sEdgeSof, sFirstL, sFirstH, sFirstC;
        uint32_t sIrq, sEdgeIrq;

        EA = 0;
        sLastL   = lastTickL;   sLastH   = lastTickH;
        sIrq     = irqCount;
        sEdgeL   = edgeTickL;   sEdgeH   = edgeTickH;
        sEdgeIrq = edgeIrq;
        sGuard   = edgeUsbGuard;
        sCount   = edgeCount;
        sFrameL  = edgeFrameL;  sFrameH  = edgeFrameH;
        sLastSof = lastSofCount; sEdgeSof = edgeSofCount;
        sFirstL  = edgeFirstL;   sFirstH  = edgeFirstH;
        sFirstC  = edgeFirstCarry;
        EA = 1;

        USB_CSR0 = CSR0_SERV_RXPKTRDY;
        USB_FIFO0[0] = sLastL;
        USB_FIFO0[1] = sLastH;
        USB_FIFO0[2] = (uint8_t)sIrq;
        USB_FIFO0[3] = (uint8_t)(sIrq >> 8);
        USB_FIFO0[0] = (uint8_t)(sIrq >> 16);
        USB_FIFO0[1] = (uint8_t)(sIrq >> 24);
        USB_FIFO0[2] = sEdgeL;
        USB_FIFO0[3] = sEdgeH;
        USB_FIFO0[0] = (uint8_t)sEdgeIrq;
        USB_FIFO0[1] = (uint8_t)(sEdgeIrq >> 8);
        USB_FIFO0[2] = (uint8_t)(sEdgeIrq >> 16);
        USB_FIFO0[3] = (uint8_t)(sEdgeIrq >> 24);
        USB_FIFO0[0] = ppsRun;
        USB_FIFO0[1] = sGuard;
        USB_FIFO0[2] = sCount;            
        USB_FIFO0[3] = sFirstC;
        USB_FIFO0[0] = sFrameL;
        USB_FIFO0[1] = sFrameH;
        USB_FIFO0[2] = ppsSrcSof;
        USB_FIFO0[3] = ppsDivReload;
        USB_FIFO0[0] = sLastSof;
        USB_FIFO0[1] = sEdgeSof;
        USB_FIFO0[2] = sFirstL;
        USB_FIFO0[3] = sFirstH;
        setup.wLength = 0;
        USB_CSR0 = CSR0_TXPKTRDY | CSR0_DATAEND;
        break;
    }

    case REQ_PPS_ENABLE:
        ppsSrcSof = (setup.wValueL & 8) ? 1 : 0;
        ppsDivReload = setup.wValueH ? setup.wValueH : 1;
        sofDiv = 1;

        if (setup.wValueL & 1) {
            if (!ppsRun) { tickL = 0; tickH = 0; ppsDiv = 1; }
            ppsRun = 1;
        } else {
            ppsRun = 0;
            tickL = 1; /* force the poll loop out on its next DJNZ */
        }

	/* Enable SOF interrupt when needed */
        USB_INTRUSBE = INTRUSBE_BASE | (ppsSrcSof ? INTRUSB_SOF : 0);

        /* bit 1 asks for a fresh anchor, the next streaming interrupt takes it.
         * Taking the anchor creates a short sample glitch, so don't do it when not needed */
        if (setup.wValueL & 2) {
            anchorValid = 0;
            if (streamOn) {
                anchorPending = 1;
                for (volatile uint16_t w = 65535; anchorPending && w; w--) ;
                anchorPending = 0;
            }
        }
        ep0Ack();
        break;

    case REQ_PPS_ANCHOR: {
        uint8_t i;
        USB_CSR0 = CSR0_SERV_RXPKTRDY;
        for (i = 0; i < 16; i++)
            USB_FIFO0[i & 3] = anchor[i];
        USB_FIFO0[0] = anchorValid;
        USB_FIFO0[1] = 0;
        setup.wLength = 0;
        USB_CSR0 = CSR0_TXPKTRDY | CSR0_DATAEND;
        break;
    }

    case REQ_UART_TX:
    case REQ_I2C_WRITE:
        if (setup.wLength > sizeof xferBuf) { ep0Stall(); break; }
        if (setup.bRequest == REQ_UART_TX) {
            delayL = setup.wValueL; delayH = setup.wValueH;
            ep0Action = ACT_UART;
        } else {
            delayL = setup.wIndexL; delayH = setup.wIndexH;
            ep0Action = ACT_I2C_WRITE;
        }
        if (setup.wLength == 0) {
            ep0Action = ACT_NONE;
            if (setup.bRequest == REQ_I2C_WRITE) i2cWriteXfer(0);
            ep0Ack();
            break;
        }
        ep0Ptr = xferBuf;
        ep0Remap = 0;
        USB_CSR0 = CSR0_SERV_RXPKTRDY;
        ep0State = EP0_RX;
        break;

    case REQ_I2C_READ: {
        uint8_t n = (uint8_t)setup.wLength, i;

        if (setup.wLength > sizeof xferBuf) { ep0Stall(); break; }
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
        break;
    }

    case REQ_I2C_STATUS:
        /* 0 = ACKed, 1 = a NACK, or after a recovery, SDA still held down. */
        ep0SendByte(i2cStatus);
        break;

    case REQ_I2C_RESET:
        delayL = setup.wIndexL; delayH = setup.wIndexH;
        usbGuard = GUARD_HELD;
        i2cRecover();
        usbGuard = 2;
        ep0Ack();
        break;

    case REQ_CALL:
        USB_CSR0 = CSR0_SERV_RXPKTRDY;
        callAddr = setup.wValueL | ((uint16_t)setup.wValueH << 8);
        callInA = callCtx[0];
        callInB = callCtx[1];
        callInDpl = callCtx[2];
        callInDph = callCtx[3];
        callR0 = callCtx[4];
        callR1 = callCtx[5];
        usbGuard = GUARD_HELD;
        doCall();
        usbGuard = 2;
        USB_FIFO0[0] = callA;
        USB_FIFO0[1] = callB;
        USB_FIFO0[2] = callDpl;
        USB_FIFO0[3] = callDph;
        USB_FIFO0[0] = callR0;
        USB_FIFO0[1] = callR1;
        setup.wLength = 0;
        USB_CSR0 = CSR0_TXPKTRDY | CSR0_DATAEND;
        break;


    default:
        ep0Stall();
        break;
    }
}

/* Pull an OUT data packet into the buffer set up by request 0x44. */
static void ep0Receive(void)
{
    uint8_t count = USB_COUNT0;
    uint8_t i;

    for (i = 0; i < count; i++) {
        uint8_t v = USB_FIFO0_BYTE(i);
        if (ep0Remap) {
            EA = 0;
            MEM_BUF_MAP(1);
            *ep0Ptr++ = v;
            MEM_BUF_MAP(0);
            EA = 1;
        } else {
            *ep0Ptr++ = v;
        }
    }

    setup.wLength -= count;

    if (setup.wLength == 0) {
        USB_CSR0 = CSR0_SERV_RXPKTRDY | CSR0_DATAEND;
        ep0State = EP0_IDLE;
        if (ep0Action != ACT_NONE) {
            uint8_t act = ep0Action, n = (uint8_t)(ep0Ptr - xferBuf);
            ep0Action = ACT_NONE;
            if (act == ACT_UART) uartSend(n);
            else                 i2cWriteXfer(n);
        }
    } else {
        USB_CSR0 = CSR0_SERV_RXPKTRDY;
    }
}

/* ------------------------------------------------------------------ */
/* Interrupt handlers                                                 */
/* ------------------------------------------------------------------ */

#define EP0_PENDING(c) ((((c) & (CSR0_RXPKTRDY | CSR0_SETUPEND | CSR0_SENTSTALL)) != 0) \
                        || (ep0State == EP0_TX && !((c) & CSR0_TXPKTRDY)))
static void ep0Service(void);
static void usbReset(void);
static uint8_t rebootWait;

void streamIsr(void) __interrupt (0) __using (1)
{
    if (!streamOn) {
        return;
    }


    lastTickL = tickL;
    /* COunts down so invert it */
    lastTickH = (uint8_t)(0 - tickH);
    tickL = 0;
    tickH = 0;
    lastSofCount = sofInInterval;
    sofInInterval = 0;
    sofStraddle = inSofLatch;
    tickCarried = !lastTickL;
    if (usbGuard && usbGuard != GUARD_HELD) usbGuard--;
    irqCount++;

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
        ppsRun = 0;                     /* no packets, no PPS run */
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

static void usbReset(void)
{
    streamStop();
    stopPending = 0;
    streamParked = 0;
    ppsRun = 0;
    ppsSrcSof = 0;
    USB_INTRUSBE = INTRUSBE_BASE;

    tickL = 1;
    epHalted = 0;
    altSetting = 0;
    ep0State = EP0_IDLE;

    usbIndex(1);
    USB_TXCSRH   = 0;
    USB_TXMAXP_H = 0;
    USB_TXMAXP_L = 0;
    usbIndex(0);

    USB_INTRRXE_H = 0;
    USB_INTRRXE_L = 0;
    USB_INTRTXE_H = 0;
    USB_INTRTXE_L = INTRTX_EP0;
}

/* One service of EP0: what the handler does once it knows there is work. */
static void ep0Service(void)
{
    uint8_t csr0;

      {
        csr0 = USB_CSR0;

        if (rebootPending && (!(csr0 & CSR0_DATAEND) || ++rebootWait == 0)) {
            reboot((uint8_t)(rebootPending - 1));
        }

        if (csr0 & CSR0_SENTSTALL) {
            USB_CSR0 = 0;
            ep0State = EP0_IDLE;
        }
        
        if (csr0 & CSR0_SETUPEND) {
            USB_CSR0 = CSR0_SERV_SETUPEND;
            ep0State = EP0_IDLE;
        }

        switch (ep0State) {
        case EP0_IDLE:
            if (csr0 & CSR0_RXPKTRDY){
                handleSetup();
            }
            break;

        case EP0_TX:
            if (!(csr0 & CSR0_TXPKTRDY)) {
                ep0SendChunk();
            }
            break;

        case EP0_RX:
            if (csr0 & CSR0_RXPKTRDY) {
                ep0Receive();
            }
            break;
        }

      }
}

unsigned char _sdcc_external_startup(void)
{
    return 0;
}

static void ppsPoll(void) __naked
{
    __asm
        jnb     _ppsSrcSof,00020$

        ; Watching the USB frames, the interrupt does the latching and this is
        ; only a clock.  Leaving GPIO_0 alone means a PPS pulse on the pin
        ; cannot take the capture path and overwrite what the interrupt just
        ; latched.  Four nops for the movx and the test it replaces, so a turn
        ; is the same six cycles either way and nothing downstream changes.
    00010$:
        nop
        nop
        nop
        nop
    00012$:                             ; the carry rejoins here, four cycles
        djnz    _tickL,00010$           ; in, so the turn after it is short by
        djnz    _tickH,00013$           ; 2 - exactly what the carry cost.  And
        ret                             ;     256 carries with no packet: out,
    00013$:                             ;     so main can look at EP0
        jb      _ppsRun,00012$          ; 2 - and 2+2 is the four
        ret

    00020$:
        mov     dptr,#0xC018            ; the GPIO input register
    00001$:
        movx    a,@dptr                 ; 2 cycles
        jb      acc.0,00003$            ; 2 cycles - the watched bit is high
    00005$:                             ; the carry rejoins on the djnz, so the
        djnz    _tickL,00001$           ; 2 cycles - count and loop
    00002$:                             ; tickL hit zero
        djnz    _tickH,00006$           ; 2 - the carry, and 256 of them with
        ret                             ;     no packet is the way out
    00006$:
        jb      _ppsRun,00005$          ; 2 - four, and the turn it rejoins is two
        ret
    00003$:                             ; a high sample
        djnz    _ppsDiv,00004$          ; only every Nth: at 500 Hz the host
        mov     _ppsDiv,_ppsDivReload   ; neither needs nor can use them all
        inc     _edgeCount
        clr     ea
        mov     _edgeTickL,_tickL
        mov     a,_tickH                ; tickH counts down: the carries are -tickH
        cpl     a
        inc     a
        mov     _edgeTickH,a
        mov     _edgeIrq,_irqCount
        mov     (_edgeIrq+1),(_irqCount+1)
        mov     (_edgeIrq+2),(_irqCount+2)
        mov     (_edgeIrq+3),(_irqCount+3)
        mov     _edgeUsbGuard,_usbGuard
        setb    ea
        ; which frame the edge fell in, read outside the critical section as it
        ; only moves every millisecond.  Taken for a GPIO_0 pulse too, so that
        ; ties to a frame number as well.
        mov     dptr,#0x400C            ; USB_FRAME_L
        movx    a,@dptr
        mov     _edgeFrameL,a
        inc     dptr
        movx    a,@dptr
        mov     _edgeFrameH,a
        mov     dptr,#0xC018            ; back to the GPIO input register
    00004$:
        movx    a,@dptr                 ; 2 cycles
        jnb     acc.0,00001$            ; 2 cycles - back to counting when low
        djnz    _tickL,00004$           ; 2 cycles
        djnz    _tickH,00007$
        ret
    00007$:
        jb      _ppsRun,00004$
        ret
    __endasm;
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
        if (ppsRun)
            ppsPoll();
        else
            PCON = 1;
    }
}
