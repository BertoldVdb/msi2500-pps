/*
 * bitbang.c: the bit period the bit banged UART and I2C master share
 */
#include "msi2500.h"
#include "bitbang.h"

__data uint8_t delayL, delayH;          /* bit period, from the host */
static __data uint8_t dcL, dcH;

void bitDelay(void) __naked
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
