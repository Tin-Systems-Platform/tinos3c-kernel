#include "console.h"
#include <hal/keyboard/kb.h>
#include "test_cmds/sleep.h"
#include <lib/std/stdio.h>
#include <core/mm/pmm.h>
#include <lib/sys/panic.h>
#include "test_cmds/crash.h"
#include <hal/blkdev/blkdev.h>

/** Parse one unsigned decimal token. @param text Token start. @param value Output value. @return Pointer after token, or NULL for malformed/overflowing input. @copyright See LICENSE.md. @date 2026-11-25 */
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

/** Write a byte-pattern sector and read it back for a destructive disk test. @param device_index Registered disk index. @param lba Target sector. @return None; reports invalid input or I/O/verification errors. @copyright See LICENSE.md. @date 2026-11-25 */
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
