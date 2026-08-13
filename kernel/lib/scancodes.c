#include "scancodes.h"
#include <lib/std/stdio.h>
#include <lib/std/stdint.h>


#define KEYBOARD_PORT 0x60
#define STATUS_PORT   0x64
// read scancodes from keyboard from port 0x60

uint8_t keyboard_read_scancode() {
    while ((inb(STATUS_PORT) & 1) == 0);
    return inb(KEYBOARD_PORT);
}

// scancode to ascii map
char scancode_to_ascii(uint8_t scancode) {
    static char kbd_map[] = {
        0,  27, '1', '2', '3', '4', '5', '6', '7', '8',	'9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
        '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*',
        0, ' '
    };
    if (scancode < sizeof(kbd_map)) return kbd_map[scancode];
    return 0;
}