#ifndef VMM_H
#define VMM_H

/*
* This file is part of the Memory Management for tinosc kernel.
* 
* This file contains the definitions and declarations for the virtual memory management (VMM) subsystem.
*/

void vmm_init(void);

/**
 * @brief The Paging logic.
 * @author randomusert
 * @date 2026-08-14
 * 
 */
void paging_init(void);

#endif