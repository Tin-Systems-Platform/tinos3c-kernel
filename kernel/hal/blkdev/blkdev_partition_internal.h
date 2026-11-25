#ifndef BLKDEV_PARTITION_INTERNAL_H
#define BLKDEV_PARTITION_INTERNAL_H

#include "blkdev.h"

#define PARTITION_MAX_ENTRIES 128U
#define GPT_ENTRY_COUNT 128U
#define GPT_ENTRY_SIZE 128U
#define GPT_TABLE_BYTES (GPT_ENTRY_COUNT * GPT_ENTRY_SIZE)
#define GPT_TABLE_SECTORS (GPT_TABLE_BYTES / BLKDEV_SECTOR_SIZE)
#define PARTITION_ALIGNMENT_LBA 2048ULL

extern uint8_t gpt_entries[GPT_TABLE_BYTES];
extern struct block_device *gpt_cache_disk;
extern uint32_t gpt_cache_entries_crc;
extern uint64_t gpt_cache_first_usable;
extern uint64_t gpt_cache_last_usable;
extern uint8_t gpt_cache_valid;

/** Decode a little-endian 32-bit integer from disk bytes.
 * @param p Pointer to at least four bytes.
 * @return Decoded integer.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
uint32_t read_le32(const uint8_t *p);

/** Decode a little-endian 64-bit integer from disk bytes.
 * @param p Pointer to at least eight bytes.
 * @return Decoded integer.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
uint64_t read_le64(const uint8_t *p);

/** Update an IEEE CRC-32 checksum.
 * @param crc Previous CRC state.
 * @param bytes Data to checksum.
 * @param length Number of bytes.
 * @return Updated CRC state.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
uint32_t crc32_update(uint32_t crc, const uint8_t *bytes, uint32_t length);

/** Validate a descriptor as a registered whole disk.
 * @param disk Candidate descriptor.
 * @return Nonzero when it is a registered whole disk.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_valid_disk(struct block_device *disk);

/** Scan an MBR sector and register its supported primary partitions.
 * @param disk Whole-disk descriptor.
 * @param sector Sector-zero contents.
 * @return Number registered, or -1 when the table is invalid.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_scan_mbr(struct block_device *disk, const uint8_t *sector);

/** Validate and scan a primary GPT and register its partitions.
 * @param disk Whole-disk descriptor.
 * @return Number registered, or -1 when the table is invalid.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_scan_gpt(struct block_device *disk);

/** Refresh partition devices for a disk after a table update.
 * @param disk Whole-disk descriptor.
 * @return Number reloaded, or -1 on I/O error.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_refresh_disk(struct block_device *disk);

/** Reset cached GPT state during block-device initialization.
 * @return None.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
void blkdev_partition_table_init(void);

#endif
