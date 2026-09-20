#include "panic.h"
#include <lib/std/stdio.h>

void panic(const char* message, const char* panicCode) {
    asm volatile("cli");
    printf("[PANIC]: %s", message);
    
    printf("\n");

    printf("Exception: %s", panicCode);

    printf("\nSystem halted. Please restart the system.\n");

    while (1) {
        asm volatile("hlt");
    }
}