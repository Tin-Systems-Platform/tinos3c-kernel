#ifndef PMM_H
#define PMM_H

#include <stdint.h>

#define PMM_PAGE_SIZE 4096
#define PMM_MAX_PHYSICAL_MEMORY (64ULL * 1024 * 1024 * 1024)

void pmm_init(void);

void pmm_add_region(uint64_t base, uint64_t length);
void pmm_reserve_region(uint64_t base, uint64_t length);

uint64_t pmm_alloc_page(void);
/**
 * Allocate physically contiguous pages below a physical address limit.
 *
 * @param page_count Number of pages to allocate.
 * @param max_address Exclusive upper physical-address limit.
 * @return Physical base address, or zero when no suitable range exists.
 * @copyright See LICENSE.md.
 * @date 2026-11-10
 */
uint64_t pmm_alloc_contiguous(uint64_t page_count, uint64_t max_address);

/**
 * Free a physically contiguous range previously allocated by the PMM.
 *
 * @param address Page-aligned physical base address.
 * @param page_count Number of pages in the range.
 * @copyright See LICENSE.md.
 * @date 2026-11-10
 */
void pmm_free_contiguous(uint64_t address, uint64_t page_count);

void pmm_free_page(uint64_t address);

uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_total_pages(void);

uint64_t pmm_alloc_page_low(void);
#endif
