#include "vmm.h"

#include <lib/std/stdio.h>
#include <mm/pmm.h>

/* The kernel runs in 32-bit protected mode and uses two-level x86 paging. */
#define PAGE_SIZE               0x1000U
#define PAGE_ENTRIES            1024U
#define PAGE_ADDRESS_MASK       0xFFFFF000U
#define PAGE_FLAG_PRESENT       0x001U
#define PAGE_FLAG_WRITABLE      0x002U
#define PAGE_FLAG_MASK          0xFFFU
#define RECURSIVE_DIRECTORY     1023U
#define RECURSIVE_TABLES_BASE   0xFFC00000U
#define RECURSIVE_DIRECTORY_VA  0xFFFFF000U

/* These reside in the kernel image and are covered by the bootstrap map. */
static uint32_t initial_page_directory[PAGE_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));
static uint32_t initial_page_table[PAGE_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));

static int paging_enabled;

static int page_aligned_address(uint64_t address)
{
    return (address >> 32) == 0 && (address & (PAGE_SIZE - 1U)) == 0;
}

static uint32_t *active_page_directory(void)
{
    if (paging_enabled)
        return (uint32_t *)(uintptr_t)RECURSIVE_DIRECTORY_VA;

    return initial_page_directory;
}

static uint32_t *active_page_table(uint32_t directory_index)
{
    uint32_t *directory = active_page_directory();

    if ((directory[directory_index] & PAGE_FLAG_PRESENT) == 0)
        return 0;

    if (paging_enabled) {
        return (uint32_t *)(uintptr_t)(RECURSIVE_TABLES_BASE +
                                       directory_index * PAGE_SIZE);
    }

    /* Only the bootstrap table exists before paging is enabled. */
    if (directory_index == 0)
        return initial_page_table;

    return 0;
}

/* Check a full, page-aligned caller buffer before writing page entries to it. */
static int page_buffer_is_valid(const void *buffer)
{
    uint32_t address = (uint32_t)(uintptr_t)buffer;
    uint32_t directory_index;
    uint32_t table_index;
    uint32_t *table;

    if (buffer == 0 || (address & (PAGE_SIZE - 1U)) != 0)
        return 0;

    /* Paging-off pointers are physical/identity addresses; only alignment and
     * null validation are possible until the VMM owns an address space. */
    if (!paging_enabled)
        return 1;

    directory_index = address >> 22;
    table_index = (address >> 12) & 0x3FFU;
    table = active_page_table(directory_index);

    return table != 0 &&
           (table[table_index] & (PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE)) ==
               (PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE);
}

static void flush_tlb_page(uint32_t virtual_address)
{
    asm volatile("invlpg (%0)" : : "r" ((void *)(uintptr_t)virtual_address)
                 : "memory");
}

static void reload_page_directory(void)
{
    uint32_t page_directory = (uint32_t)(uintptr_t)initial_page_directory;

    asm volatile("mov %0, %%cr3" : : "r" (page_directory) : "memory");
}

void vmm_init(void)
{
    print("[VMM] Initializing Virtual Memory Management (VMM) subsystem...\n");
    paging_init();
    print("[VMM] subsystem initialized successfully.\n");
}

/*
 * Identity-map the first 4 MiB.  This covers the kernel, paging structures,
 * VGA text memory, and the low pages initially returned by the PMM.  The last
 * directory entry maps the directory itself, exposing page tables at
 * 0xFFC00000 after paging is enabled.
 */
void paging_init(void)
{
    uint32_t index;
    uint32_t cr0;

    if (paging_enabled)
        return;

    for (index = 0; index < PAGE_ENTRIES; ++index) {
        initial_page_directory[index] = 0;
        initial_page_table[index] = (index * PAGE_SIZE) |
                                    PAGE_FLAG_PRESENT |
                                    PAGE_FLAG_WRITABLE;
    }

    initial_page_directory[0] =
        ((uint32_t)(uintptr_t)initial_page_table & PAGE_ADDRESS_MASK) |
        PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE;
    initial_page_directory[RECURSIVE_DIRECTORY] =
        ((uint32_t)(uintptr_t)initial_page_directory & PAGE_ADDRESS_MASK) |
        PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE;

    /* Physical address zero is PMM's allocation-failure sentinel. */
    pmm_reserve_region(0, PAGE_SIZE);

    reload_page_directory();
    asm volatile("mov %%cr0, %0" : "=r" (cr0));
    cr0 |= 0x80000000U;
    asm volatile("mov %0, %%cr0" : : "r" (cr0) : "memory");
    paging_enabled = 1;

    print("[PAGING] Identity-mapped first 4 MiB.\n");
}

