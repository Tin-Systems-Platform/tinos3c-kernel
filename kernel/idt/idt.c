#include "idt.h"
#include <lib/std/string.h>

extern void isr0();
extern void isr1();
extern void isr14();

idt_entry_t idt[256] __attribute__((aligned(16)));
idt_ptr_t idt_p;

extern void idt_flush(const idt_ptr_t *idt_ptr_addr);

//Set IDT gate
void idt_set_gate(uint8_t num, uint64_t offset, uint16_t sel, uint8_t flags) {
    idt[num].offset_low = (uint16_t)(offset & 0xFFFFU);
    idt[num].offset_middle = (uint16_t)((offset >> 16) & 0xFFFFU);
    idt[num].offset_high = (uint32_t)(offset >> 32);
    idt[num].sel = sel;
    idt[num].ist = 0;
    idt[num].flags = flags;
    idt[num].reserved = 0;
}

// Initialize the IDT
void init_idt() {   
    idt_p.limit = (sizeof(idt_entry_t) * 256) - 1;
    idt_p.base  = (uint64_t)(uintptr_t)&idt;
    memset(&idt, 0, sizeof(idt_entry_t) * 256);
    idt_set_gate(14, (uint64_t)(uintptr_t)isr14, 0x08, 0x8E);
    idt_set_gate(32, (uint64_t)(uintptr_t)isr0, 0x08, 0x8E);
    idt_set_gate(33, (uint64_t)(uintptr_t)isr1, 0x08, 0x8E);
    idt_flush(&idt_p);
}
