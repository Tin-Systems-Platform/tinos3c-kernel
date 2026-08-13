#include <idt/interrupt.h>

#include <pic/pic.h>
#include <drivers/keyboard/kb.h>


// Common ISR handler
void isr_handler(struct regs  *r) {
    if (r->int_no == 33) {
        uint8_t scancode = inb(0x60); 
        if (!(scancode & 0x80))
        {
            kbd_push(scancode);
        }
    }

    if (r->int_no >= 32) {
        if (r->int_no >= 40) {
            outb(0xA0, 0x20); 
        }
        outb(0x20, 0x20);     
    }
}