#ifndef VMM_H
#define VMM_H

#include <lib/std/stdint.h>

/*
* This file is part of the Memory Management for tinosc kernel.
* 
* This file contains the definitions and declarations for the virtual memory management (VMM) subsystem.
*/

void vmm_init(void);
void vmm_page_fault_handler(void);

/**
 * @brief The Paging logic.
 * @author randomusert
 * @date 2026-08-14
 * 
 */
void paging_init(void);

/* Both addresses must be 4 KiB aligned.  Physical addresses may be 64-bit. */
void map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags);
void unmap_page(uint64_t virtual_address);
uint64_t* get_page_table(uint64_t virtual_address);

/*
 * page_table and page_directory must be distinct, non-NULL, 4 KiB-aligned
 * buffers.  Each must provide at least one full 4 KiB page of writable memory.
 * Page entries are 64-bit values on the x86_64 paging implementation.
 */
void get_all_pages(uint64_t* page_table, uint64_t* page_directory);


#endif
