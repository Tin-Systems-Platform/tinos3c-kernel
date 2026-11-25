#include "blkdev.h"

#include <lib/std/stdio.h>

static struct block_device *devices[BLKDEV_MAX_DEVICES];
static uint32_t device_count;

/** Register a validated block device. @param bdev Device descriptor. @return 0 on success, -1 for invalid/duplicate/full registry. @copyright See LICENSE.md. @date 2026-11-25 */
int blkdev_register(struct block_device *bdev)
{
    if (bdev == 0 || bdev->ops == 0 || bdev->ops->read_sector == 0 ||
        bdev->ops->write_sector == 0 || bdev->capacity_lba == 0 ||
        device_count >= BLKDEV_MAX_DEVICES)
        return -1;
    for (uint32_t i = 0; i < device_count; ++i)
        if (devices[i] == bdev)
            return -1;
    devices[device_count++] = bdev;
    printf("BLKDEV: registered %s (%u sectors)\n", bdev->name,
           bdev->capacity_lba);
    return 0;
}

/** Return the registered block device at an index. @param index Zero-based registry index. @return Device or NULL when out of range. @copyright See LICENSE.md. @date 2026-11-25 */
struct block_device *blkdev_get(uint32_t index)
{
    return index < device_count ? devices[index] : 0;
}

/** Read a validated sector through the device driver. @param bdev Device. @param lba Sector index. @param buf Writable 512-byte destination. @return 0 on success, -1 on invalid input or I/O error. @copyright See LICENSE.md. @date 2026-11-25 */
int blkdev_read(struct block_device *bdev, uint64_t lba, void *buf)
{
    if (bdev == 0 || buf == 0 || bdev->ops == 0 ||
        bdev->ops->read_sector == 0 || lba >= bdev->capacity_lba)
        return -1;
    return bdev->ops->read_sector(bdev, lba, buf);
}

/** Write a validated sector through the device driver. @param bdev Device. @param lba Sector index. @param buf Source of 512 bytes. @return 0 on success, -1 on invalid input or I/O error. @copyright See LICENSE.md. @date 2026-11-25 */
int blkdev_write(struct block_device *bdev, uint64_t lba, const void *buf)
{
    if (bdev == 0 || buf == 0 || bdev->ops == 0 ||
        bdev->ops->write_sector == 0 || lba >= bdev->capacity_lba)
        return -1;
    return bdev->ops->write_sector(bdev, lba, buf);
}

/** Initialize the block-device registry. @return None. @copyright See LICENSE.md. @date 2026-11-25 */
void blkdev_init(void)
{
    device_count = 0;
    for (uint32_t i = 0; i < BLKDEV_MAX_DEVICES; ++i)
        devices[i] = 0;
    printf("BLKDEV: registry ready\n");
}

/** Placeholder for future interrupt-driven block I/O. @return None. @copyright See LICENSE.md. @date 2026-11-25 */
void enable_interupts_blockdev(void)
{
    printf("BLKDEV: interrupt-driven I/O is not enabled\n");
}
