#ifndef FAT_BLOCK_DEVICE_H
#define FAT_BLOCK_DEVICE_H

#include <stdint.h>

// block device interface: abstraction from underlying storage device

typedef struct {
    int(*read_sectors)(void *device, uint32_t sector, uint32_t count, void *buffer);
    int(*write_sectors)(void *device, uint32_t sector, uint32_t count, const void *buffer);
    void *device_data;
} fat_block_device_t;

// sector size
#define FAT_SECTOR_SIZE 512

#endif