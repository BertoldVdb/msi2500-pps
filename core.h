/*
 * core.h: the 8051 core's waits and reboot, and the writes to the ADC / tuner /
 * GPIO block with the shadows of the registers this firmware reads back
 */
#ifndef CORE_H
#define CORE_H

#include <stdint.h>

/* Shadows of host written values to reg 3 and 8 */
extern __xdata uint32_t reg3Shadow;
extern __xdata uint32_t reg8Shadow;

extern __xdata uint8_t rebootPending;   /* 0 none, else target + 1: see
                                           reboot() and request 0x40 */

void usbSettle(void);
void reboot(uint8_t fromRam);

void mmioWriteNoShadow(uint8_t reg, uint32_t val);
void mmioWrite(uint8_t reg, uint32_t val);
void gpioApply(void);

/* requests 0x40 and 0x41 */
void bootRequest(void);
void regWriteRequest(void);

#endif /* CORE_H */
