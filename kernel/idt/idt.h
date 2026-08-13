#ifndef IDT_H
#define IDT_H

#include <lib/std/stdint.h>

struct idt_entry_struct {
    uint16_t base_low;  
    uint16_t sel;       
    uint8_t  always0;   
    uint8_t  flags;     
    uint16_t base_high; 
} __attribute__((packed));

typedef struct idt_entry_struct idt_entry_t;

struct idt_ptr_struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

typedef struct idt_ptr_struct idt_ptr_t;

extern idt_entry_t idt[256];
extern idt_ptr_t idt_ptr;

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
void init_idt();
#endif