/*
 * list.c: register lists. Two banks, so the host can load one while the
 * other runs. A list runs from main, so PPS pauses while it runs, as the poll
 * loop holds main, and resumes when it ends. The entries are in requests.h.
 */
#include "msi2500.h"
#include "core.h"
#include "ep0.h"
#include "list.h"
#include "pps.h"
#include "requests.h"

#define LIST_BANK_BYTES         252     /* 63 entries, so offsets in a bank fit a byte */
#define LIST_PAGE               0x1D    /* bank 0 at 0x1D00, bank 1 at 0x1E00 */
#define LIST_GAP_DEFAULT        53      /* 2 cycle turns, 3.5 us: a port word takes ~2.15 us */

volatile __data struct listState listBlk;

/* Each bank on its own page: an entry's address is the page and a byte offset.
   The Makefile keeps the allocated xdata below 0x1D00. */
__xdata __at (LIST_PAGE << 8) uint8_t listBuf[2][256];
static __xdata uint8_t listLen[2];      /* bytes */
static __xdata uint8_t listLoad;        /* the bank being loaded, bit 1 to queue it */
static __data uint8_t listGap = LIST_GAP_DEFAULT;
static __data uint8_t listPage, listEnd;   /* the running bank's page, end offset */
static __data uint32_t listVal;          /* the entry being run */
__data uint16_t listCount;              /* streaming interrupts left to wait */
static __data uint8_t gapCount, usCount, usL, usH;
volatile __bit listDue;                 /* main has a step to take */

void portGap(void) __naked
{
    __asm
        mov     _gapCount,_listGap
    00001$:
        djnz    _gapCount,00001$        ; 2 cycles a turn
        ret
    __endasm;
}

/* usH:usL turns of 31 cycles, ~1.03 us. usH is already raised by one when usL
   is not 0.
   A stop from the host ends it early. */
static void waitUs(void) __naked
{
    __asm
    00001$:
        mov     _usCount,#12            ; 2
    00002$:
        djnz    _usCount,00002$         ; 24
        mov     a,(_listBlk + 0)        ; 1, listRun
        jz      00003$                  ; 2
        djnz    _usL,00001$             ; 2
        djnz    _usH,00001$
    00003$:
        ret
    __endasm;
}

static void listStart(uint8_t bank)
{
    listBank = bank;
    listPage = LIST_PAGE + bank;
    listPos = 0;
    listEnd = listLen[bank];
    listPass = 0;
}

/* to the other bank if it is queued */
static uint8_t listSwitch(void)
{
    uint8_t other = listBank ^ 1, bit = other ? 2 : 1;

    if (!(listQueued & bit)) return 0;
    listQueued &= (uint8_t) ~bit;
    listStart(other);
    return 1;
}

void listStep(void)
{
    uint8_t op;
    uint16_t arg;

    listDue = 0;

    for (;;) {
        /* Take the entry with interrupts off: a load stops the list before it writes
           a bank, so a running list never reads one half loaded */
        EA = 0;
        if (listRun && listPos == listEnd && !listSwitch()) listRun = 0;
        if (!listRun) {
            EA = 1;
            break;
        }
        listVal = *(__xdata uint32_t *)(((uint16_t)listPage << 8) | listPos);
        listPos += 4;
        EA = 1;

        /* laid out as the port is: register, then the value */
        op = (uint8_t)listVal;
        listVal >>= 8;

        if (op < 0x20) {
            /* the shadows are shared with the USB interrupt's UART and I2C code */
            EA = 0;
            mmioWrite(op, listVal);
            EA = 1;
            if (op == MMIO_REG_TUNER) portGap();
            continue;
        }

        arg = (uint16_t)listVal;

        switch (op) {
        case LIST_WAIT_US:
            if (!arg) break;
            usL = (uint8_t)arg;
            usH = (uint8_t)(arg >> 8);
            if (usL) usH++;
            waitUs();
            break;

        case LIST_WAIT_IRQ:
            if (!arg) break;
            EA = 0;
            listCount = arg;
            listWaiting = 1;
            EA = 1;
            return;

        case LIST_REPEAT:
            if (!arg || listPass < arg) {
                listPass++;
                listPos = 0;
            }
            break;

        case LIST_SWITCH:
            EA = 0;
            listSwitch();
            EA = 1;
            break;

        case LIST_WAIT_SPI:
            /* read index 5 bits 5 and 4 together, as the EEPROM code polls them: bit 5
               alone comes back ~0.7 us into a transfer, both at its end */
            while (listRun && (*MMIO_RD(5) & 0x30) != 0x30) {
                if (!--arg) {
                    listSpiTimeout = 1;
                    listRun = 0;
                    break;
                }
            }
            break;

        default:
            listRun = 0;
            break;
        }
    }
}

void listLoaded(void)
{
    uint8_t bank = listLoad & 1;

    listLen[bank] = (uint8_t)(uint16_t)ep0Ptr;     /* the bank starts on a page */

    /* queued behind a list that is still running: it starts when that one switches
       to it or ends */
    if ((listLoad & 2) && listRun) {
        listQueued |= bank ? 2 : 1;
        return;
    }

    if (ppsRun) {
        PPS_RUN(0);
        ppsPaused = 1;
        tickL = 1;                      /* force the poll loop out on its next DJNZ */
    }

    listStart(bank);
    listSpiTimeout = 0;
    listRun = 1;
    listDue = 1;
}

void listRequest(void)
{
    uint8_t bank = setup.wValueL & 1, queue = setup.wValueL & 2;

    if (queue && listRun) {
        /* the running bank cannot be loaded under it */
        if (bank == listBank || !setup.wLength) { ep0Stall(); return; }
    } else {
        listRun = 0;
        listWaiting = 0;
        listDue = 0;
        listQueued = 0;
        queue = 0;
    }

    if (!setup.wLength) { ep0Ack(); return; }    /* stop */

    if ((setup.wLength & 3) || setup.wLength > LIST_BANK_BYTES) { ep0Stall(); return; }

    listGap = setup.wIndexL ? setup.wIndexL : LIST_GAP_DEFAULT;
    listQueued &= (uint8_t) ~(bank ? 2 : 1);
    listLoad = bank | queue;
    ep0StartRx((__xdata uint8_t *)((uint16_t)(LIST_PAGE + bank) << 8), 0, ACT_REG_LIST);
}
