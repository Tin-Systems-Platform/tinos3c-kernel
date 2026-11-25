#include <lib/multiboot.h>
#include <errno.h>

//EXTERNAL INCLUDES
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include <uacpi/event.h>

//SUBSYSTEM INCLUDES
#include <core/gdt/gdt.h>
#include <core/idt/idt.h>
#include <core/pic/pic.h>
#include <core/mm/pmm.h>
#include <core/mm/vmm.h>
#include <hal/pci/pci.h>
#include <hal/ahci/ahci.h>
#include <hal/blkdev/blkdev.h>


//INTERNAL SYSLIB INCLUDES
#include <lib/sys/panic.h>

#include <console/console.h>

__attribute__((section(".multiboot")))
struct multiboot_header_t mboot_header = {
    .magic = MULTIBOOT_MAGIC,
    .flags = MULTIBOOT_FLAGS,
    .checksum = -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)
};

typedef struct multiboot_memory_map_t mmap_entry_t;

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
    serial_init(); // Debugging via serial

    asm volatile("sti"); // Enable interrupts after PIC remapping

    uint64_t total_memory = 0;

    if (mboot_info == 0 || mboot_magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("Invalid multiboot handoff", "BAD_MULTIBOOT_HEADER");
    
    pmm_init();

	uint8_t *mmap_cursor;
    uint32_t mmap_remaining;

    if ((mboot_info->flags & (1U << 6)) == 0 ||
        mboot_info->mmap_length == 0)
        panic("Multiboot memory map is missing", "NO_MEMORY_MAP");

    mmap_cursor = (uint8_t *)(uintptr_t)mboot_info->mmap_addr;
    mmap_remaining = mboot_info->mmap_length;

    printf("[BOOT] Memory map entries:\n");

    while (mmap_remaining != 0)
    {
        mmap_entry_t *entry;
        uint32_t entry_bytes;

        /* The size field excludes itself; require the standard 20-byte body. */
        if (mmap_remaining < sizeof(entry->size))
            panic("Truncated multiboot memory map", "BAD_MEMORY_MAP");

        entry = (mmap_entry_t *)mmap_cursor;
        if (entry->size < 20 || entry->size > UINT32_MAX - sizeof(entry->size))
            panic("Invalid multiboot memory map entry", "BAD_MEMORY_MAP");
        entry_bytes = entry->size + sizeof(entry->size);
        if (entry_bytes > mmap_remaining)
            panic("Truncated multiboot memory map entry", "BAD_MEMORY_MAP");

        uint32_t type = entry->type;
        if (type == MULTIBOOT_MEMORY_AVAILABLE)
        {
            uint64_t base =
                ((uint64_t)entry->addr_high << 32) |
                entry->addr_low;

            uint64_t length =
                ((uint64_t)entry->len_high << 32) |
                entry->len_low;

            total_memory += length;
            printf("[MMAP] Base=");


            printf("0x%x", base);
            printf(" Len=");
            printf("0x%x", length);
            printf(" Type=");
            printf("%u\n", type);

            pmm_add_region(base, length);
        }

        mmap_cursor += entry_bytes;
        mmap_remaining -= entry_bytes;
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
    printf("Usable memory: %u MiB\n", total_memory / (1024 * 1024));

    vmm_init();
   

    init_acpi();

    pci_init();

    blkdev_init();

    ahci_init();
    //printf("Usable memory: %u MiB\n", total_memory / (1024 * 1024));

    console();
    panic("Kernel has paniced due to something going wrong internally", "DEBUG_CONSOLE_EXITED");
}
