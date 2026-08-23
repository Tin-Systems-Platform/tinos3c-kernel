#include "idt.h"
#include <lib/std/string.h>

extern void isr0();
extern void isr1();
extern void isr14();

idt_entry_t idt[256] __attribute__((aligned(16)));
idt_ptr_t idt_p;

extern void idt_flush(uint32_t idt_ptr_addr);

//Set IDT gate
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel       = sel;
    idt[num].always0   = 0;
    idt[num].flags     = flags; 
}

// Initialize the IDT
void init_idt() {   
    idt_p.limit = (sizeof(idt_entry_t) * 256) - 1;
    idt_p.base  = (uint32_t)&idt;
    memset(&idt, 0, sizeof(idt_entry_t) * 256);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(32, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)isr1, 0x08, 0x8E);
    idt_flush((uint32_t)&idt_p);
}
