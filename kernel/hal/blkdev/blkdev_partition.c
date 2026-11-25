#include "blkdev.h"
#include "blkdev_internal.h"
#include "blkdev_partition_internal.h"

#include <lib/std/stdio.h>

typedef struct {
    struct block_device bdev;
    struct block_device *parent;
    uint64_t start_lba;
    uint32_t number;
    enum blkdev_partition_table table;
    uint8_t in_use;
} partition_device_t;

static partition_device_t partitions[BLKDEV_MAX_DEVICES];
static uint32_t partition_count;
static struct block_device *scanned_disks[BLKDEV_MAX_DEVICES];
static uint32_t scanned_disk_count;

/** Reset private partition-table state during block-device initialization.
 * @return None.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
void blkdev_partition_init(void)
{
    partition_count = 0;
    scanned_disk_count = 0;
    blkdev_partition_table_init();

    for (uint32_t i = 0; i < BLKDEV_MAX_DEVICES; ++i) {
        scanned_disks[i] = 0;
        partitions[i].parent = 0;
        partitions[i].start_lba = 0;
        partitions[i].number = 0;
        partitions[i].table = BLKDEV_PARTITION_TABLE_NONE;
        partitions[i].in_use = 0;
    }
}

/** Decode a little-endian 32-bit integer from on-disk bytes.
 * @param p Pointer to at least four bytes.
 * @return Decoded integer.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
uint32_t read_le32(const uint8_t *p)
{
    uint32_t low = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
    uint32_t high = (uint32_t)p[2] | ((uint32_t)p[3] << 8);

    return low | (high << 16);
}

/** Decode a little-endian 64-bit integer from on-disk bytes.
 * @param p Pointer to at least eight bytes.
 * @return Decoded integer.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
uint64_t read_le64(const uint8_t *p)
{
    uint64_t low = read_le32(p);
    uint64_t high = read_le32(p + 4);

    return low | (high << 32);
}

/** Update an IEEE CRC-32 checksum.
 * @param crc Previous CRC state.
 * @param bytes Data to checksum.
 * @param length Number of bytes.
 * @return Updated CRC state.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
uint32_t crc32_update(uint32_t crc, const uint8_t *bytes, uint32_t length)
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
    if (bdev == 0) {
        return 0;
    }

    return bdev->capacity_lba;
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
                              uint64_t count, uint32_t number,
                              enum blkdev_partition_table table)
{
    partition_device_t *partition;
    uint32_t slot;
    uint32_t n = 0;

    if (disk == 0 || count == 0 || start == 0 || start >= disk->capacity_lba ||
        count > disk->capacity_lba - start ||
        partition_count >= BLKDEV_MAX_DEVICES ||
        blkdev_registry_count() >= BLKDEV_MAX_DEVICES)
        return -1;

    for (slot = 0; slot < BLKDEV_MAX_DEVICES; ++slot)
        if (!partitions[slot].in_use) break;
    if (slot == BLKDEV_MAX_DEVICES) return -1;

    partition = &partitions[slot];
    partition->parent = disk;
    partition->start_lba = start;
    partition->number = number;
    partition->table = table;
    partition->in_use = 1;

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

        if (n != 0 && disk->name[n - 1] >= '0' && disk->name[n - 1] <= '9')
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
    {
        partition->in_use = 0;
        return -1;
    }

    ++partition_count;
    return 0;
}

/** Parse standard primary MBR entries.
 * @param disk Disk whose sector zero has been read.
 * @param sector Sector-zero contents.
 * @return Number of partitions registered, or -1 for an invalid table.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_scan_mbr(struct block_device *disk, const uint8_t *sector)
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

        if (register_partition(disk, start, count, i + 1,
                               BLKDEV_PARTITION_TABLE_MBR) == 0)
            ++found;
    }
    return found;
}

/** Validate and scan a primary GPT partition table.
 * @param disk Whole-disk device.
 * @return Number of partitions registered, or -1 for an invalid GPT.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_scan_gpt(struct block_device *disk)
{
    uint8_t sector[BLKDEV_SECTOR_SIZE];
    uint8_t header[BLKDEV_SECTOR_SIZE];

    uint32_t header_size, entry_count, entry_size, expected_entries_crc;
    uint64_t entries_lba, entries_bytes, first_usable, last_usable;
    uint32_t crc;

    int found = 0;

    if (disk == 0 || disk->capacity_lba < GPT_TABLE_SECTORS + 36 ||
        blkdev_read(disk, 1, sector) != 0 || sector[0] != 'E' ||
        sector[1] != 'F' || sector[2] != 'I' || sector[3] != ' ' ||
        sector[4] != 'P' || sector[5] != 'A' || sector[6] != 'R' ||
        sector[7] != 'T')
        return -1;

    header_size = read_le32(sector + 12);

    if (header_size < 92 || header_size > BLKDEV_SECTOR_SIZE ||
        read_le64(sector + 24) != 1 ||
        read_le64(sector + 32) != disk->capacity_lba - 1 ||
        read_le64(sector + 40) < 34 ||
        read_le64(sector + 48) < read_le64(sector + 40) ||
        read_le64(sector + 48) > disk->capacity_lba - GPT_TABLE_SECTORS - 2)
        return -1;
    last_usable = read_le64(sector + 48);
    first_usable = read_le64(sector + 40);

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
        disk->capacity_lba - entries_lba || entries_lba != 2 ||
        entries_lba + (entries_bytes + BLKDEV_SECTOR_SIZE - 1) /
            BLKDEV_SECTOR_SIZE > read_le64(sector + 40))
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

        if (last < first || first < first_usable || last > last_usable)
            continue;

        if (register_partition(disk, first, last - first + 1, i + 1,
                               BLKDEV_PARTITION_TABLE_GPT) == 0)
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
    uint32_t disks_to_scan = blkdev_registry_count();
    int total_found = 0;

    for (uint32_t i = 0; i < disks_to_scan; ++i) {
        struct block_device *disk = blkdev_get(i);

        uint8_t sector[BLKDEV_SECTOR_SIZE];
        int already_scanned = 0;

        for (uint32_t j = 0; j < scanned_disk_count; ++j)
            if (scanned_disks[j] == disk) { already_scanned = 1; break; }

        if (already_scanned || disk == 0 || blkdev_partition_is_device(disk) ||
            disk->capacity_lba < 2 ||
            blkdev_read(disk, 0, sector) != 0)
            continue;

        int result;

        if (sector[510] == 0x55 && sector[511] == 0xAA &&
            sector[450] == 0xEE)
            result = blkdev_partition_scan_gpt(disk);
        else
            result = blkdev_partition_scan_mbr(disk, sector);

        if (scanned_disk_count < BLKDEV_MAX_DEVICES)
            scanned_disks[scanned_disk_count++] = disk;
        if (result > 0) total_found += result;

        printf("BLKDEV: scanned %s, %u partitions\n", disk->name,
               result > 0 ? (uint32_t)result : 0U);
    }

    return total_found;
}

/** Check whether a block-device descriptor represents a registered partition.
 * @param bdev Candidate device.
 * @return Nonzero when it is one of the internal partition devices.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_is_device(struct block_device *bdev)
{
    for (uint32_t i = 0; i < BLKDEV_MAX_DEVICES; ++i) {
        if (partitions[i].in_use && &partitions[i].bdev == bdev)
            return 1;
    }

    return 0;
}

/** Validate a descriptor as a registered whole disk.
 * @param disk Candidate descriptor.
 * @return Nonzero when it is a registered whole disk.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_valid_disk(struct block_device *disk)
{
    if (disk == 0 || blkdev_partition_is_device(disk)) return 0;
    uint32_t count = blkdev_registry_count();

    for (uint32_t i = 0; i < count; ++i) {
        if (blkdev_get(i) == disk) {
            return 1;
        }
    }
    return 0;
}

/** Return the number of physical disks, excluding partition devices.
 * @return Whole-disk count.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
uint32_t blkdev_disk_count(void)
{
    uint32_t count = 0;
    uint32_t registry_count = blkdev_registry_count();

    for (uint32_t i = 0; i < registry_count; ++i) {
        if (!blkdev_partition_is_device(blkdev_get(i))) {
            ++count;
        }
    }
    return count;
}

/** Return a physical disk by its stable order among whole disks.
 * @param index Zero-based whole-disk number.
 * @return Disk descriptor or NULL when out of range.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
struct block_device *blkdev_disk_get(uint32_t index)
{
    uint32_t registry_count = blkdev_registry_count();

    for (uint32_t i = 0; i < registry_count; ++i) {
        struct block_device *device = blkdev_get(i);

        if (blkdev_partition_is_device(device)) {
            continue;
        }

        if (index == 0) {
            return device;
        }
        --index;
    }
    return 0;
}

/** Refresh registered partition devices for a disk after a table change.
 * @param disk Whole-disk descriptor.
 * @return Number of partitions reloaded.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_refresh_disk(struct block_device *disk)
{
    for (uint32_t i = 0; i < BLKDEV_MAX_DEVICES; ++i) {
        if (!partitions[i].in_use || partitions[i].parent != disk) continue;
        struct block_device *old = &partitions[i].bdev;
        blkdev_unregister(old);
        partitions[i].in_use = 0;
        partitions[i].parent = 0;
        if (partition_count != 0) --partition_count;
    }

    uint8_t sector[BLKDEV_SECTOR_SIZE];
    if (blkdev_read(disk, 0, sector) != 0) return -1;

    if (sector[510] != 0x55 || sector[511] != 0xAA) return 0;
    if (sector[450] == 0xEE) return blkdev_partition_scan_gpt(disk);
    return blkdev_partition_scan_mbr(disk, sector);
}

