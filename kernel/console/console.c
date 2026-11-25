#include "console.h"
#include <hal/keyboard/kb.h>
#include "test_cmds/sleep.h"
#include <lib/std/stdio.h>
#include <core/mm/pmm.h>
#include <lib/sys/panic.h>
#include "test_cmds/crash.h"
#include <hal/blkdev/blkdev.h>

/** Parse one unsigned decimal token.
 *  @param text Token start. 
 *  @param value Output value.
 *  @return Pointer after token, or NULL for malformed/overflowing input.
 *  @copyright See LICENSE.md.
 *  @date 2026-11-25 
 **/
static const char *parse_decimal(const char *text, uint64_t *value) {
    uint64_t result = 0;

    if (text == 0 || value == 0 || *text < '0' || *text > '9') return 0;

    while (*text >= '0' && *text <= '9') {
        uint32_t digit = (uint32_t)(*text++ - '0');

        if (result > (~0ULL - digit) / 10) return 0;
        result = result * 10 + digit;
    }

    *value = result;

    return text;
}

/** Write a byte-pattern sector and read it back for a destructive disk test. 
 * @param device_index Registered disk index. 
 * @param lba Target sector.
 * @return None; reports invalid input or I/O/verification errors. 
 * @copyright See LICENSE.md.
 * @date 2026-11-25 
 **/
static void disk_write_test(uint64_t device_index, uint64_t lba) {
    struct block_device *device = device_index > UINT32_MAX ? 0 : blkdev_get((uint32_t)device_index);

    uint8_t written[BLKDEV_SECTOR_SIZE];
    uint8_t readback[BLKDEV_SECTOR_SIZE];

    if (device == 0 || lba >= device->capacity_lba) {
        printf("disktest: invalid device index or LBA\n");
        return;
    }

    for (uint32_t i = 0; i < BLKDEV_SECTOR_SIZE; ++i) written[i] = (uint8_t)i;
    printf("WARNING: disktest overwrites %s LBA %u.\n", device->name, lba);

    if (blkdev_write(device, lba, written) != 0) {
        printf("disktest: write failed\n");
        return;
    }

    if (blkdev_read(device, lba, readback) != 0) {
        printf("disktest: readback failed\n");
        return;
    }

    for (uint32_t i = 0; i < BLKDEV_SECTOR_SIZE; ++i) {
        if (readback[i] != written[i]) {
            printf("disktest: mismatch at byte %u (expected %02x, got %02x)\n", i, written[i], readback[i]);
            return;
        }
    }

    printf("disktest: wrote and verified 512 test bytes\n");
}

static void show_memory_info(void) {

    uint64_t free = pmm_get_free_pages();
    uint64_t total = pmm_get_total_pages();
    uint64_t used = total - free;

    uint64_t free_mb = (free * 4096) / (1024 * 1024);
    uint64_t total_mb = (total * 4096) / (1024 * 1024);
    uint64_t used_mb = (used * 4096) / (1024 * 1024);
    uint32_t usage_percent = (used * 100) / total;


     printf("  Total: ");
    printf("%u", total_mb);
    printf(" MiB (");
    printf("%u", total);
    printf(" pages)\n");
    printf("  Used:  ");
    printf("%u", used_mb);
    printf(" MiB (");
    printf("%u", used);
    printf(" pages)\n");
    printf("  Free:  ");
    printf("%u", free_mb);
    printf(" MiB (");
    printf("%u", free);
    printf(" pages)\n");
    printf("  Usage: ");
    printf("%u", usage_percent);
    printf("%%\n");
}

