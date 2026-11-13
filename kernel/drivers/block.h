#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>

/* Find the primary storage device. Returns 0 on success, -1 if none. */
int block_init(void);

/* Total sectors on the device */
uint64_t block_sectors(void);

/* Read or write one 512-byte sector. Returns 0 on success, -1 on error. */
int block_read(uint64_t lba, void *buf);
int block_write(uint64_t lba, const void *buf);

#endif