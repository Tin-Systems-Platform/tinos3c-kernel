#include "pci_mmio.h"

#include <mm/vmm.h>

#define PCI_MMIO_PAGE_SIZE 0x1000ULL
#define PCI_IDENTITY_LIMIT 0x40000000ULL
#define PCI_MMIO_VIRTUAL_BASE 0xFFFF800000000000ULL
#define PCI_MMIO_VIRTUAL_LIMIT 0xFFFF900000000000ULL

static uint64_t next_virtual_address = PCI_MMIO_VIRTUAL_BASE;

static uint64_t align_up(uint64_t address)
{
    if (address > ~0ULL - (PCI_MMIO_PAGE_SIZE - 1ULL))
        return 0;
    return (address + PCI_MMIO_PAGE_SIZE - 1ULL) &
           ~(PCI_MMIO_PAGE_SIZE - 1ULL);
}

/**
 * Verify that a virtual page contains the requested physical page.
 *
 * @param virtual_address Page-aligned virtual address.
 * @param physical_address Page-aligned physical address.
 * @return Non-zero when the page-table entry matches.
 * @date 2026-10-10
 */
static int page_mapping_matches(uint64_t virtual_address,
                                uint64_t physical_address)
{
    uint64_t *page_table = get_page_table(virtual_address);
    uint16_t page_index;

    if (page_table == 0)
        return 0;
    page_index = (uint16_t)((virtual_address >> 12) & 0x1FFULL);
    return (page_table[page_index] & 0x000FFFFFFFFFF000ULL) ==
           physical_address;
}

void *pci_mmio_map(uint64_t physical_address, uint64_t length)
{
    uint64_t physical_start;
    uint64_t physical_end;
    uint64_t map_length;
    uint64_t virtual_start;
    uint64_t offset;
    uint64_t address;

    if (length == 0 ||
        physical_address > 0x000FFFFFFFFFF000ULL ||
        physical_address > ~0ULL - length)
        return 0;
    offset = physical_address & (PCI_MMIO_PAGE_SIZE - 1ULL);
    physical_start = physical_address & ~(PCI_MMIO_PAGE_SIZE - 1ULL);
    physical_end = align_up(physical_address + length);
    if (physical_end == 0 || physical_end < physical_start)
        return 0;
    if (physical_address < PCI_IDENTITY_LIMIT &&
        length <= PCI_IDENTITY_LIMIT - physical_address)
        return (void *)(uintptr_t)physical_address;
    map_length = physical_end - physical_start;
    if (next_virtual_address > PCI_MMIO_VIRTUAL_LIMIT ||
        map_length > PCI_MMIO_VIRTUAL_LIMIT - next_virtual_address)
        return 0;

    virtual_start = next_virtual_address;
    next_virtual_address += map_length;
    for (address = 0; address < map_length; address += PCI_MMIO_PAGE_SIZE) {
        map_page(virtual_start + address, physical_start + address, 0x003);
        if (!page_mapping_matches(virtual_start + address,
                                  physical_start + address)) {
            while (address != 0) {
                address -= PCI_MMIO_PAGE_SIZE;
                unmap_page(virtual_start + address);
            }
            return 0;
        }
    }
    return (void *)(uintptr_t)(virtual_start + offset);
}

void pci_mmio_unmap(void *virtual_address, uint64_t length)
{
    uint64_t start;
    uint64_t end;
    uint64_t address;

    if (virtual_address == 0 || length == 0)
        return;
    start = (uint64_t)(uintptr_t)virtual_address &
            ~(PCI_MMIO_PAGE_SIZE - 1ULL);
    if (start < PCI_MMIO_VIRTUAL_BASE ||
        (uint64_t)(uintptr_t)virtual_address > ~0ULL - length)
        return;
    end = align_up((uint64_t)(uintptr_t)virtual_address + length);
    if (end == 0 || end < start)
        return;
    for (address = start; address < end; address += PCI_MMIO_PAGE_SIZE)
        unmap_page(address);
}
