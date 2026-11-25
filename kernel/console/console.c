#include "console.h"

#include <hal/keyboard/kb.h>
#include <lib/std/stdio.h>

#include "test_cmds/commands.h"
#include "test_cmds/crash.h"
#include "test_cmds/sleep.h"

/** Read input and pass each completed line to the console command handlers.
 * @return None; runs until the exit command is entered.
 * @copyright See LICENSE.md.
 * @date 2026-11-25 */
void console(void)
{
    char buffer[128];
    int position = 0;

    printf("This console is the Tinos3c kernel Debug Console.\n");
    printf(">");

    while (1) {
        char character = read_char();

        if (!character) {
            continue;
        }

        if (character == '\b') {
            if (position > 0) {
                --position;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
            continue;
        }

        if (character == '\n') {
            buffer[position] = 0;
            printf("\n");

            if (position > 0) {
                if (strcmp(buffer, "halt") == 0) {
                    while (1) {
                        asm volatile ("hlt");
                    }
                } else if (strcmp(buffer, "poweroff") == 0) {
                    system_shutdown();
                } else if (strcmp(buffer, "meminfo") == 0) {
                    console_cmd_meminfo();
                } else if (strcmp(buffer, "crash") == 0) {
                    crash();
                } else if (position >= 4 && buffer[0] == 'd' &&
                           buffer[1] == 'i' && buffer[2] == 's' &&
                           buffer[3] == 'k' &&
                           (buffer[4] == ' ' || buffer[4] == '\t' ||
                            buffer[4] == 0)) {
                    console_cmd_disk(buffer);
                } else if (strcmp(buffer, "massstorageinfo") == 0) {
                    console_cmd_massstorageinfo();
                } else if (position >= 8 && buffer[0] == 'd' &&
                           buffer[1] == 'i' && buffer[2] == 's' &&
                           buffer[3] == 'k' && buffer[4] == 't' &&
                           buffer[5] == 'e' && buffer[6] == 's' &&
                           buffer[7] == 't' &&
                           (buffer[8] == ' ' || buffer[8] == 0)) {
                    console_cmd_disktest(buffer + 8);
                } else if (strcmp(buffer, "exit") == 0) {
                    break;
                } else {
                    printf("Unknown command: %s\n", buffer);
                }
            }

            scroll_screen();
            printf(">");
            position = 0;
            continue;
        }

        if (position < (int)sizeof(buffer) - 1) {
            buffer[position++] = character;
            putchar(character);
        }
    }
}
