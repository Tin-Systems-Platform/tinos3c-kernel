#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <lib/std/stdint.h>

// keyboard function prototypes
void  kbd_push(uint8_t scanode);

uint8_t kbd_pop();

char kbd_read_blocking();

char read_char();

#endif