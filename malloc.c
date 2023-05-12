#include <unistd.h>
#include <stdint.h>
#include "malloc.h"
struct block_header_t {
    uint64_t                        size; /* size of usable data block */
    struct block_header_t*          next; /* pointer to next block. Only used when block is free*/
    struct block_header_t*          prev; /* pointer to previous block. Only used when block is free*/
};

/* Footer used to merge with previous block */
struct block_footer_t {
    uint64_t                        size; /* size of usable data block */
};

#define INIIAL_HEAP_SIZE            1024
#define ALLOC_SIZE                  1024
#define MINIMUM_ALLOC_SIZE          16
#define MINIMUM_BLOCK_SIZE          (sizeof(uint64_t) + MINIMUM_ALLOC_SIZE + sizeof(struct block_footer_t))

/* returns 1 if block is free, 0 otherwise */
static inline char is_free(struct block_header_t* block) {
    return ((block->size) & 1) == 1;
}

/* set block to free */
static inline void set_free(void* block) {
    struct block_header_t * m_block = (struct block_header_t*) block;
    m_block->size |= 1;   
}

/* set block as used */
static inline void set_used(void* block) {
    struct block_header_t * m_block = (struct block_header_t*) block;
    m_block->size &= -2;
}

/* returns size of block */
static inline uint64_t get_size(void* block) {
    struct block_header_t * m_block = (struct block_header_t*) block;
    return m_block->size & -2;
}

static char* heap =                 -1; /* starting address of heap*/
static char* heap_end =             -1; /* end address of heap */

static struct block_header_t*       last =         NULL; /* last block allocated */
static struct block_header_t*       first =        NULL; /* first block allocated */
static struct block_header_t*       free_list =    NULL; /* list of free blocks */

/* returns footer given header */
static inline struct block_footer_t* get_footer_from_header(struct block_header_t* header) {
    return (struct block_footer_t*)((char*)header + get_size(header) + sizeof(uint64_t));
}

/* returns header given footer */
static inline struct block_header_t* get_header_from_footer(struct block_footer_t* footer) {
    return (struct block_header_t*)((char*)footer - get_size(footer) - sizeof(uint64_t));
}

/* returns address of block given address of data */
static inline char* head_addr(char* data) {
    return data - sizeof(uint64_t);
}

/* returns address of data in block */
static inline char* data_addr(struct block_header_t* m_block) {
    return (char*) m_block + sizeof(uint64_t);
}

/* next available address to allocate */
static struct block_header_t* next_available_block() {
    if (last == NULL) {
        return heap;
    } else {
        return (char*)last + sizeof(uint64_t) + get_size(last) + sizeof(struct block_footer_t);
    }
}

/* round size to nearest multiple of 8 for alignment */
static inline unsigned int round(uint64_t size) {
    return ((size + 7) & (-8));
}

/* Wrapper for sbrk() */
static char Sbrk() {
    void* returned_addr = sbrk(ALLOC_SIZE);
    if (returned_addr != -1) {
        heap_end = heap_end + ALLOC_SIZE;
        return 0;
    } else {
        return 1;
    }
}

/* Add block to free list */
static void add_to_free_list(struct block_header_t* block) {
    block->next = free_list;
    block->prev = NULL;
    if (free_list != NULL) {
        free_list->prev = block;
    }
    free_list = block;
    set_free(block);
    set_free(get_footer_from_header(block));
}

/* Remove block from free list */
static void remove_from_free_list(struct block_header_t* block) {
    if (block->next != NULL) {
        block->next->prev = block->prev;
    }
    if (block->prev != NULL) {
        block->prev->next = block->next;
    }
    if (block == free_list) {
        free_list = block->next;
    }
    set_used(block);
    set_used(get_footer_from_header(block));
}

/* Called once to initialize heap */
static void init() {
    if (heap == -1) {
        heap = (char*) sbrk(0);
        void* returned_addr = sbrk(INIIAL_HEAP_SIZE);
        if (returned_addr == -1) {
            heap = -1;
            heap_end = -1;
        } else {
            heap_end = heap + INIIAL_HEAP_SIZE;
        }
    }
}

