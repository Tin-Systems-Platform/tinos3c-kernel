#include "dma.h"

#include "pmm.h"

/**
 * Allocate zeroed, physically contiguous DMA pages.
 *
 * The returned virtual address is valid because DMA allocations are limited
 * to the bootstrap identity map. This also keeps the physical address
 * representable by devices that only support 32-bit bus addresses.
 *
 * @param page_count Number of 4096-byte pages to allocate.
 * @param buffer Output DMA buffer descriptor.
 * @return Non-zero on success, zero on invalid input or allocation failure.
 * @copyright See LICENSE.md.
 * @date 2026-11-10
 */
int dma_alloc_pages(uint64_t page_count, dma_buffer_t *buffer)
{
    uint64_t physical_address;
    uint64_t byte_count;
    uint8_t *memory;

    if (page_count == 0 || buffer == 0 ||
        page_count > (~0ULL / PMM_PAGE_SIZE))
        return 0;

    byte_count = page_count * PMM_PAGE_SIZE;
    physical_address = pmm_alloc_contiguous(
        page_count, DMA_MAX_PHYSICAL_ADDRESS);
    if (physical_address == 0)
        return 0;

    memory = (uint8_t *)(uintptr_t)physical_address;
    for (uint64_t index = 0; index < byte_count; ++index)
        memory[index] = 0;

    buffer->physical_address = physical_address;
    buffer->virtual_address = memory;
    buffer->page_count = page_count;
    return 1;
}

/**
 * Release a DMA buffer allocated by dma_alloc_pages.
 *
 * @param buffer DMA buffer descriptor to release.
 * @copyright See LICENSE.md.
 * @date 2026-11-10
 */
void dma_free_pages(dma_buffer_t *buffer)
{
    if (buffer == 0 || buffer->physical_address == 0 ||
        buffer->page_count == 0)
        return;

    pmm_free_contiguous(buffer->physical_address, buffer->page_count);
    buffer->physical_address = 0;
    buffer->virtual_address = 0;
    buffer->page_count = 0;
}
