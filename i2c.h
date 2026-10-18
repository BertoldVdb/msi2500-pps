/*
 * i2c.h: the bit banged I2C master
 */
#ifndef I2C_H
#define I2C_H

#include <stdint.h>

/* the write's data stage has arrived in xferBuf, n bytes */
void i2cWriteXfer(uint8_t n);

/* requests 0x55 to 0x58 */
void i2cWriteRequest(void);
void i2cReadRequest(void);
void i2cStatusRequest(void);
void i2cResetRequest(void);

#endif /* I2C_H */