void* Malloc(uint64_t size) {
    if (heap == -1) {
        init();
    }
    if (heap == -1 || heap_end == -1) {
        return NULL;
    }
    if (size < MINIMUM_ALLOC_SIZE) {
        size = MINIMUM_ALLOC_SIZE;
    }
    size = round(size);
    // Find block in free list
    struct block_header_t* m_block = free_list;
    while (m_block != NULL) {
        if (get_size(m_block) >= size) { // Found block
            // Partition the block if remaining space is large enough
            uint64_t total_size = get_size(m_block) + sizeof(struct block_header_t) + sizeof(struct block_footer_t);
            uint64_t size_needed = size + sizeof(uint64_t) + sizeof(struct block_footer_t);
            if (total_size - size_needed >= MINIMUM_BLOCK_SIZE) {
                struct block_header_t* new_block = ((char*)m_block) + sizeof(uint64_t) + size + sizeof(struct block_footer_t);
                new_block->size = total_size - size_needed - sizeof(struct block_footer_t) - sizeof(uint64_t);
                add_to_free_list(new_block);
                set_free(new_block);
                set_free(get_footer_from_header(new_block));
            }
            m_block->size = size;
            set_used(m_block);
            set_used(get_footer_from_header(m_block));
            return data_addr(m_block);
        }
        m_block = m_block->next;
    }
    struct block_header_t* next_block = next_available_block();
    while (((char*)next_block) + sizeof(uint64_t) + size + sizeof(struct block_footer_t) > heap_end) {
        if (Sbrk() == 1) {
            return NULL;
        }
    }
    next_block->size = size;
    set_used(next_block);
    set_used(get_footer_from_header(next_block));
    if (first == NULL) {
        first = next_block;
    }
    last = next_block;
    return data_addr(next_block);
}

void Free(void* p) {
    if (p == 0) {
        return;
    }
    char* ptr = p;
    struct block_header_t* m_block = head_addr(ptr);
    struct block_header_t* next_block = NULL;
    struct block_header_t* prev_block = NULL;
    if (m_block != first) {
        struct block_footer_t* prev_footer = (char*)m_block - sizeof(struct block_footer_t);
        prev_block = get_header_from_footer(prev_footer);
    }
    if (m_block != last) {
        next_block = (char*)m_block + sizeof(uint64_t) + get_size(m_block) + sizeof(struct block_footer_t);
    }
    if (next_block != NULL && is_free(next_block)
    && prev_block != NULL && is_free(prev_block)) {
        remove_from_free_list(next_block);
        remove_from_free_list(prev_block);
        if (next_block == last) {
            last = prev_block;
        }
        uint64_t total_size = get_size(prev_block) + get_size(m_block) + get_size(next_block) + 3 * sizeof(uint64_t) + 3 * sizeof(struct block_footer_t);
        uint64_t usable_size = total_size - sizeof(uint64_t) - sizeof(struct block_footer_t);
        prev_block->size = usable_size;
        set_free(prev_block);
        get_footer_from_header(prev_block)->size = usable_size;
        set_free(get_footer_from_header(prev_block));
        add_to_free_list(prev_block);
    } else if (prev_block != NULL && is_free(prev_block)) {
        remove_from_free_list(prev_block);
        if (m_block == last) {
            last = prev_block;
        }
        uint64_t total_size = get_size(prev_block) + get_size(m_block) + 2 * sizeof(uint64_t) + 2 * sizeof(struct block_footer_t);
        uint64_t usable_size = total_size - sizeof(uint64_t) - sizeof(struct block_footer_t);
        prev_block->size = usable_size;
        set_free(prev_block);
        get_footer_from_header(prev_block)->size = usable_size;
        set_free(get_footer_from_header(prev_block));
        add_to_free_list(prev_block);
    } else if (next_block != NULL && is_free(next_block)) {
        remove_from_free_list(next_block);
        if (next_block == last) {
            last = m_block;
        }
        uint64_t total_size = get_size(m_block) + get_size(next_block) + 2 * sizeof(uint64_t) + 2 * sizeof(struct block_footer_t);
        uint64_t usable_size = total_size - sizeof(uint64_t) - sizeof(struct block_footer_t);
        m_block->size = usable_size;
        set_free(m_block);
        get_footer_from_header(m_block)->size = usable_size;
        set_free(get_footer_from_header(m_block));
        add_to_free_list(m_block);
    } else {
        set_free(m_block);
        set_free(get_footer_from_header(m_block));
        add_to_free_list(m_block);
    }
}

#include <assert.h>
#include <stdio.h>
int main() {
    char* prev = -1;
    for (int i = 2; i < 200; i++) {
        char* ptr = Malloc(i << 3);
        if (prev != -1) {
            struct block_header_t* prev_block = head_addr(prev);
            uint64_t size_prev = get_size(prev_block);
            struct block_header_t* block = head_addr(ptr);
            if ((char*)prev_block + size_prev + sizeof(uint64_t) + sizeof(struct block_footer_t) != (char*)block) {
                printf("prev: %p, prev size: %lu, block: %p size: %lu \n", prev_block, size_prev, block, get_size(block));
            }
        }
        prev = ptr;
    }
    for (int i = 2; i < 200; i++) {
        char* ptr1 = Malloc(i << 3);
        Free(ptr1);
        char* ptr2 = Malloc(i << 3);
        Free(ptr2);
        if (ptr1 != ptr2) {
            printf("ptr1: %p, ptr2: %p \n", ptr1, ptr2);
        }
    }
}
