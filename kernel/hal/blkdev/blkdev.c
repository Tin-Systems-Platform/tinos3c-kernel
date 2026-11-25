#include "blkdev.h"

#include <lib/std/stdio.h>

static struct block_device *devices[BLKDEV_MAX_DEVICES];
static uint32_t device_count;

#define PARTITION_MAX_ENTRIES 128U

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

#define GPT_ENTRY_COUNT 128U
#define GPT_ENTRY_SIZE 128U
#define GPT_TABLE_BYTES (GPT_ENTRY_COUNT * GPT_ENTRY_SIZE)
#define GPT_TABLE_SECTORS (GPT_TABLE_BYTES / BLKDEV_SECTOR_SIZE)
#define PARTITION_ALIGNMENT_LBA 2048ULL

static uint8_t gpt_entries[GPT_TABLE_BYTES];
static struct block_device *gpt_cache_disk;
static uint32_t gpt_cache_entries_crc;
static uint64_t gpt_cache_first_usable;
static uint64_t gpt_cache_last_usable;
static uint8_t gpt_cache_valid;

static int is_partition_device(struct block_device *bdev);

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

/** Store a little-endian 32-bit integer in an on-disk structure.
 * @param p Destination with at least four bytes.
 * @param value Integer to store.
 * @return None.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

/** Store a little-endian 64-bit integer in an on-disk structure.
 * @param p Destination with at least eight bytes.
 * @param value Integer to store.
 * @return None.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static void write_le64(uint8_t *p, uint64_t value)
{
    write_le32(p, (uint32_t)value);
    write_le32(p + 4, (uint32_t)(value >> 32));
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
                              uint64_t count, uint32_t number,
                              enum blkdev_partition_table table)
{
    partition_device_t *partition;
    uint32_t slot;
    uint32_t n = 0;

    if (disk == 0 || count == 0 || start == 0 || start >= disk->capacity_lba ||
        count > disk->capacity_lba - start ||
        partition_count >= BLKDEV_MAX_DEVICES || device_count >= BLKDEV_MAX_DEVICES)
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
static int scan_gpt(struct block_device *disk)
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
    uint32_t disks_to_scan = device_count;
    int total_found = 0;

    for (uint32_t i = 0; i < disks_to_scan; ++i) {
        struct block_device *disk = devices[i];

        uint8_t sector[BLKDEV_SECTOR_SIZE];
        int already_scanned = 0;

        for (uint32_t j = 0; j < scanned_disk_count; ++j)
            if (scanned_disks[j] == disk) { already_scanned = 1; break; }
        
        if (already_scanned || disk == 0 || is_partition_device(disk) ||
            disk->capacity_lba < 2 ||
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

    if (gpt_cache_valid && bdev == gpt_cache_disk &&
        (lba <= GPT_TABLE_SECTORS + 1 ||
         lba >= bdev->capacity_lba - GPT_TABLE_SECTORS - 1))
        gpt_cache_valid = 0;

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
    gpt_cache_valid = 0;

    for (uint32_t i = 0; i < BLKDEV_MAX_DEVICES; ++i)
        devices[i] = 0;

    for (uint32_t i = 0; i < BLKDEV_MAX_DEVICES; ++i) {
        scanned_disks[i] = 0;
        partitions[i].parent = 0;
        partitions[i].start_lba = 0;
        partitions[i].number = 0;
        partitions[i].table = BLKDEV_PARTITION_TABLE_NONE;
        partitions[i].in_use = 0;
    }

    printf("BLKDEV: registry ready\n");
}

/** Check whether a block-device descriptor represents a registered partition.
 * @param bdev Candidate device.
 * @return Nonzero when it is one of the internal partition devices.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int is_partition_device(struct block_device *bdev)
{
    for (uint32_t i = 0; i < BLKDEV_MAX_DEVICES; ++i)
        if (partitions[i].in_use && &partitions[i].bdev == bdev)
            return 1;
    return 0;
}

/** Validate a descriptor as a registered whole disk.
 * @param disk Candidate descriptor.
 * @return Nonzero when it is a registered whole disk.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int valid_whole_disk(struct block_device *disk)
{
    if (disk == 0 || is_partition_device(disk)) return 0;
    for (uint32_t i = 0; i < device_count; ++i)
        if (devices[i] == disk) return 1;
    return 0;
}

/** Return the number of physical disks, excluding partition devices.
 * @return Whole-disk count.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
uint32_t blkdev_disk_count(void)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < device_count; ++i)
        if (!is_partition_device(devices[i])) ++count;
    return count;
}

/** Return a physical disk by its stable order among whole disks.
 * @param index Zero-based whole-disk number.
 * @return Disk descriptor or NULL when out of range.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
struct block_device *blkdev_disk_get(uint32_t index)
{
    for (uint32_t i = 0; i < device_count; ++i) {
        if (is_partition_device(devices[i])) continue;
        if (index == 0) return devices[i];
        --index;
    }
    return 0;
}

/** Read and validate the kernel's supported canonical primary GPT layout.
 * @param disk Whole-disk descriptor.
 * @param header Output primary header sector.
 * @return 0 when the GPT uses 128 entries of 128 bytes; otherwise -1.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int gpt_load(struct block_device *disk, uint8_t *header)
{
    uint32_t crc;
    if (!valid_whole_disk(disk) ||
        disk->capacity_lba < GPT_TABLE_SECTORS + 35 || header == 0)
        return -1;
    if (blkdev_read(disk, 1, header) != 0 || header[0] != 'E' ||
        header[1] != 'F' || header[2] != 'I' || header[3] != ' ' ||
        header[4] != 'P' || header[5] != 'A' || header[6] != 'R' ||
        header[7] != 'T' || read_le32(header + 12) != 92 ||
        read_le64(header + 24) != 1 ||
        read_le64(header + 32) != disk->capacity_lba - 1 ||
        read_le64(header + 72) != 2 ||
        read_le32(header + 80) != GPT_ENTRY_COUNT ||
        read_le32(header + 84) != GPT_ENTRY_SIZE ||
        read_le64(header + 40) < 34 ||
        read_le64(header + 48) < read_le64(header + 40) ||
        read_le64(header + 48) >= disk->capacity_lba - GPT_TABLE_SECTORS - 1)
        return -1;

    uint8_t header_copy[BLKDEV_SECTOR_SIZE];
    for (uint32_t i = 0; i < BLKDEV_SECTOR_SIZE; ++i) header_copy[i] = header[i];
    uint32_t expected_header_crc = read_le32(header_copy + 16);
    header_copy[16] = header_copy[17] = header_copy[18] = header_copy[19] = 0;
    crc = crc32_update(0xFFFFFFFFU, header_copy, 92) ^ 0xFFFFFFFFU;
    if (crc != expected_header_crc) return -1;

    uint32_t expected_entries_crc = read_le32(header + 88);
    gpt_cache_first_usable = read_le64(header + 40);
    gpt_cache_last_usable = read_le64(header + 48);
    if (gpt_cache_valid && gpt_cache_disk == disk &&
        gpt_cache_entries_crc == expected_entries_crc)
        return 0;

    for (uint32_t i = 0; i < GPT_TABLE_SECTORS; ++i)
        if (blkdev_read(disk, 2 + i, gpt_entries + i * BLKDEV_SECTOR_SIZE) != 0)
            return -1;
    crc = crc32_update(0xFFFFFFFFU, gpt_entries, GPT_TABLE_BYTES) ^ 0xFFFFFFFFU;
    if (crc != expected_entries_crc) {
        gpt_cache_valid = 0;
        return -1;
    }
    gpt_cache_disk = disk;
    gpt_cache_entries_crc = expected_entries_crc;
    gpt_cache_first_usable = read_le64(header + 40);
    gpt_cache_last_usable = read_le64(header + 48);
    gpt_cache_valid = 1;
    return 0;
}

/** Build a GPT header sector and its checksum.
 * @param header Sector to update.
 * @param current Current header LBA.
 * @param backup Other header LBA.
 * @param entries_lba Partition-array LBA.
 * @param entries_crc CRC of the complete partition array.
 * @return None.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static void gpt_build_header(uint8_t *header, uint64_t current,
                             uint64_t backup, uint64_t entries_lba,
                             uint32_t entries_crc)
{
    write_le64(header + 24, current);
    write_le64(header + 32, backup);
    write_le64(header + 72, entries_lba);
    write_le32(header + 88, entries_crc);
    header[16] = header[17] = header[18] = header[19] = 0;
    write_le32(header + 16,
        crc32_update(0xFFFFFFFFU, header, 92) ^ 0xFFFFFFFFU);
}

/** Persist updated partition entries and both GPT headers.
 * @param disk Whole-disk descriptor.
 * @param header Current primary header.
 * @return 0 on success, -1 on an I/O failure.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int gpt_store(struct block_device *disk, uint8_t *header)
{
    uint8_t backup_header[BLKDEV_SECTOR_SIZE];
    uint32_t entries_crc = crc32_update(0xFFFFFFFFU, gpt_entries,
                                       GPT_TABLE_BYTES) ^ 0xFFFFFFFFU;
    uint64_t backup_entries_lba = disk->capacity_lba - GPT_TABLE_SECTORS - 1;

    for (uint32_t i = 0; i < BLKDEV_SECTOR_SIZE; ++i)
        backup_header[i] = header[i];
    gpt_build_header(header, 1, disk->capacity_lba - 1, 2, entries_crc);
    gpt_build_header(backup_header, disk->capacity_lba - 1, 1,
                     backup_entries_lba, entries_crc);

    gpt_cache_valid = 0;
    for (uint32_t i = 0; i < GPT_TABLE_SECTORS; ++i)
        if (blkdev_write(disk, backup_entries_lba + i,
                         gpt_entries + i * BLKDEV_SECTOR_SIZE) != 0)
            return -1;
    if (blkdev_write(disk, disk->capacity_lba - 1, backup_header) != 0)
        return -1;
    for (uint32_t i = 0; i < GPT_TABLE_SECTORS; ++i)
        if (blkdev_write(disk, 2 + i,
                         gpt_entries + i * BLKDEV_SECTOR_SIZE) != 0)
            return -1;
    if (blkdev_write(disk, 1, header) != 0) return -1;
    gpt_cache_disk = disk;
    gpt_cache_entries_crc = entries_crc;
    gpt_cache_first_usable = read_le64(header + 40);
    gpt_cache_last_usable = read_le64(header + 48);
    gpt_cache_valid = 1;
    return 0;
}

/** Identify an on-disk partition table and validate GPT metadata when present.
 * @param disk Whole-disk descriptor.
 * @return Detected table type, or NONE when no supported valid table exists.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
enum blkdev_partition_table blkdev_disk_table(struct block_device *disk)
{
    uint8_t sector[BLKDEV_SECTOR_SIZE];
    uint8_t header[BLKDEV_SECTOR_SIZE];
    if (valid_whole_disk(disk) && gpt_cache_valid && gpt_cache_disk == disk)
        return BLKDEV_PARTITION_TABLE_GPT;
    if (!valid_whole_disk(disk) || blkdev_read(disk, 0, sector) != 0 ||
        sector[510] != 0x55 || sector[511] != 0xAA)
        return BLKDEV_PARTITION_TABLE_NONE;
    if (sector[450] == 0xEE)
        return gpt_load(disk, header) == 0 ? BLKDEV_PARTITION_TABLE_GPT :
                                             BLKDEV_PARTITION_TABLE_NONE;
    return BLKDEV_PARTITION_TABLE_MBR;
}

/** Read one partition's metadata from the active MBR or GPT table.
 * @param disk Whole-disk descriptor.
 * @param number One-based table entry number.
 * @param info Output metadata.
 * @return 0 when present, -1 otherwise.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_get_info(struct block_device *disk, uint32_t number,
                              struct blkdev_partition_info *info)
{
    uint8_t sector[BLKDEV_SECTOR_SIZE];
    uint8_t header[BLKDEV_SECTOR_SIZE];
    enum blkdev_partition_table table = blkdev_disk_table(disk);
    uint64_t start, count;
    if (info == 0 || number == 0) return -1;
    if (table == BLKDEV_PARTITION_TABLE_MBR) {
        if (number > 4 || blkdev_read(disk, 0, sector) != 0) return -1;
        const uint8_t *entry = sector + 446 + (number - 1) * 16;
        start = read_le32(entry + 8);
        count = read_le32(entry + 12);
        if (entry[4] == 0 || count == 0) return -1;
    } else if (table == BLKDEV_PARTITION_TABLE_GPT) {
        if (number > GPT_ENTRY_COUNT ||
            ((!gpt_cache_valid || gpt_cache_disk != disk) &&
             gpt_load(disk, header) != 0)) return -1;
        const uint8_t *entry = gpt_entries + (number - 1) * GPT_ENTRY_SIZE;
        int empty = 1;
        for (uint32_t i = 0; i < 16; ++i) if (entry[i] != 0) { empty = 0; break; }
        if (empty) return -1;
        start = read_le64(entry + 32);
        uint64_t last = read_le64(entry + 40);
        if (last < start || start < gpt_cache_first_usable ||
            last > gpt_cache_last_usable) return -1;
        count = last - start + 1;
    } else {
        return -1;
    }
    if (start >= disk->capacity_lba || count > disk->capacity_lba - start)
        return -1;
    uint32_t n = 0;
    while (n < sizeof(info->name) - 1 && disk->name[n] != 0) {
        info->name[n] = disk->name[n];
        ++n;
    }
    if (n != 0 && disk->name[n - 1] >= '0' && disk->name[n - 1] <= '9' &&
        n < sizeof(info->name) - 1)
        info->name[n++] = 'p';
    uint32_t digits[10], digit_count = 0, value = number;
    do { digits[digit_count++] = value % 10; value /= 10; }
    while (value != 0 && digit_count < 10);
    while (digit_count != 0 && n < sizeof(info->name) - 1)
        info->name[n++] = (char)('0' + digits[--digit_count]);
    info->name[n] = 0;
    info->start_lba = start;
    info->sector_count = count;
    info->number = number;
    return 0;
}

/** Refresh registered partition devices for a disk after a table change.
 * @param disk Whole-disk descriptor.
 * @return Number of partitions reloaded.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int refresh_disk_partitions(struct block_device *disk)
{
    for (uint32_t i = 0; i < BLKDEV_MAX_DEVICES; ++i) {
        if (!partitions[i].in_use || partitions[i].parent != disk) continue;
        struct block_device *old = &partitions[i].bdev;
        for (uint32_t j = 0; j < device_count; ++j) {
            if (devices[j] != old) continue;
            for (uint32_t k = j + 1; k < device_count; ++k)
                devices[k - 1] = devices[k];
            devices[--device_count] = 0;
            break;
        }
        partitions[i].in_use = 0;
        partitions[i].parent = 0;
        if (partition_count != 0) --partition_count;
    }
    uint8_t sector[BLKDEV_SECTOR_SIZE];
    if (blkdev_read(disk, 0, sector) != 0) return -1;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return 0;
    if (sector[450] == 0xEE) return scan_gpt(disk);
    return scan_mbr(disk, sector);
}

/** Check whether a sector consists entirely of zero bytes.
 * @param sector Sector contents.
 * @return Nonzero when all bytes are zero.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int sector_is_zero(const uint8_t *sector)
{
    for (uint32_t i = 0; i < BLKDEV_SECTOR_SIZE; ++i)
        if (sector[i] != 0) return 0;
    return 1;
}

/** Initialize a disk using an empty MBR or canonical 128-entry GPT.
 * The caller must choose this explicitly because it writes disk metadata.
 * @param disk Whole-disk descriptor.
 * @param table MBR or GPT.
 * @return 0 on success, -1 when not empty, invalid, or an I/O write fails.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_disk_initialize(struct block_device *disk,
                           enum blkdev_partition_table table)
{
    uint8_t mbr[BLKDEV_SECTOR_SIZE];
    uint8_t sector[BLKDEV_SECTOR_SIZE];
    if (!valid_whole_disk(disk) ||
        (table != BLKDEV_PARTITION_TABLE_MBR &&
         table != BLKDEV_PARTITION_TABLE_GPT) ||
        disk->capacity_lba < (table == BLKDEV_PARTITION_TABLE_GPT ? 68 : 2) ||
        blkdev_read(disk, 0, mbr) != 0)
        return -1;
    if ((mbr[510] == 0x55 && mbr[511] == 0xAA) || mbr[450] == 0xEE)
        return -1;
    for (uint32_t i = 446; i < 510; ++i)
        if (mbr[i] != 0) return -1;
    if (blkdev_read(disk, 1, sector) != 0) return -1;
    if (!sector_is_zero(sector)) return -1;

    uint64_t last_sector = disk->capacity_lba - 1;
    if (blkdev_read(disk, last_sector, sector) != 0 || !sector_is_zero(sector))
        return -1;

    if (table == BLKDEV_PARTITION_TABLE_GPT) {
        uint64_t backup_entries = disk->capacity_lba - GPT_TABLE_SECTORS - 1;
        for (uint32_t i = 2; i < 2 + GPT_TABLE_SECTORS; ++i) {
            if (blkdev_read(disk, i, sector) != 0 || !sector_is_zero(sector))
                return -1;
            if (blkdev_read(disk, backup_entries + i - 2, sector) != 0 ||
                !sector_is_zero(sector)) return -1;
        }
    }

    if (table == BLKDEV_PARTITION_TABLE_MBR) {
        mbr[510] = 0x55;
        mbr[511] = 0xAA;
        if (blkdev_write(disk, 0, mbr) != 0) return -1;
    } else {
        uint64_t backup_entries = disk->capacity_lba - GPT_TABLE_SECTORS - 1;
        uint8_t header[BLKDEV_SECTOR_SIZE];
        for (uint32_t i = 0; i < GPT_TABLE_BYTES; ++i) gpt_entries[i] = 0;
        for (uint32_t i = 0; i < GPT_TABLE_SECTORS; ++i) {
            if (blkdev_write(disk, 2 + i,
                             gpt_entries + i * BLKDEV_SECTOR_SIZE) != 0 ||
                blkdev_write(disk, backup_entries + i,
                             gpt_entries + i * BLKDEV_SECTOR_SIZE) != 0)
                return -1;
        }
        for (uint32_t i = 0; i < BLKDEV_SECTOR_SIZE; ++i) header[i] = 0;
        header[0] = 'E'; header[1] = 'F'; header[2] = 'I'; header[3] = ' ';
        header[4] = 'P'; header[5] = 'A'; header[6] = 'R'; header[7] = 'T';
        write_le32(header + 8, 0x00010000U);
        write_le32(header + 12, 92);
        write_le64(header + 40, 34);
        write_le64(header + 48, backup_entries - 1);
        write_le32(header + 80, GPT_ENTRY_COUNT);
        write_le32(header + 84, GPT_ENTRY_SIZE);
        /* Stable device-based GUID seed; the kernel has no entropy source yet. */
        uint64_t id = disk->capacity_lba ^ (uint64_t)(uintptr_t)disk ^
                      0x54494E4F53475054ULL;
        for (uint32_t i = 0; i < sizeof(disk->name) && disk->name[i] != 0; ++i)
            id = (id ^ (uint8_t)disk->name[i]) * 1099511628211ULL;
        if (id == 0) id = 1;
        uint64_t id_high = (id << 17) ^ (id >> 13) ^
                           0xA5A5A5A55A5A5A5AULL;
        if (id_high == 0 || id_high == id) id_high ^= 0x9E3779B97F4A7C15ULL;
        write_le64(header + 56, id);
        write_le64(header + 64, id_high);
        uint32_t empty_crc = crc32_update(0xFFFFFFFFU, gpt_entries,
                                         GPT_TABLE_BYTES) ^ 0xFFFFFFFFU;
        uint8_t backup_header[BLKDEV_SECTOR_SIZE];
        for (uint32_t i = 0; i < BLKDEV_SECTOR_SIZE; ++i)
            backup_header[i] = header[i];
        gpt_build_header(header, 1, disk->capacity_lba - 1, 2, empty_crc);
        gpt_build_header(backup_header, disk->capacity_lba - 1, 1,
                         backup_entries, empty_crc);
        if (blkdev_write(disk, disk->capacity_lba - 1, backup_header) != 0 ||
            blkdev_write(disk, 1, header) != 0)
            return -1;

        for (uint32_t i = 446; i < 510; ++i) mbr[i] = 0;
        mbr[446 + 4] = 0xEE;
        write_le32(mbr + 446 + 8, 1);
        uint64_t protective_count = disk->capacity_lba - 1;
        if (protective_count > 0xFFFFFFFFULL) protective_count = 0xFFFFFFFFULL;
        write_le32(mbr + 446 + 12, (uint32_t)protective_count);
        mbr[510] = 0x55; mbr[511] = 0xAA;
        if (blkdev_write(disk, 0, mbr) != 0) return -1;
    }
    refresh_disk_partitions(disk);
    return 0;
}

