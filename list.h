/*
 * list.h: register lists, entries the host loads into one of two banks that
 * run from main (the entries themselves are in requests.h)
 */
#ifndef LIST_H
#define LIST_H

#include <stdint.h>

/* List state */
struct listState {
    uint8_t  run;                       /* a list is active */
    uint8_t  waiting;                   /* the streaming interrupt counts listCount */
    uint8_t  ppsPaused;                 /* PPS was on when the list started */
    uint8_t  spiTimeout;                /* the last list stopped waiting for SPI */
    uint8_t  queued;                    /* bank bits loaded behind the running one */
    uint8_t  bank;
    uint8_t  pos;                       /* the running bank's next entry, bytes */
    uint16_t pass;
};

extern volatile __data struct listState listBlk;

#define listRun         listBlk.run
#define listWaiting     listBlk.waiting
#define ppsPaused       listBlk.ppsPaused
#define listSpiTimeout  listBlk.spiTimeout
#define listQueued      listBlk.queued
#define listBank        listBlk.bank
#define listPos         listBlk.pos
#define listPass        listBlk.pass

extern __data uint16_t listCount;       /* streaming interrupts left to wait */
extern volatile __bit listDue;          /* main has a step to take */

/* Let the tuner port finish shifting before anything else writes it */
void portGap(void) __naked;

/* from main, when listDue is set */
void listStep(void);

/* from the EP0 handler, when a list has arrived */
void listLoaded(void);

/* request 0x5A */
void listRequest(void);

#endif /* LIST_H */
