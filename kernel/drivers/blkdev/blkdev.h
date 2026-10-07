#ifndef BLKDEV_H
#define BLKDEV_H

#include <lib/std/stdint.h>

struct block_device_ops {
    int (*read_sector)(struct block_device *bdev, uint64_t lba, void *buf);
    int (*write_sector)(struct block_device *bdev, uint64_t lba, void *buf);
    uint64_t (*get_capacity)(struct block_device *bdev);
};

struct block_device {
    char name[32];          // "sda", "nvme0n1"
    uint64_t capacity_lba;
    struct block_device_ops *ops;
    void *private_data;     // Driver-specific data
};

#endif