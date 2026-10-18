/*
 * desc.c: what the device says it is, in the USB descriptors and the firmware
 * block a host identifies this firmware by
 */
#include "msi2500.h"
#include "core.h"
#include "desc.h"
#include "ep0.h"
#include "list.h"
#include "pps.h"
#include "requests.h"

/* A host loading this image patches the two ids below into it first, so the
   device keeps the identity it had. */
static __code uint8_t deviceDesc[18] = {
    18, DESC_DEVICE,
    0x00, 0x02,             /* bcdUSB 2.00 */
    0xFF, 0xFF, 0xFF,       /* vendor specific class/subclass/protocol */
    EP0_MAXPACKET,
    0xD0, 0x16,             /* idVendor 0x16D0 */
    0x8C, 0x15,             /* idProduct 0x158C */
    0x00, 0x01,             /* bcdDevice */
    1, 2, 0,                /* iManufacturer, iProduct, no serial */
    1                       /* bNumConfigurations */
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
    __data struct ppsState *ppsAt;      /* the status blocks: internal RAM, */
    __xdata struct anchorState *anchorAt;   /* xdata, */
    volatile __data struct listState *listAt;   /* internal RAM */
    uint8_t  spare[4];
};

__code __at (0x0040) struct fwInfo fwBlock = {
    { 'B', 'V', 'D', 'B' },
    { 0, 0, 0, 0, 0, 0, 0, 0 },
    deviceDesc,
    stringSerial,
    &ppsBlk,
    &anchorBlk,
    &listBlk,
    { 0, 0, 0, 0 }
};

static __code uint8_t qualifierDesc[10] = {
    10, DESC_DEVICE_QUALIFIER,
    0x00, 0x02,
    0xFF, 0xFF, 0xFF,
    8,
    1, 0
};

/* High speed configuration: one interface with five alternate settings.
 *   alt 0: no endpoints (idle)
 *   alt 1: isochronous IN, 3 x 1024 bytes per microframe
 *   alt 2: isochronous IN, 1024 bytes per microframe
 *   alt 3: bulk IN, 512 bytes
 *   alt 4: isochronous IN, 2 x 1024 bytes per microframe
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

void descRequest(void)
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
