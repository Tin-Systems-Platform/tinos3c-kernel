#include "string.h"

//memset impl
void* memset(void* dest, int val, uint32_t len) {
    unsigned char* ptr = (unsigned char*)dest;
    while (len-- > 0) {
        *ptr++ = (unsigned char)val;
    }
    return dest;
}