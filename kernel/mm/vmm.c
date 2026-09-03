#include "vmm.h"

#include <lib/std/stdio.h>
#include <mm/pmm.h>

#define PAGE_SIZE                 0x1000ULL
#define PAGE_ENTRIES              512U
#define PAGE_ADDRESS_MASK         0x000FFFFFFFFFF000ULL
#define PAGE_FLAG_PRESENT         0x001ULL
#define PAGE_FLAG_WRITABLE        0x002ULL
#define PAGE_FLAG_MASK            0xFFFULL
#define BOOTSTRAP_IDENTITY_LIMIT  (1024ULL * 1024ULL * 1024ULL)

/* Accessed through the bootstrap identity map until a physical direct map exists. */
static uint64_t initial_pml4[PAGE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t initial_pdpt[PAGE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t initial_pd[PAGE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint64_t initial_pt[128][PAGE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static int paging_enabled;

static int page_aligned_address(uint64_t address)
{
    return (address & (PAGE_SIZE - 1ULL)) == 0;
}

static int canonical_address(uint64_t address)
{
    uint64_t upper = address >> 48;
    return upper == 0 || upper == 0xFFFFULL;
}

static void zero_page(uint64_t *page)
{
    uint16_t index;
    for (index = 0; index < PAGE_ENTRIES; ++index)
        page[index] = 0;
}

static uint64_t *physical_to_bootstrap_virtual(uint64_t physical_address)
{
    if (physical_address >= BOOTSTRAP_IDENTITY_LIMIT ||
        !page_aligned_address(physical_address))
        return 0;
    return (uint64_t *)(uintptr_t)physical_address;
}

static uint64_t *entry_table(uint64_t entry)
{
    if ((entry & PAGE_FLAG_PRESENT) == 0)
        return 0;
    return physical_to_bootstrap_virtual(entry & PAGE_ADDRESS_MASK);
}

static uint64_t table_entry(uint64_t physical_address)
{
    return (physical_address & PAGE_ADDRESS_MASK) |
           PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE;
}

static uint64_t *allocate_table(void)
{
    uint64_t physical_address = pmm_alloc_page_low(); 
    uint64_t *table;

    if (physical_address == 0)
        return 0;
    table = physical_to_bootstrap_virtual(physical_address);
    if (table == 0) {
        pmm_free_page(physical_address);
        return 0;
    }
    zero_page(table);
    return table;
}

static uint64_t *next_table(uint64_t *table, uint16_t index)
{
    uint64_t *next = entry_table(table[index]);

    if (next != 0)
        return next;
    next = allocate_table();
    if (next == 0)
        return 0;
    table[index] = table_entry((uint64_t)(uintptr_t)next);
    return next;
}

static void flush_tlb_page(uint64_t virtual_address)
{
    asm volatile("invlpg (%0)" : : "r" ((void *)(uintptr_t)virtual_address)
                 : "memory");
}

static void reload_page_directory(void)
{
    uint64_t pml4_physical = (uint64_t)(uintptr_t)initial_pml4;
    asm volatile("mov %0, %%cr3" : : "r" (pml4_physical) : "memory");
}

void vmm_init(void)
{
    printf("[VMM] Initializing x86_64 Virtual Memory Management subsystem...\n");
    paging_init();
    printf("[VMM] subsystem initialized successfully.\n");
}

/* Build a four-level identity map for the first 8 MiB. */
void paging_init(void)
{
    uint16_t index;

    if (paging_enabled)
        return;
        
    zero_page(initial_pml4);
    zero_page(initial_pdpt);
    zero_page(initial_pd);

    // PML4[0] osoittaa PDPT-taulukkoon (Present + Writable)
    initial_pml4[0] = (uint64_t)(uintptr_t)initial_pdpt | 0x003ULL;
    
    // PDPT[0] osoittaa PD-taulukkoon (Present + Writable)
    initial_pdpt[0] = (uint64_t)(uintptr_t)initial_pd | 0x003ULL;

    // Täytetään PD-taulukko 2 MiB suurilla sivuilla (512 entryä * 2 MiB = 1 GiB)
    for (index = 0; index < PAGE_ENTRIES; ++index) {
        // 0x080 on bit 7 (Page Size, PS). Se kertoo suorittimelle, 
        // että tämä sivu on suoraan 2 MiB kokoinen, eikä viittaa PT-taulukkoon!
        initial_pd[index] = ((uint64_t)index * 2ULL * 1024ULL * 1024ULL) | 
                            0x083ULL; // Present + Writable + Page Size (0x80)
    }

    reload_page_directory();
    paging_enabled = 1;
    printf("[PAGING] Massive 1 GiB Identity Map installed using 2 MiB Large Pages.\n");
}

void map_page(uint64_t virtual_address, uint64_t physical_address,
              uint64_t flags)
{
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint16_t pml4_index;
    uint16_t pdpt_index;
    uint16_t pd_index;
    uint16_t pt_index;

    if (!paging_enabled || !canonical_address(virtual_address) ||
        !page_aligned_address(virtual_address) ||
        !page_aligned_address(physical_address) ||
        (physical_address & ~PAGE_ADDRESS_MASK) != 0 ||
        (flags & ~PAGE_FLAG_MASK) != 0)
        return;

    pml4_index = (uint16_t)((virtual_address >> 39) & 0x1FFULL);
    pdpt_index = (uint16_t)((virtual_address >> 30) & 0x1FFULL);
    pd_index = (uint16_t)((virtual_address >> 21) & 0x1FFULL);
    pt_index = (uint16_t)((virtual_address >> 12) & 0x1FFULL);
    pdpt = next_table(initial_pml4, pml4_index);
    pd = pdpt == 0 ? 0 : next_table(pdpt, pdpt_index);
    pt = pd == 0 ? 0 : next_table(pd, pd_index);
    if (pt == 0)
        return;

    pt[pt_index] = (physical_address & PAGE_ADDRESS_MASK) |
                   (flags & PAGE_FLAG_MASK) | PAGE_FLAG_PRESENT;
    flush_tlb_page(virtual_address);
}

void unmap_page(uint64_t virtual_address)
{
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint16_t pml4_index;
    uint16_t pdpt_index;
    uint16_t pd_index;
    uint16_t pt_index;

    if (!paging_enabled || !canonical_address(virtual_address) ||
        !page_aligned_address(virtual_address))
        return;
    pml4_index = (uint16_t)((virtual_address >> 39) & 0x1FFULL);
    pdpt_index = (uint16_t)((virtual_address >> 30) & 0x1FFULL);
    pd_index = (uint16_t)((virtual_address >> 21) & 0x1FFULL);
    pt_index = (uint16_t)((virtual_address >> 12) & 0x1FFULL);
    pdpt = entry_table(initial_pml4[pml4_index]);
    pd = pdpt == 0 ? 0 : entry_table(pdpt[pdpt_index]);
    pt = pd == 0 ? 0 : entry_table(pd[pd_index]);
    if (pt == 0)
        return;
    pt[pt_index] = 0;
    flush_tlb_page(virtual_address);
}

uint64_t *get_page_table(uint64_t virtual_address)
{
    uint64_t *pdpt;
    uint64_t *pd;
    uint16_t pml4_index;
    uint16_t pdpt_index;
    uint16_t pd_index;

    if (!paging_enabled || !canonical_address(virtual_address))
        return 0;
    pml4_index = (uint16_t)((virtual_address >> 39) & 0x1FFULL);
    pdpt_index = (uint16_t)((virtual_address >> 30) & 0x1FFULL);
    pd_index = (uint16_t)((virtual_address >> 21) & 0x1FFULL);
    pdpt = entry_table(initial_pml4[pml4_index]);
    pd = pdpt == 0 ? 0 : entry_table(pdpt[pdpt_index]);
    return pd == 0 ? 0 : entry_table(pd[pd_index]);
}

void get_all_pages(uint64_t *page_table, uint64_t *page_directory)
{
    uint16_t index;

    if (page_table == 0 || page_directory == 0 || page_table == page_directory ||
        !page_aligned_address((uint64_t)(uintptr_t)page_table) ||
        !page_aligned_address((uint64_t)(uintptr_t)page_directory))
        return;
    for (index = 0; index < PAGE_ENTRIES; ++index)
        page_table[index] = ((uint64_t)index * PAGE_SIZE) |
                            PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE;
    zero_page(page_directory);
    page_directory[0] = table_entry((uint64_t)(uintptr_t)page_table);
}

void vmm_page_fault_handler(void)
{
    uint64_t fault_address;
    asm volatile("mov %%cr2, %0" : "=r" (fault_address));
    printf("[VMM] Page fault at %x\n", fault_address);
    asm volatile("cli");
    for (;;)
        asm volatile("hlt");
}
