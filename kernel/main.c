#include <lib/multiboot.h>

#include <gdt/gdt.h>
#include <idt/idt.h>
#include <pic/pic.h>
#include <mm/pmm.h>

#include <console/console.h>

__attribute__((section(".multiboot")))
struct multiboot_header_t mboot_header = {
    .magic = MULTIBOOT_MAGIC,
    .flags = MULTIBOOT_FLAGS,
    .checksum = -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)
};

typedef struct multiboot_memory_map_t mmap_entry_t;

void _main(struct multiboot_info_t *mboot_info, uint32_t mboot_magic) {
    init_gdt();
    init_idt();

    pic_remap(0x20, 0x28); // Remap PIC:

    asm volatile("sti"); // Enable interrupts after PIC remapping

    uint64_t total_memory = 0;

    pmm_init();

	uint8_t* mmap_start =
        (uint8_t*)(uintptr_t)mboot_info->mmap_addr;

    uint8_t* mmap_end =
        mmap_start + mboot_info->mmap_length;

    mmap_entry_t* entry = (mmap_entry_t*)mmap_start;

    while ((uint8_t*)entry < mmap_end)
    {
        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE)
        {
            uint64_t base =
                ((uint64_t)entry->addr_high << 32) |
                entry->addr_low;

            uint64_t length =
                ((uint64_t)entry->len_high << 32) |
                entry->len_low;

                total_memory += length;

            pmm_add_region(base, length);
        }

        entry = (mmap_entry_t*)((uint8_t*)entry +
                                entry->size +
                                sizeof(entry->size));
    }

    /*
    * These are defined by the linker, and such are basicly dynamic.
    * We can use them to reserve the memory that the kernel is using.
    */
    extern char kernel_start;
    extern char kernel_end;

    uint64_t start = (uint64_t)&kernel_start;
    uint64_t end   = (uint64_t)&kernel_end;

    pmm_reserve_region(start, end - start);

    // Output the memory amount
    print("Usable memory: ");
    print_uint64(total_memory / (1024 * 1024));
    print(" MB\n");
    
    console();
}