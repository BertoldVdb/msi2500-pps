/*
 * mem.h: the host's way into the core, with memory reads and writes and a
 * call to code it has put there
 */
#ifndef MEM_H
#define MEM_H

/* requests 0x42, 0x44 and 0x59 */
void memReadRequest(void);
void memWriteRequest(void);
void callRequest(void);

#endif /* MEM_H */
