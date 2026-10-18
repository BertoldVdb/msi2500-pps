/*
 * pps.c: the PPS timestamper, with the poll loop that counts between streaming
 * interrupts and catches GPIO_0's edges, and the request that runs it
 */
#include "msi2500.h"
#include "ep0.h"
#include "list.h"
#include "pps.h"
#include "stream.h"
#include "usb.h"

__data struct ppsState ppsBlk;
__xdata struct anchorState anchorBlk;

__data uint8_t  tickL, tickH;
__bit ppsRun;
__bit ppsSrcSof;
__data uint8_t sofDiv, sofLastFrame;
__data uint8_t sofInInterval;
__data uint8_t firstSofL, firstSofH, firstSofCarry;
volatile __bit inSofLatch;
volatile __bit sofStraddle;
volatile __bit tickCarried;
__data uint8_t ppsDiv;

volatile __xdata uint8_t anchorPending;

__data uint8_t usbGuard;

void ppsPoll(void) __naked
{
    __asm
        jnb     _ppsSrcSof,00020$

        ; Watching the USB frames, the interrupt does the latching and this is
        ; only a clock. Leaving GPIO_0 alone means a PPS pulse on the pin
        ; cannot take the capture path and overwrite what the interrupt just
        ; latched. Four nops for the movx and the test it replaces, so a turn
        ; is the same six cycles either way and nothing downstream changes.
    00010$:
        nop
        nop
        nop
        nop
    00012$:                             ; the carry rejoins here, four cycles
        djnz    _tickL,00010$           ; in, so the turn after it is short by
        djnz    _tickH,00013$           ; 2, exactly what the carry cost. And
        ret                             ;     256 carries with no packet: out,
    00013$:                             ;     so main can look at EP0
        jb      _ppsRun,00012$          ; 2, and 2+2 is the four
        ret

    00020$:
        mov     dptr,#0xC018            ; the GPIO input register
    00001$:
        movx    a,@dptr                 ; 2 cycles
        jb      acc.0,00003$            ; 2 cycles, the watched bit is high
    00005$:                             ; the carry rejoins on the djnz, so the
        djnz    _tickL,00001$           ; 2 cycles, count and loop
    00002$:                             ; tickL hit zero
        djnz    _tickH,00006$           ; 2, the carry, and 256 of them with
        ret                             ;     no packet is the way out
    00006$:
        jb      _ppsRun,00005$          ; 2, four, and the turn it rejoins is two
        ret
    00003$:                             ; a high sample
        djnz    _ppsDiv,00004$          ; only every Nth: at 500 Hz the host
        mov     _ppsDiv,(_ppsBlk + 19)  ; neither needs nor can use them all
        inc     (_ppsBlk + 14)
        clr     ea
        mov     (_ppsBlk + 6),_tickL
        mov     a,_tickH                ; tickH counts down: the carries are -tickH
        cpl     a
        inc     a
        mov     (_ppsBlk + 7),a
        mov     (_ppsBlk + 8),(_ppsBlk + 2)     ; edgeIrq = irqCount
        mov     (_ppsBlk + 9),(_ppsBlk + 3)
        mov     (_ppsBlk + 10),(_ppsBlk + 4)
        mov     (_ppsBlk + 11),(_ppsBlk + 5)
        mov     (_ppsBlk + 13),_usbGuard        ; edgeUsbGuard
        setb    ea
        ; which frame the edge fell in, read outside the critical section as it
        ; only moves every millisecond. Taken for a GPIO_0 pulse too, so that
        ; ties to a frame number as well.
        mov     dptr,#0x400C            ; USB_FRAME_L
        movx    a,@dptr
        mov     (_ppsBlk + 16),a
        inc     dptr
        movx    a,@dptr
        mov     (_ppsBlk + 17),a
        mov     dptr,#0xC018            ; back to the GPIO input register
    00004$:
        movx    a,@dptr                 ; 2 cycles
        jnb     acc.0,00001$            ; 2 cycles, back to counting when low
        djnz    _tickL,00004$           ; 2 cycles
        djnz    _tickH,00007$
        ret
    00007$:
        jb      _ppsRun,00004$
        ret
    __endasm;
}

void ppsRequest(void)
{
    ppsSrcSof = (setup.wValueL & 8) ? 1 : 0;
    ppsDivReload = setup.wValueH ? setup.wValueH : 1;
    sofDiv = 1;

    if (setup.wValueL & 1) {
        if (listRun) {
            ppsPaused = 1;          /* starts when the list ends */
        } else {
            if (!ppsRun) { tickL = 0; tickH = 0; ppsDiv = 1; }
            PPS_RUN(1);
        }
    } else {
        PPS_RUN(0);
        ppsPaused = 0;
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
}
