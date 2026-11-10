#include "blkdev.h"
#include <lib/std/stdio.h>

#include "../../lib/std/stdio.h"


/*
 * @brief Initializes the kernel Block Device driver on kernel startup
 * @author randomusert
 * @date 2026-10-08
 */
void blkdev_init(void) {
    printf("BLKDEV: Block device driver initialization started\n");

    printf("BLKDEV: Block device driver initialization complete\n");
}

void enable_interupts_blockdev(void) {
    printf("BLKDEV: Interrupts for block devices are being initialized. Please wait\n");

    
}