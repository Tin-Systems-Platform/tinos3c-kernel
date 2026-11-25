#ifndef BLKDEV_H
#define BLKDEV_H

//Includes
#include <lib/std/stdint.h>

//Function declarations
void blkdev_init(void);
void enable_interupts_blockdev(void);


// Structs
struct block_device;

struct block_device_ops {
    int (*read_sector)(struct block_device *bdev, uint64_t lba, void *buf);
    int (*write_sector)(struct block_device *bdev, uint64_t lba, const void *buf);
    uint64_t (*get_capacity)(struct block_device *bdev);
};

#define BLKDEV_SECTOR_SIZE 512U
#define BLKDEV_MAX_DEVICES 16U

/** Register a block device with the kernel registry. @return 0 on success. @copyright See LICENSE.md. @date 2026-11-25 */
int blkdev_register(struct block_device *bdev);
/** Return a registered device by zero-based index, or NULL. @copyright See LICENSE.md. @date 2026-11-25 */
struct block_device *blkdev_get(uint32_t index);
/** Read one sector after validating device, buffer, and LBA. @return 0 on success. @copyright See LICENSE.md. @date 2026-11-25 */
int blkdev_read(struct block_device *bdev, uint64_t lba, void *buf);
/** Write one sector after validating device, buffer, and LBA. @return 0 on success. @copyright See LICENSE.md. @date 2026-11-25 */
int blkdev_write(struct block_device *bdev, uint64_t lba, const void *buf);

struct block_device {
    char name[32];          // "sda", "nvme0n1"
    uint64_t capacity_lba;
    struct block_device_ops *ops;
    void *private_data;     // Driver-specific data
};

#endif
