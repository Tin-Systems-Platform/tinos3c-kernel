#include "console.h"
#include <drivers/keyboard/kb.h>
#include "test_cmds/sleep.h"
#include <lib/std/stdio.h>
#include <mm/pmm.h>
#include <lib/sys/panic.h>
#include "test_cmds/crash.h"

static void show_memory_info(void) {

    uint64_t free = pmm_get_free_pages();
    uint64_t total = pmm_get_total_pages();
    uint64_t used = total - free;

    printf("[DEBUG] free_pages=");
    printf("%u", free);
    printf(" total_pages=");
    printf("%u", total);
    printf("\n");

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
            else if (strcmp(buffer, "crash") == 0) {
                crash();
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