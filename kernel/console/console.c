#include "console.h"
#include <drivers/keyboard/kb.h>
#include "test_cmds/sleep.h"
#include <lib/std/stdio.h>
#include <mm/pmm.h>

static void show_memory_info(void) {

    uint64_t free = pmm_get_free_pages();
    uint64_t total = pmm_get_total_pages();
    uint64_t used = total - free;
    uint64_t free_mb = (free * 4096) / (1024 * 1024);
    uint64_t total_mb = (total * 4096) / (1024 * 1024);
    uint64_t used_mb = (used * 4096) / (1024 * 1024);
    uint32_t usage_percent = (used * 100) / total;


    printf("Memory Status:\n");
    printf("  Total:  %u MiB (%u pages)\n", total_mb, total);
    printf("  Used:   %u MiB (%u pages)\n", used_mb, used);
    printf("  Free:   %u MiB (%u pages)\n", free_mb, free);
    printf("  Usage:  %u%%\n", usage_percent);
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

            if (strcmp(buffer, "ver") == 0) {
                printf("Tinos3 C edition\n");
            } else if (strcmp(buffer, "halt") == 0)
            {
                while (1) asm volatile ("hlt");
            } else if (strcmp(buffer,"poweroff") == 0)
            {
                system_shutdown();
            }
            else if (strcmp(buffer, "meminfo") == 0) {
                show_memory_info();
            }
            else {
                printf("Unknown command: %s\n", buffer);
            }

            scroll_screen();
            printf(">");
            pos = 0;
        } else if (pos < sizeof(buffer) - 1) {
            buffer[pos++] = c;
            putchar(c);
        }
    }
}