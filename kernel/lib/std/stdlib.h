#ifndef STDLIB_H
#define STDLIB_H

typedef unsigned int size_t;

/* Null pointer constant. */
#define NULL ((void *)0)

typedef struct Block {
    size_t size;         // Size of the block
    int free;            // 1 if free, 0 if allocated
    struct Block *next;  // Pointer to the next block
} Block;

#define POOL_SIZE (1024U * 1024U) /* 1 MiB */

void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t new_size);
void *calloc(size_t num, size_t size);

#endif
