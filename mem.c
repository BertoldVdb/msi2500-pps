/*
 * mem.c: the host's way into the core, with memory reads and writes and a
 * call to code it has put there
 */
#include "msi2500.h"
#include "ep0.h"
#include "mem.h"
#include "pps.h"

/* The call gate runs once, when a host asks for it, so it pays the movx to keep
   the directly addressed bytes for the capture path. */
static __xdata uint16_t callAddr;
static __xdata uint8_t  callR0, callR1, callA, callB, callDpl, callDph;
static __xdata uint8_t  callInA, callInB, callInDpl, callInDph;

/* The entry state for REQ_CALL, which the host writes with request 0x44:
   A, B, DPL, DPH, R0, R1, then two spare. The Makefile keeps --xram-size
   below it so the allocator cannot grow into these eight bytes. Code ends
   below 0x1800 and xdata is 0x1C00-0x1CFF: 0x1A00-0x1BFF is for the host's
   stubs. And 0x1800-0x19FF is reserved. */
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

void memReadRequest(void)
{
    __xdata uint8_t *p = (__xdata uint8_t *)(uint16_t)
        (0xC000 + (setup.wIndexL | ((uint16_t)setup.wIndexH << 8)));
    __idata uint8_t *q = (__idata uint8_t *)setup.wIndexL;
    uint8_t n = setup.wLength > sizeof xferBuf ? sizeof xferBuf : (uint8_t)setup.wLength;
    uint8_t i;

    EA = 0;
    if (setup.wValueL & 1) MEM_BUF_MAP(1);
    if (setup.wValueL & 2)
        for (i = 0; i < n; i++) xferBuf[i] = q[i];
    else
        for (i = 0; i < n; i++) xferBuf[i] = p[i];
    MEM_BUF_MAP(0);
    EA = 1;

    /* code and xdata are one RAM: the descriptor path sends it */
    ep0CodePtr = (__code uint8_t *)xferBuf;
    ep0TypeByte = xferBuf[1];
    ep0StartTx(n);
}

void memWriteRequest(void)
{
    ep0StartRx((__xdata uint8_t *)(uint16_t)(setup.wValueL | ((uint16_t)setup.wValueH << 8)),
               setup.wIndexL & 1, ACT_NONE);
}

void callRequest(void)
{
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
}
