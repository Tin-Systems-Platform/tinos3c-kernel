#ifndef PCI_MMIO_H
#define PCI_MMIO_H

#include <stdint.h>

/**
 * Map a PCI MMIO physical range into kernel virtual memory.
 * @param physical_address Physical start address.
 * @param length Number of bytes to map.
 * @return Mapped virtual address, or NULL on invalid input/exhaustion.
 * @date 2026-10-07
 */
void *pci_mmio_map(uint64_t physical_address, uint64_t length);

/**
 * Remove a PCI MMIO mapping.
 * @param virtual_address Address returned by pci_mmio_map.
 * @param length Number of bytes passed to pci_mmio_map.
 * @date 2026-10-07
 */
void pci_mmio_unmap(void *virtual_address, uint64_t length);

#endif
