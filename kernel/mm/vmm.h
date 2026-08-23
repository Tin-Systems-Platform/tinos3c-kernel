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

void map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags);
void unmap_page(uint64_t virtual_address);
uint64_t* get_page_table(uint64_t virtual_address);
void get_all_pages(uint64_t* page_table, uint64_t* page_directory);


#endif