/** Read existing partition regions into bounded local arrays.
 * @param disk Whole-disk descriptor.
 * @param table Active table type.
 * @param starts Output start-LBA array.
 * @param lengths Output sector-count array.
 * @param used Output used-slot flags.
 * @param slots Output number of entries.
 * @return 0 on success, -1 on an invalid table or read error.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int read_regions(struct block_device *disk,
                        enum blkdev_partition_table table,
                        uint64_t *starts, uint64_t *lengths,
                        uint8_t *used, uint32_t *slots)
{
    uint8_t sector[BLKDEV_SECTOR_SIZE], header[BLKDEV_SECTOR_SIZE];
    *slots = table == BLKDEV_PARTITION_TABLE_MBR ? 4 : GPT_ENTRY_COUNT;
    for (uint32_t i = 0; i < *slots; ++i) { starts[i] = 0; lengths[i] = 0; used[i] = 0; }
    if (table == BLKDEV_PARTITION_TABLE_MBR) {
        if (blkdev_read(disk, 0, sector) != 0 || sector[510] != 0x55 || sector[511] != 0xAA)
            return -1;
        for (uint32_t i = 0; i < 4; ++i) {
            const uint8_t *entry = sector + 446 + i * 16;
            uint64_t start = read_le32(entry + 8), length = read_le32(entry + 12);
            if (entry[4] == 0 || length == 0) continue;
            if (entry[4] == 0x05 || entry[4] == 0x0F || entry[4] == 0x85 ||
                start == 0 || start >= disk->capacity_lba ||
                length > disk->capacity_lba - start)
                return -1;
            used[i] = 1; starts[i] = start; lengths[i] = length;
        }
    } else if (table != BLKDEV_PARTITION_TABLE_GPT || gpt_load(disk, header) != 0) {
        return -1;
    } else {
        for (uint32_t i = 0; i < GPT_ENTRY_COUNT; ++i) {
            const uint8_t *entry = gpt_entries + i * GPT_ENTRY_SIZE;
            int empty = 1;
            for (uint32_t j = 0; j < 16; ++j) if (entry[j] != 0) { empty = 0; break; }
            if (empty) continue;
            uint64_t start = read_le64(entry + 32), last = read_le64(entry + 40);
            if (start < gpt_cache_first_usable || last < start ||
                last > gpt_cache_last_usable) return -1;
            used[i] = 1; starts[i] = start; lengths[i] = last - start + 1;
        }
    }

    for (uint32_t i = 0; i < *slots; ++i) {
        if (!used[i]) continue;
        for (uint32_t j = i + 1; j < *slots; ++j) {
            if (!used[j]) continue;
            if (starts[i] < starts[j] + lengths[j] &&
                starts[j] < starts[i] + lengths[i]) return -1;
        }
    }
    return 0;
}

/** Find a free aligned interval or validate a requested interval.
 * @param disk Whole-disk descriptor.
 * @param table Active table type.
 * @param requested_start Requested LBA, or zero for first fit.
 * @param length Requested sector count.
 * @param excluded_slot Slot ignored during resizing, or UINT32_MAX.
 * @param starts Existing start-LBA array.
 * @param lengths Existing lengths array.
 * @param used Existing slot-use flags.
 * @param slots Number of slots.
 * @param result_start Output selected start LBA.
 * @return 0 when the region is free and valid; otherwise -1.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int choose_region(struct block_device *disk,
                         enum blkdev_partition_table table,
                         uint64_t requested_start, uint64_t length,
                         uint32_t excluded_slot, const uint64_t *starts,
                         const uint64_t *lengths, const uint8_t *used,
                         uint32_t slots, uint64_t *result_start)
{
    uint64_t first_usable = table == BLKDEV_PARTITION_TABLE_GPT ?
        gpt_cache_first_usable : 1;
    uint64_t end_usable = table == BLKDEV_PARTITION_TABLE_GPT ?
        gpt_cache_last_usable + 1 : disk->capacity_lba;
    if (table == BLKDEV_PARTITION_TABLE_MBR && end_usable > 0x100000000ULL)
        end_usable = 0x100000000ULL;
    if (length == 0 || length > end_usable - first_usable) return -1;

    if (requested_start != 0) {
        if (requested_start < first_usable ||
            (excluded_slot == ~0U &&
             requested_start % PARTITION_ALIGNMENT_LBA != 0) ||
            requested_start > end_usable || length > end_usable - requested_start)
            return -1;
        for (uint32_t i = 0; i < slots; ++i) {
            if (!used[i] || i == excluded_slot) continue;
            if (requested_start < starts[i] + lengths[i] &&
                starts[i] < requested_start + length) return -1;
        }
        *result_start = requested_start;
        return 0;
    }

    uint64_t candidate = (first_usable + PARTITION_ALIGNMENT_LBA - 1) /
                         PARTITION_ALIGNMENT_LBA * PARTITION_ALIGNMENT_LBA;
    for (uint32_t pass = 0; pass <= slots; ++pass) {
        uint64_t nearest_start = end_usable;
        uint64_t nearest_end = end_usable;
        int found = 0;
        for (uint32_t i = 0; i < slots; ++i) {
            if (!used[i] || i == excluded_slot || starts[i] < candidate) continue;
            if (!found || starts[i] < nearest_start) {
                nearest_start = starts[i];
                nearest_end = starts[i] + lengths[i];
                found = 1;
            }
        }
        uint64_t gap_end = !found ? end_usable : nearest_start;
        if (candidate <= gap_end && length <= gap_end - candidate) {
            *result_start = candidate;
            return 0;
        }
        if (!found || nearest_end > end_usable) return -1;
        candidate = (nearest_end + PARTITION_ALIGNMENT_LBA - 1) /
                    PARTITION_ALIGNMENT_LBA * PARTITION_ALIGNMENT_LBA;
        if (candidate >= end_usable) return -1;
    }
    return -1;
}

/** Recompute checksums and write a GPT table update to both copies.
 * @param disk Whole-disk descriptor.
 * @param header Primary header loaded by gpt_load.
 * @return 0 on success, -1 on I/O failure.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int persist_gpt_change(struct block_device *disk, uint8_t *header)
{
    return gpt_store(disk, header);
}

/** Persist a newly created partition entry and refresh registered partitions.
 * @param disk Whole-disk descriptor.
 * @param table Active table type.
 * @param slot Zero-based table slot.
 * @param start Start LBA.
 * @param length Sector count.
 * @return 0 on success, -1 on read/write failure.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_create(struct block_device *disk, uint64_t size_lba,
                            uint64_t start_lba, uint32_t *number)
{
    enum blkdev_partition_table table = blkdev_disk_table(disk);
    uint64_t starts[GPT_ENTRY_COUNT], lengths[GPT_ENTRY_COUNT], start;
    uint8_t used[GPT_ENTRY_COUNT], sector[BLKDEV_SECTOR_SIZE];
    uint32_t slots, slot;
    if (!valid_whole_disk(disk) || number == 0 ||
        (table != BLKDEV_PARTITION_TABLE_MBR && table != BLKDEV_PARTITION_TABLE_GPT) ||
        read_regions(disk, table, starts, lengths, used, &slots) != 0)
        return -1;
    for (slot = 0; slot < slots && used[slot]; ++slot) { }
    if (slot == slots || choose_region(disk, table, start_lba, size_lba,
        ~0U, starts, lengths, used, slots, &start) != 0)
        return -1;

    if (table == BLKDEV_PARTITION_TABLE_MBR) {
        if (blkdev_read(disk, 0, sector) != 0 || start > 0xFFFFFFFFULL ||
            size_lba > 0xFFFFFFFFULL) return -1;
        uint8_t *entry = sector + 446 + slot * 16;
        for (uint32_t i = 0; i < 16; ++i) entry[i] = 0;
        entry[1] = 0xFE; entry[2] = 0xFF; entry[3] = 0xFF;
        entry[4] = 0x83;
        entry[5] = 0xFE; entry[6] = 0xFF; entry[7] = 0xFF;
        write_le32(entry + 8, (uint32_t)start);
        write_le32(entry + 12, (uint32_t)size_lba);
        if (blkdev_write(disk, 0, sector) != 0) return -1;
    } else {
        uint8_t header[BLKDEV_SECTOR_SIZE];
        if (gpt_load(disk, header) != 0) return -1;
        uint8_t *entry = gpt_entries + slot * GPT_ENTRY_SIZE;
        static const uint8_t linux_type_guid[16] = {
            0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
            0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
        };
        for (uint32_t i = 0; i < GPT_ENTRY_SIZE; ++i) entry[i] = 0;
        for (uint32_t i = 0; i < 16; ++i) entry[i] = linux_type_guid[i];
        uint64_t guid_a = start ^ (size_lba << 1) ^ disk->capacity_lba;
        uint64_t guid_b = size_lba ^ (start << 7) ^
                          (uint64_t)(uintptr_t)disk ^ 0x54494E4F53475054ULL;
        if ((guid_a | guid_b) == 0) guid_b = 1;
        write_le64(entry + 16, guid_a);
        write_le64(entry + 24, guid_b);
        write_le64(entry + 32, start);
        write_le64(entry + 40, start + size_lba - 1);
        if (persist_gpt_change(disk, header) != 0) return -1;
    }
    *number = slot + 1;
    refresh_disk_partitions(disk);
    return 0;
}

/** Resize an existing MBR/GPT partition without moving its first sector.
 * @param disk Whole-disk descriptor.
 * @param number One-based partition entry number.
 * @param new_size_lba New size in sectors.
 * @return 0 on success, -1 if invalid, overlapping, or I/O fails.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_resize(struct block_device *disk, uint32_t number,
                            uint64_t new_size_lba)
{
    enum blkdev_partition_table table = blkdev_disk_table(disk);
    uint64_t starts[GPT_ENTRY_COUNT], lengths[GPT_ENTRY_COUNT], ignored;
    uint8_t used[GPT_ENTRY_COUNT], sector[BLKDEV_SECTOR_SIZE];
    uint32_t slots;
    if (!valid_whole_disk(disk) || number == 0 ||
        read_regions(disk, table, starts, lengths, used, &slots) != 0 ||
        number > slots || !used[number - 1] || new_size_lba == 0 ||
        choose_region(disk, table, starts[number - 1], new_size_lba,
                     number - 1, starts, lengths, used, slots, &ignored) != 0)
        return -1;
    if (table == BLKDEV_PARTITION_TABLE_MBR) {
        if (new_size_lba > 0xFFFFFFFFULL || blkdev_read(disk, 0, sector) != 0)
            return -1;
        write_le32(sector + 446 + (number - 1) * 16 + 12,
                   (uint32_t)new_size_lba);
        if (blkdev_write(disk, 0, sector) != 0) return -1;
    } else if (table == BLKDEV_PARTITION_TABLE_GPT) {
        uint8_t header[BLKDEV_SECTOR_SIZE];
        if (gpt_load(disk, header) != 0) return -1;
        write_le64(gpt_entries + (number - 1) * GPT_ENTRY_SIZE + 40,
                   starts[number - 1] + new_size_lba - 1);
        if (persist_gpt_change(disk, header) != 0) return -1;
    } else return -1;
    refresh_disk_partitions(disk);
    return 0;
}

/** Delete a partition entry while preserving the data in its sectors.
 * @param disk Whole-disk descriptor.
 * @param number One-based partition entry number.
 * @return 0 on success, -1 if absent, invalid, or I/O fails.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
int blkdev_partition_delete(struct block_device *disk, uint32_t number)
{
    enum blkdev_partition_table table = blkdev_disk_table(disk);
    uint64_t starts[GPT_ENTRY_COUNT], lengths[GPT_ENTRY_COUNT];
    uint8_t used[GPT_ENTRY_COUNT], sector[BLKDEV_SECTOR_SIZE];
    uint32_t slots;
    if (!valid_whole_disk(disk) || number == 0 ||
        read_regions(disk, table, starts, lengths, used, &slots) != 0 ||
        number > slots || !used[number - 1]) return -1;
    if (table == BLKDEV_PARTITION_TABLE_MBR) {
        if (blkdev_read(disk, 0, sector) != 0) return -1;
        for (uint32_t i = 0; i < 16; ++i)
            sector[446 + (number - 1) * 16 + i] = 0;
        if (blkdev_write(disk, 0, sector) != 0) return -1;
    } else if (table == BLKDEV_PARTITION_TABLE_GPT) {
        uint8_t header[BLKDEV_SECTOR_SIZE];
        if (gpt_load(disk, header) != 0) return -1;
        for (uint32_t i = 0; i < GPT_ENTRY_SIZE; ++i)
            gpt_entries[(number - 1) * GPT_ENTRY_SIZE + i] = 0;
        if (persist_gpt_change(disk, header) != 0) return -1;
    } else return -1;
    refresh_disk_partitions(disk);
    return 0;
}

/** Placeholder for future interrupt-driven block I/O. 
 * @return None. 
 * @copyright See LICENSE.md. 
 * @date 2026-11-25 */
void enable_interupts_blockdev(void)
{
    printf("BLKDEV: interrupt-driven I/O is not enabled\n");
}
