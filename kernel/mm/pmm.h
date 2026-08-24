#ifndef PMM_H
#define PMM_H

#include <stdint.h>

#define PMM_PAGE_SIZE 4096
#define PMM_MAX_PHYSICAL_MEMORY (64ULL * 1024 * 1024 * 1024)

void pmm_init(void);

void pmm_add_region(uint64_t base, uint64_t length);
void pmm_reserve_region(uint64_t base, uint64_t length);

uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t address);

uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_total_pages(void);

#endif
