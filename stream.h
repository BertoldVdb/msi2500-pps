/*
 * stream.h: the streaming endpoint 0x81, with the capture engine's interrupt,
 * starting and stopping it, the halt feature and the alternate settings
 */
#ifndef STREAM_H
#define STREAM_H

#include <stdint.h>

extern __bit streamOn;
extern volatile __bit stopPending;   /* stop at the next interrupt, not here */
extern volatile __bit streamParked;  /* one buffer handed over but not acked */
extern __xdata uint8_t epHalted;     /* endpoint 0x81 halted by the host */
extern __xdata uint8_t altSetting;

/* The vector: main.c sees this prototype */
void streamIsr(void) __interrupt (0) __using (1);

void streamEnable(uint8_t on);
void streamStop(void);
void streamReset(void);

/* SET_FEATURE (set) and CLEAR_FEATURE on the endpoint, SET_INTERFACE */
void streamHalt(uint8_t set);
void streamSetInterface(void);

/* requests 0x43 and 0x45 */
void streamStartRequest(void);
void streamStopRequest(void);

#endif /* STREAM_H */
