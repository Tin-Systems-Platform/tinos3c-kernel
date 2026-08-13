#include <lib/multiboot.h>

#include <gdt/gdt.h>
#include <idt/idt.h>
#include <pic/pic.h>

#include <console/console.h>

__attribute__((section(".multiboot")))
struct multiboot_header_t mboot_header = {
    .magic = MULTIBOOT_MAGIC,
    .flags = MULTIBOOT_FLAGS,
    .checksum = -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)
};

void _main(struct multiboot_info_t *mboot_info, uint32_t mboot_magic) {
    init_gdt();
    init_idt();

    pic_remap(0x20, 0x28); // Remap PIC:

    asm volatile("sti"); // Enable interrupts after PIC remapping
    console();
}