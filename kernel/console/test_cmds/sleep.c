#include "sleep.h"
#include <errno.h>
#include <lib/std/stdio.h>



int system_shutdown(void) {
    /*
     * This helper essentially performs a two stage shutdown.
     * 
     * Stage 1:
     * - Runs the \_PTS & \_SST methods, if they exist
     * - Fetches the \_S5 and \_S0 values to make system wake possible later on
     *
     * Stage 2:
     * Actually enter the sleep state by writing to the hardware registers with
     * the values from stage 1. This also disables runtime events and enables
     * only those that are needed for wake.
     */
    uacpi_status ret = uacpi_enter_sleep_state_simple(UACPI_SLEEP_STATE_S5);
    if (uacpi_unlikely_error(ret)) {
        printf("failed to enter sleep: %s", uacpi_status_to_string(ret));
        return -EIO;
    }

    /*
     * Technically unreachable code, but leave it here to prevent the compiler
     * from complaining.
     */
    return 0;
}