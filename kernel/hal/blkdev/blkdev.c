#include "blkdev.h"

#include <lib/std/stdio.h>

static struct block_device *devices[BLKDEV_MAX_DEVICES];
static uint32_t device_count;

#define PARTITION_MAX_ENTRIES 128U

typedef struct {
    struct block_device bdev;
    struct block_device *parent;
    uint64_t start_lba;
} partition_device_t;

static partition_device_t partitions[BLKDEV_MAX_DEVICES];
static uint32_t partition_count;

static struct block_device *scanned_disks[BLKDEV_MAX_DEVICES];
static uint32_t scanned_disk_count;

/** Decode a little-endian 32-bit integer from on-disk bytes.
 * @param p Pointer to at least four bytes.
 * @return Decoded integer.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** Decode a little-endian 64-bit integer from on-disk bytes.
 * @param p Pointer to at least eight bytes.
 * @return Decoded integer.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static uint64_t read_le64(const uint8_t *p)
{
    return (uint64_t)read_le32(p) | ((uint64_t)read_le32(p + 4) << 32);
}

/** Update an IEEE CRC-32 checksum.
 * @param crc Previous CRC state.
 * @param bytes Data to checksum.
 * @param length Number of bytes.
 * @return Updated CRC state.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static uint32_t crc32_update(uint32_t crc, const uint8_t *bytes, uint32_t length)
{
    for (uint32_t i = 0; i < length; ++i) {
        crc ^= bytes[i];

        for (uint32_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }

    return crc;
}

/** Route a partition read through its parent while enforcing partition bounds.
 * @param bdev Partition descriptor.
 * @param lba Partition-relative sector.
 * @param buffer Sector destination.
 * @return 0 on success, -1 on invalid descriptor or I/O failure.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int partition_read(struct block_device *bdev, uint64_t lba, void *buffer)
{
    partition_device_t *partition;

    if (bdev == 0 || buffer == 0 || bdev->private_data == 0)
        return -1;

    partition = (partition_device_t *)bdev->private_data;

    if (partition->parent == 0 || lba >= bdev->capacity_lba ||
        partition->start_lba > ~0ULL - lba)
        return -1;

    return blkdev_read(partition->parent, partition->start_lba + lba, buffer);
}

/** Route a partition write through its parent while enforcing partition bounds.
 * @param bdev Partition descriptor.
 * @param lba Partition-relative sector.
 * @param buffer Sector source.
 * @return 0 on success, -1 on invalid descriptor or I/O failure.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int partition_write(struct block_device *bdev, uint64_t lba,
                           const void *buffer)
{
    partition_device_t *partition;

    if (bdev == 0 || buffer == 0 || bdev->private_data == 0)
        return -1;

    partition = (partition_device_t *)bdev->private_data;

    if (partition->parent == 0 || lba >= bdev->capacity_lba ||
        partition->start_lba > ~0ULL - lba)
        return -1;
    
    return blkdev_write(partition->parent, partition->start_lba + lba, buffer);
}

/** Return a partition's sector count.
 * @param bdev Partition descriptor.
 * @return Sector count, or zero for a null descriptor.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static uint64_t partition_capacity(struct block_device *bdev)
{
    return bdev == 0 ? 0 : bdev->capacity_lba;
}

static struct block_device_ops partition_ops = {
    .read_sector = partition_read,
    .write_sector = partition_write,
    .get_capacity = partition_capacity
};

/** Add a validated partition to the global block-device registry.
 * @param disk Parent whole-disk device.
 * @param start First sector on the parent disk.
 * @param count Number of sectors.
 * @param number Partition number used in its device name.
 * @return 0 on success, -1 when invalid or the registry is full.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int register_partition(struct block_device *disk, uint64_t start,
                              uint64_t count, uint32_t number)
{
    partition_device_t *partition;
    uint32_t n = 0;

    if (disk == 0 || count == 0 || start == 0 || start >= disk->capacity_lba ||
        count > disk->capacity_lba - start ||
        partition_count >= BLKDEV_MAX_DEVICES || device_count >= BLKDEV_MAX_DEVICES)
        return -1;

    partition = &partitions[partition_count];
    partition->parent = disk;
    partition->start_lba = start;

    partition->bdev.capacity_lba = count;
    partition->bdev.ops = &partition_ops;

    partition->bdev.private_data = partition;

    while (n < sizeof(partition->bdev.name) - 1 && disk->name[n] != 0) {
        partition->bdev.name[n] = disk->name[n];
        ++n;
    }

    if (n + 1 < sizeof(partition->bdev.name)) {
        uint32_t digits[10];
        uint32_t digit_count = 0;

        partition->bdev.name[n++] = 'p';

        do {
            digits[digit_count++] = number % 10;
            number /= 10;
        } while (number != 0 && digit_count < 10);

        while (digit_count != 0 && n < sizeof(partition->bdev.name) - 1)
            partition->bdev.name[n++] = (char)('0' + digits[--digit_count]);
    }

    partition->bdev.name[n] = 0;

    if (blkdev_register(&partition->bdev) != 0)
        return -1;
    
    ++partition_count;
    return 0;
}

/** Parse standard primary MBR entries.
 * @param disk Disk whose sector zero has been read.
 * @param sector Sector-zero contents.
 * @return Number of partitions registered, or -1 for an invalid table.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int scan_mbr(struct block_device *disk, const uint8_t *sector)
{
    int found = 0;

    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return -1;
    
    for (uint32_t i = 0; i < 4; ++i) {
        const uint8_t *entry = sector + 446 + i * 16;

        uint64_t start = read_le32(entry + 8);
        uint64_t count = read_le32(entry + 12);

        if (entry[4] == 0 || count == 0)
            continue;

        if (register_partition(disk, start, count, i + 1) == 0)
            ++found;
    }
    return found;
}

/** Validate and scan a primary GPT partition table.
 * @param disk Whole-disk device.
 * @return Number of partitions registered, or -1 for an invalid GPT.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int scan_gpt(struct block_device *disk)
{
    uint8_t sector[BLKDEV_SECTOR_SIZE];
    uint8_t header[BLKDEV_SECTOR_SIZE];

    uint32_t header_size, entry_count, entry_size, expected_entries_crc;
    uint64_t entries_lba, entries_bytes;
    uint32_t crc;

    int found = 0;

    if (blkdev_read(disk, 1, sector) != 0 || sector[0] != 'E' ||
        sector[1] != 'F' || sector[2] != 'I' || sector[3] != ' ' ||
        sector[4] != 'P' || sector[5] != 'A' || sector[6] != 'R' ||
        sector[7] != 'T')
        return -1;

    header_size = read_le32(sector + 12);

    if (header_size < 92 || header_size > BLKDEV_SECTOR_SIZE)
        return -1;
    
    for (uint32_t i = 0; i < BLKDEV_SECTOR_SIZE; ++i) header[i] = sector[i];
    uint32_t expected_header_crc = read_le32(header + 16);

    header[16] = header[17] = header[18] = header[19] = 0;
    crc = crc32_update(0xFFFFFFFFU, header, header_size) ^ 0xFFFFFFFFU;

    if (crc != expected_header_crc)
        return -1;

    entries_lba = read_le64(sector + 72);
    entry_count = read_le32(sector + 80);

    entry_size = read_le32(sector + 84);
    expected_entries_crc = read_le32(sector + 88);

    if (entry_count == 0 || entry_count > PARTITION_MAX_ENTRIES ||
        entry_size < 128 || entry_size > BLKDEV_SECTOR_SIZE ||
        (entry_size & 7U) != 0 || entries_lba == 0 ||
        entries_lba >= disk->capacity_lba ||
        (uint64_t)entry_count > ~0ULL / entry_size)
        return -1;
    
    entries_bytes = (uint64_t)entry_count * entry_size;

    if ((entries_bytes + BLKDEV_SECTOR_SIZE - 1) / BLKDEV_SECTOR_SIZE >
        disk->capacity_lba - entries_lba)
        return -1;

    crc = 0xFFFFFFFFU;

    for (uint64_t byte_offset = 0; byte_offset < entries_bytes;) {
        uint64_t lba = entries_lba + byte_offset / BLKDEV_SECTOR_SIZE;
        
        uint32_t within = (uint32_t)(byte_offset % BLKDEV_SECTOR_SIZE);
        uint32_t amount = BLKDEV_SECTOR_SIZE - within;

        if (amount > entries_bytes - byte_offset) amount = (uint32_t)(entries_bytes - byte_offset);
        if (blkdev_read(disk, lba, sector) != 0) return -1;

        crc = crc32_update(crc, sector + within, amount);
        byte_offset += amount;
    }

    if ((crc ^ 0xFFFFFFFFU) != expected_entries_crc)
        return -1;

    for (uint32_t i = 0; i < entry_count; ++i) {
        uint64_t offset = (uint64_t)i * entry_size;
        uint64_t lba = entries_lba + offset / BLKDEV_SECTOR_SIZE;

        uint32_t within = (uint32_t)(offset % BLKDEV_SECTOR_SIZE);
        uint8_t entry[BLKDEV_SECTOR_SIZE];

        if (blkdev_read(disk, lba, sector) != 0) return -1;

        uint32_t first_amount = BLKDEV_SECTOR_SIZE - within;
        if (first_amount > entry_size) first_amount = entry_size;

        for (uint32_t j = 0; j < first_amount; ++j) entry[j] = sector[within + j];

        if (first_amount < entry_size) {
            if (lba + 1 >= disk->capacity_lba ||
                blkdev_read(disk, lba + 1, sector) != 0) return -1;

            for (uint32_t j = first_amount; j < entry_size; ++j)
                entry[j] = sector[j - first_amount];
        }

        int unused = 1;
        for (uint32_t j = 0; j < 16; ++j) if (entry[j] != 0) { unused = 0; break; }

        if (unused) continue;
        uint64_t first = read_le64(entry + 32);
        uint64_t last = read_le64(entry + 40);

        if (last < first || first == 0 || last >= disk->capacity_lba)
            continue;

        if (register_partition(disk, first, last - first + 1, i + 1) == 0)
            ++found;
    }

    return found;
}

/** Discover MBR or GPT partitions on registered whole disks.
 * Whole disks present at the start of the call are scanned once; discovered
 * partitions are appended to the same registry and use parent-relative I/O.
 * @return Number of partitions registered during this call.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_scan_partitions(void)
{
    uint32_t disks_to_scan = device_count;
    int total_found = 0;

    for (uint32_t i = 0; i < disks_to_scan; ++i) {
        struct block_device *disk = devices[i];

        uint8_t sector[BLKDEV_SECTOR_SIZE];
        int already_scanned = 0;

        for (uint32_t j = 0; j < scanned_disk_count; ++j)
            if (scanned_disks[j] == disk) { already_scanned = 1; break; }
        
        if (already_scanned || disk == 0 || disk->capacity_lba < 2 ||
            blkdev_read(disk, 0, sector) != 0)
            continue;

        int result;

        if (sector[510] == 0x55 && sector[511] == 0xAA &&
            sector[450] == 0xEE)
            result = scan_gpt(disk);
        else
            result = scan_mbr(disk, sector);

        if (scanned_disk_count < BLKDEV_MAX_DEVICES)
            scanned_disks[scanned_disk_count++] = disk;
        if (result > 0) total_found += result;

        printf("BLKDEV: scanned %s, %u partitions\n", disk->name,
               result > 0 ? (uint32_t)result : 0U);
    }

    return total_found;
}

/** Register a validated block device.
 *  @param bdev Device descriptor.
 *  @return 0 on success, -1 for invalid/duplicate/full registry.
 *  @copyright See LICENSE.md.
 *  @date 2026-11-25 */
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

