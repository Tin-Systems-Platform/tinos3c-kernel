#include <drivers/keyboard/kb.h>
#include <lib/std/stdio.h>
#include <lib/std/stdint.h>


#define KBD_BUF_SIZE 256

static uint8_t kbd_buffer[KBD_BUF_SIZE];

static uint32_t kbd_head = 0;

static uint32_t kbd_tail = 0;

void kbd_push(uint8_t scancode) {
    uint32_t next = (kbd_head + 1) % KBD_BUF_SIZE;

    if (next != kbd_tail) {
        kbd_buffer[kbd_head] = scancode;
        kbd_head = next;
    }
}

uint8_t kbd_pop() {
    if (kbd_head == kbd_tail) return 0; 

    uint8_t scancode = kbd_buffer[kbd_tail];

    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;

    return scancode;
}

char kbd_read_blocking() {
    static const char scancode_table[128] = {
        0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
        '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
        'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
        'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
    };

    while (kbd_head == kbd_tail) {
        asm volatile("hlt");
    }

    uint8_t sc = kbd_pop();

    return sc < 128 ? scancode_table[sc] : 0;
}

char read_char() {
    static const char scancode_table[128] = {
        0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
        '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
        'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
        'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
    };

    uint8_t sc = kbd_pop();

    if (sc == 0) return 0;

    return sc < 128 ? scancode_table[sc] : 0;
}