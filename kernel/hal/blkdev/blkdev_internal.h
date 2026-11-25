#ifndef BLKDEV_INTERNAL_H
#define BLKDEV_INTERNAL_H

#include "blkdev.h"

/** Return the number of entries in the block-device registry.
 * @return Registered device count.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
uint32_t blkdev_registry_count(void);

/** Remove a device from the block-device registry.
 * @param bdev Device descriptor to remove.
 * @return 0 when removed, -1 when not registered.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_unregister(struct block_device *bdev);

/** Reset private partition-table state during block-device initialization.
 * @return None.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
void blkdev_partition_init(void);

/** Check whether a descriptor is one of the partition devices.
 * @param bdev Candidate descriptor.
 * @return Nonzero for a partition device.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_is_device(struct block_device *bdev);

/** Invalidate cached partition metadata when table sectors are written.
 * @param bdev Device being written.
 * @param lba Sector being written.
 * @return None.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
void blkdev_partition_note_write(struct block_device *bdev, uint64_t lba);

#endif
