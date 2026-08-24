#include <lib/std/stdint.h>
#include "gdt.h"

gdt_entry_t gdt[3] __attribute__((aligned(16)));
gdt_ptr_t gdt_p; 

extern void gdt_flush(const gdt_ptr_t *gdt_ptr_addr);

// Set GDT gate
void gdt_set_gate(int32_t num, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;
    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access      = access;
}

// Initialize the GDT
void init_gdt() {
    gdt_p.limit = (sizeof(gdt_entry_t) * 3) - 1;
    gdt_p.base  = (uint64_t)(uintptr_t)&gdt;
    gdt_set_gate(0, 0, 0, 0, 0);
    /* Long-mode code descriptor: L=1 and D=0. */
    gdt_set_gate(1, 0, 0, 0x9A, 0xA0);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_flush(&gdt_p);
}
