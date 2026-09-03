#include <lib/multiboot.h>
#include <errno.h>

//EXTERNAL INCLUDES
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include <uacpi/event.h>

//SUBSYSTEM INCLUDES
#include <gdt/gdt.h>
#include <idt/idt.h>
#include <pic/pic.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <drivers/pci/pci.h>

#include <console/console.h>

__attribute__((section(".multiboot")))
struct multiboot_header_t mboot_header = {
    .magic = MULTIBOOT_MAGIC,
    .flags = MULTIBOOT_FLAGS,
    .checksum = -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)
};

typedef struct multiboot_memory_map_t mmap_entry_t;

void internal_panic(const char *message) {
    printf("[PANIC] ");
    printf(message);
    printf("\n");
    while (1) {
        asm volatile("hlt");
    }
}

int init_acpi(void) {

    uacpi_status ret = uacpi_initialize(0);
    if (uacpi_unlikely_error(ret)) {
        //log_error("uacpi_initialize error: %s", uacpi_status_to_string(ret));
        printf("uacpi_initialize error: %s\n", uacpi_status_to_string(ret));
        return -ENODEV;
    }

    scroll_screen();
    ret = uacpi_namespace_load();
    if (uacpi_unlikely_error(ret)) {
        //log_error("uacpi_namespace_load error: %s", uacpi_status_to_string(ret));
        printf("uacpi_namespace_load error: %s\n", uacpi_status_to_string(ret));
        return -ENODEV;
    }


    ret = uacpi_namespace_initialize();
    if (uacpi_unlikely_error(ret)) {
        //log_error("uacpi_namespace_initialize error: %s", uacpi_status_to_string(ret));

        printf("uacpi_namespace_initialize error: %s\n", uacpi_status_to_string(ret));
        return -ENODEV;
    }

    uacpi_set_interrupt_model(UACPI_INTERRUPT_MODEL_IOAPIC);

    ret = uacpi_finalize_gpe_initialization();
    if (uacpi_unlikely_error(ret)) {
        //log_error("uACPI GPE initialization error: %s", uacpi_status_to_string(ret));
        printf("uACPI GPE initialization error: %s\n", uacpi_status_to_string(ret));
        return -ENODEV;
    }

    return 0;
}

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
    printf("Usable memory: %u bytes\n", total_memory / (1024 * 1024));
    scroll_screen();

    scroll_screen();
    scroll_screen();
    vmm_init();
   

    init_acpi();

    pci_init();

    console();
    internal_panic("Kernel has panic due to something going wrong internally");
}