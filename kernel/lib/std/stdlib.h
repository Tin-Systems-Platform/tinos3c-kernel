#ifndef STDLIB_H
#define STDLIB_H

typedef unsigned int size_t;

void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t new_size);
void *calloc(size_t num, size_t size);

#endif