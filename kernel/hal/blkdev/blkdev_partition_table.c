#include "blkdev.h"
#include "blkdev_internal.h"
#include "blkdev_partition_internal.h"

#include <lib/std/stdio.h>

uint8_t gpt_entries[GPT_TABLE_BYTES];
struct block_device *gpt_cache_disk;
uint32_t gpt_cache_entries_crc;
uint64_t gpt_cache_first_usable;
uint64_t gpt_cache_last_usable;
uint8_t gpt_cache_valid;

/** Reset cached GPT data during block-device initialization.
 * @return None.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
void blkdev_partition_table_init(void)
{
    gpt_cache_valid = 0;

    gpt_cache_disk = 0;
    gpt_cache_entries_crc = 0;
    gpt_cache_first_usable = 0;
    gpt_cache_last_usable = 0;
}

/** Invalidate cached GPT data when table metadata is changed through block I/O.
 * @param bdev Device being written.
 * @param lba Sector being written.
 * @return None.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
void blkdev_partition_note_write(struct block_device *bdev, uint64_t lba)
{
    if (!gpt_cache_valid || bdev != gpt_cache_disk) {
        return;
    }

    if (lba <= GPT_TABLE_SECTORS + 1 ||
        lba >= bdev->capacity_lba - GPT_TABLE_SECTORS - 1) {
        gpt_cache_valid = 0;
    }
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

/** Read and validate the kernel's supported canonical primary GPT layout.
 * @param disk Whole-disk descriptor.
 * @param header Output primary header sector.
 * @return 0 when the GPT uses 128 entries of 128 bytes; otherwise -1.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int gpt_load(struct block_device *disk, uint8_t *header)
{
    uint32_t crc;
    if (!blkdev_partition_valid_disk(disk) ||
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

    if (blkdev_partition_valid_disk(disk) && gpt_cache_valid && gpt_cache_disk == disk)
        return BLKDEV_PARTITION_TABLE_GPT;

    if (!blkdev_partition_valid_disk(disk) || blkdev_read(disk, 0, sector) != 0 ||
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

/** Check whether a sector consists entirely of zero bytes.
 * @param sector Sector contents.
 * @return Nonzero when all bytes are zero.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int sector_is_zero(const uint8_t *sector)
{
    for (uint32_t i = 0; i < BLKDEV_SECTOR_SIZE; ++i) {
        if (sector[i] != 0) return 0;
    }

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
    if (!blkdev_partition_valid_disk(disk) ||
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
    blkdev_partition_refresh_disk(disk);
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
    if (!blkdev_partition_valid_disk(disk) || number == 0 ||
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
        if (gpt_store(disk, header) != 0) return -1;
    }
    *number = slot + 1;
    blkdev_partition_refresh_disk(disk);
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

    if (!blkdev_partition_valid_disk(disk) || number == 0 ||
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
        if (gpt_store(disk, header) != 0) return -1;
    } else {
        return -1;
    }

    blkdev_partition_refresh_disk(disk);
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

    if (!blkdev_partition_valid_disk(disk) || number == 0 ||
        read_regions(disk, table, starts, lengths, used, &slots) != 0 ||
        number > slots || !used[number - 1]) {
        return -1;
    }

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
        if (gpt_store(disk, header) != 0) return -1;
    } else {
        return -1;
    }

    blkdev_partition_refresh_disk(disk);
    return 0;
}

