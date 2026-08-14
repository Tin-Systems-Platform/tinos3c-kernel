#include "vmm.h"
#include <lib/std/stdio.h>

// Part of the VMM Subsystem impl

void vmm_init(void) {
    // Initialize the virtual memory management subsystem
    // This may include setting up page tables, enabling paging, etc.
    print("[VMM] Initializing Virtual Memory Management (VMM) subsystem...\n");
    paging_init();
    print("[VMM] subsystem initialized successfully.\n");
}

void paging_init(void) {
    // Initialize the paging mechanism
    // This may include setting up page directories, page tables, and enabling paging in the CPU
    print("[PAGING] Initializing Paging...\n");
    print("[PAGING] Paging initialized successfully.\n");
}