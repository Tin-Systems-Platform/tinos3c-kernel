#include "crash.h"
#include <lib/sys/panic.h>

void crash() {
    panic("Crash test", "INTENTIONAL_UNDOCUMENTED_COMMAND_EXECUTED");
}