/** Return the registered block device at an index. 
 * @param index Zero-based registry index. 
 * @return Device or NULL when out of range. 
 * @copyright See LICENSE.md. 
 * @date 2026-11-25 */
struct block_device *blkdev_get(uint32_t index)
{
    return index < device_count ? devices[index] : 0;
}

/** Read a validated sector through the device driver. 
 * @param bdev Device. 
 * @param lba Sector index. 
 * @param buf Writable 512-byte destination. 
 * @return 0 on success, -1 on invalid input or I/O error. 
 * @copyright See LICENSE.md. 
 * @date 2026-11-25 */
int blkdev_read(struct block_device *bdev, uint64_t lba, void *buf)
{
    if (bdev == 0 || buf == 0 || bdev->ops == 0 ||
        bdev->ops->read_sector == 0 || lba >= bdev->capacity_lba)
        return -1;

    return bdev->ops->read_sector(bdev, lba, buf);
}

/** Write a validated sector through the device driver. 
 * @param bdev Device. 
 * @param lba Sector index. 
 * @param buf Source of 512 bytes. 
 * @return 0 on success, -1 on invalid input or I/O error. 
 * @copyright See LICENSE.md. 
 * @date 2026-11-25 */
int blkdev_write(struct block_device *bdev, uint64_t lba, const void *buf)
{
    if (bdev == 0 || buf == 0 || bdev->ops == 0 ||
        bdev->ops->write_sector == 0 || lba >= bdev->capacity_lba)
        return -1;

    return bdev->ops->write_sector(bdev, lba, buf);
}

/** Initialize the block-device registry. 
 * @return None. 
 * @copyright See LICENSE.md. 
 * @date 2026-11-25 */
void blkdev_init(void)
{
    device_count = 0;
    partition_count = 0;
    scanned_disk_count = 0;

    for (uint32_t i = 0; i < BLKDEV_MAX_DEVICES; ++i)
        devices[i] = 0;

    for (uint32_t i = 0; i < BLKDEV_MAX_DEVICES; ++i) {
        scanned_disks[i] = 0;
        partitions[i].parent = 0;
        partitions[i].start_lba = 0;
    }

    printf("BLKDEV: registry ready\n");
}

/** Placeholder for future interrupt-driven block I/O. 
 * @return None. 
 * @copyright See LICENSE.md. 
 * @date 2026-11-25 */
void enable_interupts_blockdev(void)
{
    printf("BLKDEV: interrupt-driven I/O is not enabled\n");
}
