/*
 * ep0.c: endpoint 0, with the setup packet, its data stages and the request
 * dispatch. The standard requests are answered here, the rest by the part
 * of the firmware they belong to.
 */
#include "msi2500.h"
#include "core.h"
#include "desc.h"
#include "ep0.h"
#include "i2c.h"
#include "list.h"
#include "mem.h"
#include "pps.h"
#include "requests.h"
#include "stream.h"
#include "uart.h"
#include "usb.h"

__xdata struct setupPacket setup;

static __xdata uint8_t configuration = 1;
__xdata uint8_t ep0State;
static uint8_t rebootWait;

__xdata uint8_t *__data ep0Ptr;
static __xdata uint8_t ep0Remap;   /* 0x44: map the capture memory in */

__code  uint8_t *__data ep0CodePtr;
static __xdata uint16_t ep0Sent;
__xdata uint8_t ep0TypeByte;

static __xdata uint8_t ep0Action;
__xdata uint8_t xferBuf[XFER_BUF_LEN];

/* ------------------------------------------------------------------ */
/* Data stages */
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

void ep0StartTx(uint16_t length)
{
    ep0Sent = 0;
    if (length < setup.wLength)
        setup.wLength = length;

    USB_CSR0 = CSR0_SERV_RXPKTRDY;
    ep0SendChunk();
}

/* An OUT data stage into to (with remap through the capture memory window),
   action taken once it has all arrived. Called from the USB interrupt, so its
   parameters are not overlaid (sdcc manual, 3.7) */
#pragma save
#pragma nooverlay
void ep0StartRx(__xdata uint8_t *to, uint8_t remap, uint8_t action)
{
    ep0Ptr = to;
    ep0Remap = remap;
    ep0Action = action;
    USB_CSR0 = CSR0_SERV_RXPKTRDY;
    ep0State = EP0_RX;
}
#pragma restore

void ep0Stall(void)
{
    USB_CSR0 = CSR0_SERV_RXPKTRDY | CSR0_SENDSTALL;
}

void ep0Ack(void)
{
    USB_CSR0 = CSR0_SERV_RXPKTRDY | CSR0_DATAEND;
}

/* Return a single byte in the data stage (GET_CONFIGURATION etc). */
void ep0SendByte(uint8_t value)
{
    USB_CSR0 = CSR0_SERV_RXPKTRDY;
    USB_FIFO0[0] = value;
    USB_CSR0 = CSR0_TXPKTRDY | CSR0_DATAEND;
}

/* Pull an OUT data packet into the buffer ep0StartRx() set up. */
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
            if (act == ACT_REG_LIST) listLoaded();
            else if (act == ACT_UART) uartSend(n);
            else                 i2cWriteXfer(n);
        }
    } else {
        USB_CSR0 = CSR0_SERV_RXPKTRDY;
    }
}

/* ------------------------------------------------------------------ */
/* Setup packet dispatch */
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
        streamHalt(0);
        break;

    case REQ_SET_FEATURE:
        streamHalt(1);
        break;

    case REQ_SET_ADDRESS:
        USB_FADDR = setup.wValueL & 0x7F;
        ep0Ack();
        break;

    case REQ_GET_DESCRIPTOR:
        descRequest();
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
            /* either value puts the interface back to alt 0 (USB 2.0 9.1.1.5) */
            streamReset();
            configuration = setup.wValueL;
            ep0Ack();
        }
        break;

    /* one interface, number 0, and none while deconfigured */
    case REQ_GET_INTERFACE:
        if (setup.wIndexH != 0 || setup.wIndexL != 0 || !configuration)
            ep0Stall();
        else
            ep0SendByte(altSetting);
        break;

    case REQ_SET_INTERFACE:
        if (setup.wIndexH != 0 || setup.wIndexL != 0 || !configuration)
            ep0Stall();
        else
            streamSetInterface();
        break;

    case REQ_SYNCH_FRAME:
        ep0Stall();
        break;

    case REQ_BOOT:          bootRequest();          break;
    case REQ_WRITE_REG:     regWriteRequest();      break;
    case REQ_REG_LIST:      listRequest();          break;
    case REQ_READ_MEM:      memReadRequest();       break;
    case REQ_START_STREAM:  streamStartRequest();   break;
    case REQ_WRITE_MEM:     memWriteRequest();      break;
    case REQ_STOP_STREAM:   streamStopRequest();    break;
    case REQ_PPS_ENABLE:    ppsRequest();           break;
    case REQ_UART_TX:       uartRequest();          break;
    case REQ_I2C_WRITE:     i2cWriteRequest();      break;
    case REQ_I2C_READ:      i2cReadRequest();       break;
    case REQ_I2C_STATUS:    i2cStatusRequest();     break;
    case REQ_I2C_RESET:     i2cResetRequest();      break;
    case REQ_CALL:          callRequest();          break;

    default:
        ep0Stall();
        break;
    }
}

/* A bus reset: no transfer under way, and configured as at power on */
void ep0Reset(void)
{
    ep0State = EP0_IDLE;
    configuration = 1;
}

/* One service of EP0: what the handler does once it knows there is work. */
void ep0Service(void)
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