/** Print a sector capacity using MiB or GiB units.
 * @param sectors Sector count.
 * @return None.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static void print_disk_size(uint64_t sectors)
{
    const uint64_t sectors_per_gib = 2097152ULL;
    const uint64_t sectors_per_mib = 2048ULL;
    if (sectors >= sectors_per_gib) {
        uint64_t whole = sectors / sectors_per_gib;
        uint64_t fraction = (sectors % sectors_per_gib) * 100 / sectors_per_gib;
        printf("%u.", whole);
        if (fraction < 10) printf("0");
        printf("%u GiB", fraction);
    } else {
        uint64_t whole = sectors / sectors_per_mib;
        uint64_t fraction = (sectors % sectors_per_mib) * 100 / sectors_per_mib;
        printf("%u.", whole);
        if (fraction < 10) printf("0");
        printf("%u MiB", fraction);
    }
}

/** Parse a whitespace-separated command into a bounded token array.
 * @param command Mutable command line.
 * @param tokens Output token pointers.
 * @param capacity Maximum tokens available.
 * @return Token count, or zero when the token array is too small.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static uint32_t split_command(char *command, char **tokens, uint32_t capacity)
{
    uint32_t count = 0;
    char *cursor = command;
    while (*cursor != 0) {
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (*cursor == 0) break;
        if (count == capacity) return 0;
        tokens[count++] = cursor;
        while (*cursor != 0 && *cursor != ' ' && *cursor != '\t') ++cursor;
        if (*cursor != 0) *cursor++ = 0;
    }
    return count;
}

/** Parse one complete unsigned decimal command token.
 * @param token Text token.
 * @param value Output value.
 * @return Nonzero if the token contains only a valid decimal integer.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static int parse_number_token(const char *token, uint64_t *value)
{
    const char *end = parse_decimal(token, value);
    return end != 0 && *end == 0;
}

/** Show a disk and all its current partition entries.
 * @param disk_index Zero-based whole-disk number.
 * @return None; prints an error if the disk does not exist.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static void show_disk_info(uint64_t disk_index)
{
    struct block_device *disk = disk_index > UINT32_MAX ? 0 :
        blkdev_disk_get((uint32_t)disk_index);
    if (disk == 0) {
        printf("No disk with that number. Use 'disk list' to see available disks.\n");
        return;
    }

    enum blkdev_partition_table table = blkdev_disk_table(disk);
    const char *table_name = table == BLKDEV_PARTITION_TABLE_MBR ? "MBR" :
        table == BLKDEV_PARTITION_TABLE_GPT ? "GPT" : "not initialized";
    printf("Disk %u: %s, ", disk_index, disk->name);
    print_disk_size(disk->capacity_lba);
    printf(" (%u sectors), table: %s\n", disk->capacity_lba, table_name);

    if (table == BLKDEV_PARTITION_TABLE_NONE) {
        printf("  No supported partition table. Run 'disk help' for setup steps.\n");
        return;
    }

    uint32_t listed = 0;
    uint32_t max_entries = table == BLKDEV_PARTITION_TABLE_MBR ? 4 : 128;
    for (uint32_t number = 1; number <= max_entries; ++number) {
        struct blkdev_partition_info info;
        if (blkdev_partition_get_info(disk, number, &info) != 0) continue;
        printf("  %s: start LBA %u, ", info.name, info.start_lba);
        print_disk_size(info.sector_count);
        printf(" (%u sectors)\n", info.sector_count);
        ++listed;
    }
    if (listed == 0) printf("  No partitions yet. Use 'disk create' to add one.\n");
}

/** List disks or perform one beginner-oriented disk-management command.
 * Supported sizes are whole MiB; omitted create start means first available
 * aligned space. Initialization and deletion change table metadata.
 * @param command Mutable command line beginning with "disk".
 * @return None; prints usage and operation status.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
static void disk_command(char *command)
{
    char *tokens[6];
    uint32_t count = split_command(command, tokens, 6);
    uint64_t disk_index, value, size_lba, start_lba;
    struct block_device *disk;
    if (count < 2 || strcmp(tokens[1], "help") == 0) {
        printf("Disk commands (disk numbers start at 0):\n");
        printf("  disk list\n");
        printf("  disk info <disk>\n");
        printf("  disk init <disk> mbr|gpt  (use on a blank disk)\n");
        printf("  disk create <disk> <size-MiB> [start-MiB]\n");
        printf("  disk resize <disk> <partition> <new-size-MiB> [confirm-shrink]\n");
        printf("  disk delete <disk> <partition>  (removes entry; keeps data)\n");
        printf("Create chooses the first free 1 MiB-aligned space when start is omitted.\n");
        printf("Example: disk init 0 gpt, then disk create 0 1024, then disk list.\n");
        printf("Resize keeps the start; shrinking can cut off filesystem data.\n");
        printf("Delete removes the table entry, so filesystems on it become inaccessible.\n");
        return;
    }
    if (strcmp(tokens[1], "list") == 0) {
        uint32_t disk_count = blkdev_disk_count();
        if (disk_count == 0) printf("No disks registered.\n");
        for (uint32_t i = 0; i < disk_count; ++i) {
            show_disk_info(i);
        }
        return;
    }
    if (strcmp(tokens[1], "info") == 0 && count == 3 &&
        parse_number_token(tokens[2], &disk_index)) {
        show_disk_info(disk_index);
        return;
    }
    if (count < 3 || !parse_number_token(tokens[2], &disk_index) ||
        disk_index > UINT32_MAX || (disk = blkdev_disk_get((uint32_t)disk_index)) == 0) {
        printf("Invalid disk number. Use 'disk list' to see available disks.\n");
        return;
    }
    if (strcmp(tokens[1], "init") == 0 && count == 4) {
        enum blkdev_partition_table table = strcmp(tokens[3], "mbr") == 0 ?
            BLKDEV_PARTITION_TABLE_MBR : strcmp(tokens[3], "gpt") == 0 ?
            BLKDEV_PARTITION_TABLE_GPT : BLKDEV_PARTITION_TABLE_NONE;
        if (table == BLKDEV_PARTITION_TABLE_NONE) {
            printf("Choose 'mbr' or 'gpt'.\n");
        } else if (blkdev_disk_initialize(disk, table) != 0) {
            printf("Could not initialize disk. It may already contain a table or data.\n");
        } else {
            printf("Initialized %s with %s.\n", disk->name,
                   table == BLKDEV_PARTITION_TABLE_MBR ? "MBR" : "GPT");
        }
        return;
    }
    if (strcmp(tokens[1], "create") == 0 && (count == 4 || count == 5) &&
        parse_number_token(tokens[3], &value) && value != 0 &&
        value <= ~0ULL / 2048ULL) {
        size_lba = value * 2048ULL;
        start_lba = 0;
        if (count == 5) {
            if (!parse_number_token(tokens[4], &value) || value == 0 ||
                value > ~0ULL / 2048ULL) {
                printf("Start size must be a positive whole MiB.\n");
                return;
            }
            start_lba = value * 2048ULL;
        }
        uint32_t number;
        if (blkdev_partition_create(disk, size_lba, start_lba, &number) != 0)
            printf("Could not create partition: size may not fit or the table may be full.\n");
        else {
            printf("Created partition %u on %s: ", number, disk->name);
            print_disk_size(size_lba);
            printf(".\n");
        }
        return;
    }
    if (strcmp(tokens[1], "resize") == 0 && (count == 5 || count == 6) &&
        parse_number_token(tokens[3], &value) && value > 0 && value <= UINT32_MAX &&
        parse_number_token(tokens[4], &size_lba) && size_lba > 0 &&
        size_lba <= ~0ULL / 2048ULL) {
        struct blkdev_partition_info old_info;
        if (count == 6 && strcmp(tokens[5], "confirm-shrink") != 0) {
            printf("For a smaller size, add 'confirm-shrink' after the size.\n");
            return;
        }
        size_lba *= 2048ULL;
        if (blkdev_partition_get_info(disk, (uint32_t)value, &old_info) != 0) {
            printf("No partition with that number.\n");
            return;
        }
        if (size_lba < old_info.sector_count &&
            (count != 6 || strcmp(tokens[5], "confirm-shrink") != 0)) {
            printf("Shrinking can cut off filesystem data. Shrink the filesystem first, then repeat with 'confirm-shrink'.\n");
            return;
        }
        if (blkdev_partition_resize(disk, (uint32_t)value, size_lba) != 0)
            printf("Could not resize partition: it may overlap another partition or exceed the disk.\n");
        else printf("Resized partition %u.\n", value);
        return;
    }
    if (strcmp(tokens[1], "delete") == 0 && count == 4 &&
        parse_number_token(tokens[3], &value) && value > 0 && value <= UINT32_MAX) {
        if (blkdev_partition_delete(disk, (uint32_t)value) != 0)
            printf("Could not delete that partition.\n");
        else printf("Removed partition %u's table entry; its data sectors were not erased.\n", value);
        return;
    }
    printf("Unknown or incomplete disk command. Type 'disk help'.\n");
}



void console() {
    char buffer[128];
    int pos = 0;

    printf("This console is the Tinos3c kernel Debug Console.\n");
    printf(">");

    while (1) {
        char c = read_char();
        if (!c) continue;

        if (c == '\b') {
            if (pos > 0) {
                pos--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
        } else if (c == '\n') {
            buffer[pos] = '\0';
            printf("\n");

            if (pos > 0) {
                if (strcmp(buffer, "halt") == 0)
                {
                    while (1) asm volatile ("hlt");
                } else if (strcmp(buffer,"poweroff") == 0)
                {
                    system_shutdown();
                }
                else if (strcmp(buffer, "meminfo") == 0) {
                    show_memory_info();
                }
                else if (strcmp(buffer, "crash") == 0) {
                    crash();
                } else if (pos >= 4 && buffer[0] == 'd' && buffer[1] == 'i' &&
                           buffer[2] == 's' && buffer[3] == 'k' &&
                           (buffer[4] == ' ' || buffer[4] == '\t' || buffer[4] == 0)) {
                    disk_command(buffer);
                }
                else if (strcmp(buffer, "massstorageinfo") == 0) {
                    uint32_t index = 0;
                    struct block_device *device;

                    while ((device = blkdev_get(index)) != 0) {
                        printf("%u: %s (%u sectors)\n", index, device->name, device->capacity_lba);
                        ++index;
                    }
                    if (index == 0) printf("No block devices registered\n");
                } else if (pos >= 8 && buffer[0] == 'd' && buffer[1] == 'i' &&
                           buffer[2] == 's' && buffer[3] == 'k' &&
                           buffer[4] == 't' && buffer[5] == 'e' &&
                           buffer[6] == 's' && buffer[7] == 't' &&
                           (buffer[8] == ' ' || buffer[8] == '\0')) {
                    uint64_t device_index, lba;
                    const char *cursor = buffer[8] == ' ' ?
                        parse_decimal(buffer + 9, &device_index) : 0;

                    if (cursor == 0 || *cursor++ != ' ' ||
                        (cursor = parse_decimal(cursor, &lba)) == 0 || *cursor != 0)
                        printf("Usage: disktest <device-index> <LBA>\n");
                    else
                        disk_write_test(device_index, lba);
                } else if (strcmp(buffer, "exit") == 0) {
                    break;
                }
                else {
                    printf("Unknown command: %s\n", buffer);
                }
            }

            scroll_screen();
            printf(">");
            pos = 0;
        } else if (pos < (int)sizeof(buffer) - 1) {
            buffer[pos++] = c;
            putchar(c);
        }
    }
}
