#include "console.h"
#include <drivers/keyboard/kb.h>
#include "test_cmds/sleep.h"

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