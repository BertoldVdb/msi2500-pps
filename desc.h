/*
 * desc.h: what the device says it is, in the USB descriptors and the firmware
 * block a host identifies this firmware by
 */
#ifndef DESC_H
#define DESC_H

/* GET_DESCRIPTOR */
void descRequest(void);

/* 1 when the configuration says self powered */
uint8_t descSelfPowered(void);

#endif /* DESC_H */
