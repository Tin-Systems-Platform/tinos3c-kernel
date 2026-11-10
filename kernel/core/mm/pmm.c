#include "pmm.h"
#include <lib/std/stdio.h>

/*
 * Keep the bootstrap bitmap in the kernel image.  This tracks 64 GiB of
 * physical memory using 2 MiB of storage and, unlike the old 4 GiB limit,
 * does not truncate physical addresses to 32 bits.
 */
#define MAX_PHYSICAL_MEMORY (64ULL * 1024 * 1024 * 1024)
#define MAX_PAGES (MAX_PHYSICAL_MEMORY / PMM_PAGE_SIZE)
#define BITMAP_SIZE ((MAX_PAGES + 7) / 8)

static uint8_t bitmap[BITMAP_SIZE]
    __attribute__((section(".data.boot")));

static uint64_t total_pages __attribute__((section(".data.boot")));
static uint64_t free_pages __attribute__((section(".data.boot")));


/*
 * Mark a page as used.
 */
static void set_page(uint64_t page)
{
    bitmap[page / 8] |= (1 << (page % 8));
}


/*
 * Mark a page as free.
 */
static void clear_page(uint64_t page)
{
    bitmap[page / 8] &= ~(1 << (page % 8));
}


/*
 * Check whether a page is used.
 */
static int page_used(uint64_t page)
{
    return bitmap[page / 8] & (1 << (page % 8));
}


/*
 * Initialize the PMM.
 *
 * Everything starts as USED.
 */
void pmm_init(void)
{
    printf("[PMM] Initializing Physical Memory Manager...\n");
    for (uint64_t i = 0; i < BITMAP_SIZE; i++)
        bitmap[i] = 0xFF;

    total_pages = 0;
    free_pages = 0;
    printf("[PMM] Physical Memory Manager initialized.\n");
}


/*
 * Tell the PMM that a physical memory region is usable.
 */
void pmm_add_region(uint64_t base, uint64_t length)
{
    uint64_t start;
    uint64_t end;
    uint64_t pages_before = free_pages;

    start = (base + PMM_PAGE_SIZE - 1)
          & ~(PMM_PAGE_SIZE - 1);

    end = (base + length)
        & ~(PMM_PAGE_SIZE - 1);

    if (end <= start) {
        printf("[PMM] Skipping region (end <= start)\n");
        return;
    }

    for (uint64_t address = start;
         address < end;
         address += PMM_PAGE_SIZE)
    {
        uint64_t page = address / PMM_PAGE_SIZE;

        if (page >= MAX_PAGES) {
            printf("[PMM] Breaking at page ");
            printf("%u", page);
            printf(" (>= MAX_PAGES)\n");
            break;
        }

        if (page_used(page)) {
            clear_page(page);
            total_pages++;
            free_pages++;
        }
    }

    printf("[PMM] Added region with ");
    printf("%u", free_pages - pages_before);
    printf(" pages\n");
}


/*
 * Reserve a physical memory region.
 */
void pmm_reserve_region(uint64_t base, uint64_t length)
{
    uint64_t start;
    uint64_t end;

    start = base & ~(PMM_PAGE_SIZE - 1);

    end = (base + length + PMM_PAGE_SIZE - 1)
        & ~(PMM_PAGE_SIZE - 1);

    if (end <= start)
        return;

    for (uint64_t address = start;
         address < end;
         address += PMM_PAGE_SIZE)
    {
        uint64_t page = address / PMM_PAGE_SIZE;

        if (page >= MAX_PAGES)
            break;

        if (!page_used(page))
        {
            set_page(page);
            free_pages--;
        }
    }
}


/*
 * Allocate one physical page.
 *
 * Returns the physical address.
 */
uint64_t pmm_alloc_page(void)
{
    for (uint64_t page = 1;
         page < MAX_PAGES;
         page++)
    {
        if (!page_used(page))
        {
            set_page(page);

            free_pages--;

            return page * PMM_PAGE_SIZE;
        }
    }

    /*
     * No memory available.
     */
    return 0;
}

/**
 * Allocate a physically contiguous range of pages below a limit.
 *
 * @param page_count Number of pages to allocate.
 * @param max_address Exclusive upper physical-address limit.
 * @return Physical base address, or zero when no suitable range exists.
 * @date 2026-11-10
 */
uint64_t pmm_alloc_contiguous(uint64_t page_count, uint64_t max_address)
{
    uint64_t limit_page;
    uint64_t run_start = 0;
    uint64_t run_length = 0;

    if (page_count == 0 || page_count > MAX_PAGES ||
        max_address <= PMM_PAGE_SIZE)
        return 0;

    if (max_address > ~0ULL - (PMM_PAGE_SIZE - 1))
        limit_page = MAX_PAGES;
    else
        limit_page = (max_address + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    if (limit_page > MAX_PAGES)
        limit_page = MAX_PAGES;

    for (uint64_t page = 1; page < limit_page; ++page) {
        if (page_used(page)) {
            run_length = 0;
            continue;
        }

        if (run_length == 0)
            run_start = page;
        ++run_length;
        if (run_length < page_count)
            continue;

        for (uint64_t allocated = 0; allocated < page_count; ++allocated)
            set_page(run_start + allocated);
        free_pages -= page_count;
        return run_start * PMM_PAGE_SIZE;
    }

    return 0;
}

/**
 * Free a physically contiguous range of pages.
 *
 * The range is released only when every page is valid and currently used, so
 * a malformed or double-free request cannot partially alter PMM state.
 *
 * @param address Page-aligned physical base address.
 * @param page_count Number of pages in the range.
 * @date 2026-11-10
 */
void pmm_free_contiguous(uint64_t address, uint64_t page_count)
{
    uint64_t first_page;

    if (page_count == 0 || address % PMM_PAGE_SIZE != 0)
        return;

    first_page = address / PMM_PAGE_SIZE;
    if (first_page == 0 || first_page >= MAX_PAGES ||
        page_count > MAX_PAGES - first_page)
        return;

    for (uint64_t page = 0; page < page_count; ++page) {
        if (!page_used(first_page + page))
            return;
    }

    for (uint64_t page = 0; page < page_count; ++page)
        clear_page(first_page + page);
    free_pages += page_count;
}

/*
 * Free a physical page.
 */
void pmm_free_page(uint64_t address)
{
    uint64_t page;

    /*
     * Physical addresses must be page aligned.
     */
    if (address % PMM_PAGE_SIZE != 0)
        return;

    page = address / PMM_PAGE_SIZE;

    if (page >= MAX_PAGES)
        return;

    /*
     * Don't free something that's already free.
     */
    if (!page_used(page))
        return;

    clear_page(page);
    free_pages++;
}


/*
 * Return number of free pages.
 */
uint64_t pmm_get_free_pages(void)
{
    return free_pages;
}


/*
 * Return total number of pages tracked.
 */
uint64_t pmm_get_total_pages(void)
{
    return total_pages;
}

/*
 * Allocate one physical page strictly within the first 8 MiB (low memory).
 * Needed for early paging structures before a full direct map is live.
 */
uint64_t pmm_alloc_page_low(void)
{
    // 2048 sivua = tasan 8 MiB (olettaen että PMM_PAGE_SIZE on 4096)
    for (uint64_t page = 1; page < 2048; page++)
    {
        if (!page_used(page))
        {
            set_page(page);
            free_pages--;
            return page * PMM_PAGE_SIZE;
        }
    }

    /* No low memory available. Fallback to normal if this happens, 
       but for early boot this shouldn't fail. */
    return 0;
}
