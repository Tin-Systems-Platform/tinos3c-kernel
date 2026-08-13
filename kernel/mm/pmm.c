#include "pmm.h"
#include <lib/std/stdio.h>

#define MAX_PHYSICAL_MEMORY (4ULL * 1024 * 1024 * 1024)
#define MAX_PAGES (MAX_PHYSICAL_MEMORY / PMM_PAGE_SIZE)
#define BITMAP_SIZE ((MAX_PAGES + 7) / 8)

static uint8_t bitmap[BITMAP_SIZE];

static uint64_t total_pages = 0;
static uint64_t free_pages = 0;


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
    print("Initializing Physical Memory Manager...\n");
    for (uint64_t i = 0; i < BITMAP_SIZE; i++)
        bitmap[i] = 0xFF;

    total_pages = MAX_PAGES;
    free_pages = 0;
    print("Physical Memory Manager initialized.\n");
}


/*
 * Tell the PMM that a physical memory region is usable.
 */
void pmm_add_region(uint64_t base, uint64_t length)
{
    uint64_t start;
    uint64_t end;

    /*
     * Align the beginning upwards.
     */
    start = (base + PMM_PAGE_SIZE - 1)
          & ~(PMM_PAGE_SIZE - 1);

    /*
     * Align the end downwards.
     */
    end = (base + length)
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

        if (page_used(page))
        {
            clear_page(page);
            free_pages++;
        }
    }
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
    for (uint64_t page = 0;
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