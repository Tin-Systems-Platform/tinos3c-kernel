#include "stdlib.h"

/*
 * The pool is kept private to this translation unit.  Besides preventing
 * multiple definitions when stdlib.h is included elsewhere, the Block member
 * ensures that the byte pool has suitable alignment for its headers.
 */
static union {
    Block alignment;
    unsigned char bytes[POOL_SIZE];
} memory_pool;

static Block *free_list;
static int allocator_initialized;

#define ALLOCATION_ALIGNMENT ((size_t)sizeof(size_t))
#define SIZE_MAX ((size_t)-1)

static size_t align_size(size_t size) {
    size_t padding = ALLOCATION_ALIGNMENT - 1U;

    if (size > SIZE_MAX - padding) {
        return 0;
    }

    return (size + padding) & ~padding;
}

static void allocator_init(void) {
    if (allocator_initialized) {
        return;
    }

    free_list = (Block *)memory_pool.bytes;
    free_list->size = POOL_SIZE - sizeof(Block);
    free_list->free = 1;
    free_list->next = NULL;
    allocator_initialized = 1;
}

static void split_block(Block *block, size_t size) {
    Block *remainder;

    /* Keep a header and at least one aligned allocation unit in the tail. */
    if (block->size < size + sizeof(Block) + ALLOCATION_ALIGNMENT) {
        return;
    }

    remainder = (Block *)((unsigned char *)(block + 1) + size);
    remainder->size = block->size - size - sizeof(Block);
    remainder->free = 1;
    remainder->next = block->next;

    block->size = size;
    block->next = remainder;
}

static void coalesce_block(Block *block) {
    while (block->next != NULL && block->next->free) {
        block->size += sizeof(Block) + block->next->size;
        block->next = block->next->next;
    }
}

/*
 * Never derive a header from a caller-supplied address.  Only an exact
 * payload address from a live allocation is valid for free() or realloc().
 */
static Block *find_allocated_block(void *ptr) {
    Block *block;

    if (!allocator_initialized) {
        return NULL;
    }

    for (block = free_list; block != NULL; block = block->next) {
        if ((void *)(block + 1) == ptr) {
            return block->free ? NULL : block;
        }
    }

    return NULL;
}

void *malloc(size_t size) {
    Block *block;

    if (size == 0) {
        return NULL;
    }

    size = align_size(size);
    if (size == 0) {
        return NULL;
    }

    allocator_init();

    for (block = free_list; block != NULL; block = block->next) {
        if (block->free && block->size >= size) {
            split_block(block, size);
            block->free = 0;
            return (void *)(block + 1);
        }
    }

    return NULL;
}

void free(void *ptr) {
    Block *block;

    if (ptr == NULL) {
        return;
    }

    block = find_allocated_block(ptr);
    if (block == NULL) {
        return;
    }

    block->free = 1;
    coalesce_block(block);

    /* A preceding free block can now absorb this one as well. */
    for (block = free_list; block != NULL && block->next != NULL; block = block->next) {
        if (block->free && block->next->free) {
            coalesce_block(block);
            break;
        }
    }
}

void *realloc(void *ptr, size_t new_size) {
    Block *block;
    Block *next;
    size_t old_size;
    size_t copy_size;
    unsigned char *source;
    unsigned char *destination;
    void *new_ptr;

    if (ptr == NULL) {
        return malloc(new_size);
    }

    if (new_size == 0) {
        free(ptr);
        return NULL;
    }

    new_size = align_size(new_size);
    if (new_size == 0) {
        return NULL;
    }

    block = find_allocated_block(ptr);
    if (block == NULL) {
        return NULL;
    }

    old_size = block->size;

    if (new_size <= old_size) {
        split_block(block, new_size);
        return ptr;
    }

    next = block->next;
    if (next != NULL && next->free &&
        old_size + sizeof(Block) + next->size >= new_size) {
        block->size = old_size + sizeof(Block) + next->size;
        block->next = next->next;
        split_block(block, new_size);
        return ptr;
    }

    new_ptr = malloc(new_size);
    if (new_ptr == NULL) {
        return NULL;
    }

    copy_size = old_size < new_size ? old_size : new_size;
    source = (unsigned char *)ptr;
    destination = (unsigned char *)new_ptr;
    while (copy_size-- != 0) {
        *destination++ = *source++;
    }

    free(ptr);
    return new_ptr;

}

void *calloc(size_t num, size_t size) {
    size_t total_size;
    size_t index;
    unsigned char *memory;

    if (num == 0 || size == 0) {
        return NULL;
    }

    if (num > SIZE_MAX / size) {
        return NULL;
    }

    total_size = num * size;
    memory = (unsigned char *)malloc(total_size);
    if (memory == NULL) {
        return NULL;
    }

    for (index = 0; index < total_size; ++index) {
        memory[index] = 0;
    }

    return memory;

}