void map_page(uint64_t virtual_address, uint64_t physical_address,
              uint64_t flags)
{
    uint32_t virtual_page;
    uint32_t physical_page;
    uint32_t directory_index;
    uint32_t table_index;
    uint32_t *directory;
    uint32_t *table;
    uint64_t table_physical;

    if (!paging_enabled || !page_aligned_address(virtual_address) ||
        !page_aligned_address(physical_address) ||
        (flags & ~(uint64_t)PAGE_FLAG_MASK) != 0)
        return;

    virtual_page = (uint32_t)virtual_address;
    physical_page = (uint32_t)physical_address;
    directory_index = virtual_page >> 22;
    table_index = (virtual_page >> 12) & 0x3FFU;
    directory = active_page_directory();

    /* The recursive slot is owned by the VMM. */
    if (directory_index == RECURSIVE_DIRECTORY)
        return;

    table = active_page_table(directory_index);
    if (table == 0) {
        table_physical = pmm_alloc_page();
        if (table_physical == 0 || !page_aligned_address(table_physical))
            return;

        directory[directory_index] =
            ((uint32_t)table_physical & PAGE_ADDRESS_MASK) |
            PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE;
        reload_page_directory();

        table = active_page_table(directory_index);
        for (uint32_t index = 0; index < PAGE_ENTRIES; ++index)
            table[index] = 0;
    }

    table[table_index] = physical_page |
                         ((uint32_t)flags & PAGE_FLAG_MASK) |
                         PAGE_FLAG_PRESENT;
    flush_tlb_page(virtual_page);
}

void unmap_page(uint64_t virtual_address)
{
    uint32_t virtual_page;
    uint32_t directory_index;
    uint32_t table_index;
    uint32_t *table;

    if (!paging_enabled || !page_aligned_address(virtual_address))
        return;

    virtual_page = (uint32_t)virtual_address;
    directory_index = virtual_page >> 22;
    table_index = (virtual_page >> 12) & 0x3FFU;

    if (directory_index == RECURSIVE_DIRECTORY)
        return;

    table = active_page_table(directory_index);
    if (table == 0)
        return;

    table[table_index] = 0;
    flush_tlb_page(virtual_page);
}

uint64_t *get_page_table(uint64_t virtual_address)
{
    uint32_t directory_index;

    if ((virtual_address >> 32) != 0)
        return 0;

    directory_index = ((uint32_t)virtual_address >> 22) & 0x3FFU;
    return (uint64_t *)active_page_table(directory_index);
}

/*
 * Construct a caller-provided 4 MiB identity map.  Although the interface
 * uses uint64_t pointers, x86 page entries are 32 bits in this kernel.
 */
void get_all_pages(uint64_t *page_table, uint64_t *page_directory)
{
    uint32_t *table = (uint32_t *)page_table;
    uint32_t *directory = (uint32_t *)page_directory;
    uint32_t index;

    if (!page_buffer_is_valid(table) || !page_buffer_is_valid(directory) ||
        table == directory)
        return;

    for (index = 0; index < PAGE_ENTRIES; ++index)
        table[index] = (index * PAGE_SIZE) |
                       PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE;

    directory[0] = ((uint32_t)(uintptr_t)table & PAGE_ADDRESS_MASK) |
                   PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE;
}

void vmm_page_fault_handler(void)
{
    uint32_t fault_address;

    asm volatile("mov %%cr2, %0" : "=r" (fault_address));
    print("[VMM] Page fault at ");
    print_hex(fault_address);
    print("\n");

    /* Returning would re-execute the faulting instruction forever. */
    asm volatile("cli");
    for (;;)
        asm volatile("hlt");
}
