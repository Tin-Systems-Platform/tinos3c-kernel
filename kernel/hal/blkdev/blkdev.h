#ifndef BLKDEV_H
#define BLKDEV_H

//Includes
#include <lib/std/stdint.h>

//Function declarations
void blkdev_init(void);
/** Scan registered whole disks and register partitions described by MBR or GPT.
 * @return Number of partitions registered during this scan.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_scan_partitions(void);
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

enum blkdev_partition_table {
    BLKDEV_PARTITION_TABLE_NONE = 0,
    BLKDEV_PARTITION_TABLE_MBR = 1,
    BLKDEV_PARTITION_TABLE_GPT = 2
};

struct blkdev_partition_info {
    char name[32];
    uint64_t start_lba;
    uint64_t sector_count;
    uint32_t number;
};

/** Register a block device with the kernel registry. 
 * @param bdev Device to register. 
 * @return 0 on success. 
 * @copyright See LICENSE.md. 
 * @date 2026-11-25 */
int blkdev_register(struct block_device *bdev);

/** Return a registered device by zero-based index, or NULL. 
 * @copyright See LICENSE.md. 
 * @date 2026-11-25 */
struct block_device *blkdev_get(uint32_t index);

/** Return the number of registered whole disks, excluding partition devices.
 * @return Whole-disk count.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
uint32_t blkdev_disk_count(void);

/** Return a whole disk by its zero-based disk number.
 * @param index Zero-based whole-disk index.
 * @return Disk descriptor or NULL when out of range.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
struct block_device *blkdev_disk_get(uint32_t index);

/** Get the current partition table type for a whole disk.
 * @param disk Whole-disk descriptor.
 * @return MBR, GPT, NONE, or NONE when the disk cannot be read.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
enum blkdev_partition_table blkdev_disk_table(struct block_device *disk);

/** Get partition metadata for a whole disk by its table partition number.
 * @param disk Whole-disk descriptor.
 * @param number MBR/GPT partition number, starting at one.
 * @param info Output metadata.
 * @return 0 when found, -1 when absent or invalid.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_get_info(struct block_device *disk, uint32_t number,
                              struct blkdev_partition_info *info);

/** Initialize an empty disk with an MBR or GPT partition table.
 * @param disk Whole-disk descriptor.
 * @param table Requested table type.
 * @return 0 on success, -1 when unsupported, nonempty, or I/O fails.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_disk_initialize(struct block_device *disk,
                           enum blkdev_partition_table table);

/** Create a partition, choosing a first-fit aligned start when start_lba is 0.
 * @param disk Whole-disk descriptor.
 * @param size_lba Requested partition size in sectors.
 * @param start_lba Requested start sector, or 0 for automatic placement.
 * @param number Output table partition number.
 * @return 0 on success, -1 when it does not fit or I/O fails.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_create(struct block_device *disk, uint64_t size_lba,
                            uint64_t start_lba, uint32_t *number);

/** Resize a partition in place while keeping its start sector unchanged.
 * @param disk Whole-disk descriptor.
 * @param number MBR/GPT partition number.
 * @param new_size_lba New size in sectors.
 * @return 0 on success, -1 when it overlaps, does not fit, or I/O fails.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_resize(struct block_device *disk, uint32_t number,
                            uint64_t new_size_lba);

/** Delete a partition entry without erasing its data sectors.
 * @param disk Whole-disk descriptor.
 * @param number MBR/GPT partition number.
 * @return 0 on success, -1 when absent or I/O fails.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_delete(struct block_device *disk, uint32_t number);

/** Read one sector after validating device, buffer, and LBA. 
 * @param bdev Device to read from. 
 * @param lba Sector to read. 
 * @param buf 512-byte destination. 
 * @return 0 on success. 
 * @copyright See LICENSE.md. 
 * @date 2026-11-25 */
int blkdev_read(struct block_device *bdev, uint64_t lba, void *buf);

/** Write one sector after validating device, buffer, and LBA. 
 * @param bdev Device to write to. 
 * @param lba Sector to write. 
 * @param buf 512-byte source. 
 * @return 0 on success. 
 * @copyright See LICENSE.md. 
 * @date 2026-11-25 */
int blkdev_write(struct block_device *bdev, uint64_t lba, const void *buf);

struct block_device {
    char name[32];          // "sda", "nvme0n1"
    uint64_t capacity_lba;
    struct block_device_ops *ops;
    void *private_data;     // Driver-specific data
};

#endif
