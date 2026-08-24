#ifndef STDINT_H
#define STDINT_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;

#if defined(__x86_64__)
typedef unsigned long      uint64_t;
typedef unsigned long      size_t;
typedef unsigned long      uintptr_t;

typedef signed long        int64_t;
#else
typedef unsigned long long uint64_t;
typedef unsigned int       size_t;
typedef unsigned int       uintptr_t;

typedef signed long long   int64_t;
#endif

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;

#endif