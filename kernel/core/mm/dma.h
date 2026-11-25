#ifndef DMA_H
#define DMA_H

#include <lib/std/stdint.h>

/*
 * The bootstrap page tables identity-map the first 1 GiB. DMA buffers are
 * kept in that range until a permanent kernel virtual mapping is available.
 */
#define DMA_MAX_PHYSICAL_ADDRESS (1ULL * 1024 * 1024 * 1024)

typedef struct {
    uint64_t physical_address;
    void *virtual_address;
    uint64_t page_count;
} dma_buffer_t;

/**
 * Allocate zeroed, physically contiguous DMA pages.
 *
 * @param page_count Number of 4096-byte pages to allocate.
 * @param buffer Output DMA buffer descriptor.
 * @return Non-zero on success, zero when the PMM has no suitable range.
 * @copyright See LICENSE.md.
 * @date 2026-11-10
 */
int dma_alloc_pages(uint64_t page_count, dma_buffer_t *buffer);

/**
 * Release a DMA buffer allocated by dma_alloc_pages.
 *
 * @param buffer DMA buffer descriptor to release.
 * @copyright See LICENSE.md.
 * @date 2026-11-10
 */
void dma_free_pages(dma_buffer_t *buffer);

#endif
