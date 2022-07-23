
#define _DEFAULT_SOURCE /** Unlock brk() and sbrk() in glibc */

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

void * __malloc(size_t size);
void __free(void * memory);

static void * heap_start;
static void * program_break;

#define __safe_sbrk(size) \
if (sbrk(size) == NULL) { \
    return NULL;\
} \
program_break += size \

#define VOID_PTR(p) ((void *) p)
#define max(a,b) ((a) > (b) ? a : b)

// Structs holding memory blocks' metadata
typedef struct __free_block_header __free_block_header;
typedef struct __alloc_block_header __alloc_block_header;

struct __free_block_header {
    //
    // NOTE: The order of the fields is CRUCIAL.
    //
    size_t length;  // Length of memory block body.
    __free_block_header * prev_free_block; // Next free block of heap memory closer to heap_start. NULL if first free block.
    __free_block_header * nxt_free_block; // Next free block of heap memory closer to program_break. NULL if last free block.
    __alloc_block_header * back_expansion_cand; // Pointer to a previous alloc block that might be merged with this block on a free() call. NULL if block has no horizon for expansion.
    __alloc_block_header * fwd_expansion_cand; // Pointer to an alloc block ahead that might be merged with this block on a free() call. NULL if block has no horizon for expansion.
};
#define __FREE_BLOCK_HEADER_SZ sizeof(__free_block_header)

struct __alloc_block_header {
    //
    // NOTE: The order of the fields is CRUCIAL.
    //
    size_t length;  // Length of memory block body.
    __free_block_header * back_merge_on_free; // Pointer to a previous free block where this allocated block should be merged when freed call. NULL if should not be merged with any block.
    __free_block_header * fwd_merge_on_free; // Pointer to a free block ahead where this allocated block should be merged when freed call. NULL if should not be merged with any block.
    __alloc_block_header * prev_neigh_alloc_block; // Pointer to a previous allocated block that is directly behind this one. NULL if there is no such block.
    __alloc_block_header * nxt_neigh_alloc_block; // Pointer to a forward allocated block that is directly in front of this one. NULL if there is no such block.
};
#define __ALLOC_BLOCK_HEADER_SZ sizeof(__alloc_block_header)

//
// Heap memory blocks should be able to be immediately converted into the other, therefore we need a minimum block size.
//
#define __MINIMUM_BLOCK_SZ max(__FREE_BLOCK_HEADER_SZ, __ALLOC_BLOCK_HEADER_SZ)

static __free_block_header * free_block_list = NULL;
static __alloc_block_header * last_alloc_block = NULL;

/**
 * __MALLOC HEAP LAYOUT (illustration):
 *                                      
 *                                         NULL
 *                                          ^
 *                                          |
 * heap_start ->     -------------------- <=|============
 *                  |   free block       |==|           |
 *                  |                    |======        |
 *                   --------------------      |        |
 *                  |   alloc block      |     |        |
 *                  |                    |     |        |
 *                   --------------------      |        |
 *                  |   alloc block      |     |        |
 *                  |                    |     |        |
 *                   -------------------- <=============|================
 *                  |   free block       |===============               |
 *                  |                    |=======  nxt_free_block       | 
 *                   --------------------       |                       |
 *                  |   alloc block      |      |                       |
 *                  |                    |      |                       |
 *                   --------------------       |                       |
 *                  |                    |      |                       |
 *                  |       ...          |      ...                     ...
 *                  |                    |      |                       |
 *                   --------------------       |                       |
 *                  |   alloc block      |      |                       |
 *                  |                    |      |                       |
 *                   -------------------- <======                       |
 *                  |   free block       |=============================== prev_free_block
 *                  |                    |===|
 * program_break ->  --------------------    |
 *                                           |
 *                                           v
 *                                          NULL
 * 
 * - Free and allocated blocks should be interchangeable, therefore all blocks have a minimum total size (data + metadata) of __MINIMUM_BLOCK_SZ, 
 *      determined by the maximum between a 0-length allocated block and a 0-length free block.
 *      In x86-64 architectures this should take 40 bytes of virtual memory space.
 * - prev_free_block and nxt_free_block link free memory blocks, creating an ordered double linked list.
 * - back_expansion_cand and fwd_expansion_cand link a free memory block to allocated memory blocks that can get merged into it when they're freed.
 *      Note that this means it points to the previous (next) allocated block or NULL if there is no such block or it exists but is not a direct neighbor.
 * - prev_neigh_alloc_block and nxt_neigh_alloc_block link neighboring allocated memory blocks.
 * - back_merge_on_free and fwd_merge_on_free link an allocated block to free memory blocks that it can be merged to when freed.
 *      Note that this means it points to the previous (next) free block or NULL if there is no such block or it exists but is not a direct neighbor.
 * 
 * In some way, if free/allocated blocks are seen as green/blue blocks, we are treating the links prev_free_block / nxt_free_block as 
 *      an ordered doubly linked list of green blocks and back_expansion_cand / fwd_expansion_cand / back_merge_on_free / fwd_merge_on_free as
 *      an ordered doubly linked list of consecutive alternating green/blue blocks.
 */

//
// NOTE: Our version of malloc is cheap and only increases the heap by the minimum necessary.
// This is less performatic cause it leads to more system calls but I'm just having fun.
// It also does not incorporate other optimizations like memory page alignment or funky free block picking.
//
// If size = 0, we still allocate a memory block with data length 0 that in practice occupies some memory due to metadata in the block header.
//
void * __malloc(size_t size) {
    if (__FREE_BLOCK_HEADER_SZ > __ALLOC_BLOCK_HEADER_SZ) {
        //
        // If size is not large enough to meet the minimum block size criteria, we allocate extra bytes, even though 
        //  the caller won't be aware of it and should not make use of it.
        //
        size = max(size, __FREE_BLOCK_HEADER_SZ - __ALLOC_BLOCK_HEADER_SZ);
    }

    size_t real_size = __ALLOC_BLOCK_HEADER_SZ + size; // Total bytes that hold the allocation metadata + data
    
    __alloc_block_header * new_alloc;

    if (free_block_list == NULL) {
        //
        // No free blocks in the heap.
        //  Expand the heap and use the memory to make a new allocated block.
        //
        new_alloc = program_break;
        __safe_sbrk(real_size);
        new_alloc->length = size;
        new_alloc->back_merge_on_free = NULL;
        new_alloc->fwd_merge_on_free = NULL;
        new_alloc->prev_neigh_alloc_block = last_alloc_block;
        new_alloc->nxt_neigh_alloc_block = NULL;

        if (last_alloc_block != NULL) {
            last_alloc_block->nxt_neigh_alloc_block = new_alloc;
        }
        last_alloc_block = new_alloc;
    } else {
        __free_block_header * search_ptr = free_block_list;
        __free_block_header * trailing_search_ptr = NULL;
        while (search_ptr != NULL) {
            if (__FREE_BLOCK_HEADER_SZ + search_ptr->length >= real_size) {
                // Found a slot!
                // NOTE: First-fit strategy.
                break;
            }
            trailing_search_ptr = search_ptr;
            search_ptr = search_ptr->nxt_free_block;
        }

        if (search_ptr == NULL) {
            //
            // No free block available for the required size. Expand heap by the minimum amount possible.
            //
            size_t size_to_expand = real_size;
            if (trailing_search_ptr->fwd_expansion_cand == NULL) {
                //
                // Because consecutive free blocks are always merged into one, fwd_expansion_cand will be NULL here
                //  if and only if the free block is the last block in the heap.
                //
                // In this case, we can combine the last free block with new memory received from pushing
                //  the program break. This way we can ask the kernel for less memory.
                //
                // Expand
                size_to_expand -=  __FREE_BLOCK_HEADER_SZ + trailing_search_ptr->length;
                __safe_sbrk(size_to_expand);
                // Allocate
                new_alloc = (__alloc_block_header *) trailing_search_ptr;
                // Update links
                if (trailing_search_ptr->prev_free_block != NULL) {
                    trailing_search_ptr->prev_free_block->nxt_free_block = NULL;
                } else {
                    free_block_list = NULL;
                }
                //
                // fwd_merge_on_free, prev_neigh_alloc_block and nxt_neigh_alloc_block are already filled due to struct alignment.
                //
                // new_alloc->fwd_merge_on_free = NULL;
                // new_alloc->prev_neigh_alloc_block = trailing_search_ptr->back_expansion_cand;
                // new_alloc->nxt_neigh_alloc_block = NULL;
            } else {
                // Allocate
                new_alloc = (__alloc_block_header *) program_break;
                // Expand
                __safe_sbrk(size_to_expand);
                // Update links
                new_alloc->fwd_merge_on_free = NULL;
                new_alloc->prev_neigh_alloc_block = last_alloc_block;
                new_alloc->nxt_neigh_alloc_block = NULL;
            }

            new_alloc->length = size;
            new_alloc->back_merge_on_free = NULL;
            
            // Update last allocated block
            if (last_alloc_block != NULL) {
                last_alloc_block->fwd_merge_on_free = NULL;
                last_alloc_block->nxt_neigh_alloc_block = new_alloc;
            }
            last_alloc_block = new_alloc;

        } else {
            //
            // Found a big enough free block in the list
            //
            if (__FREE_BLOCK_HEADER_SZ + search_ptr->length >= real_size + __MINIMUM_BLOCK_SZ){
                //
                // We can split the free block found into a smaller (and still useful) free block + allocated block.
                //
                // Split
                search_ptr->length -= real_size;
                // Allocate
                new_alloc = VOID_PTR(search_ptr) + __FREE_BLOCK_HEADER_SZ + search_ptr->length;
                new_alloc->length = size;
                // Update links
                new_alloc->back_merge_on_free = search_ptr;
                new_alloc->fwd_merge_on_free = NULL; // Not so obvious: either there is no next consecutive block, or the consecutive block is not a free one (cause neighboring free blocks are always merged).
                new_alloc->prev_neigh_alloc_block = NULL;
                new_alloc->nxt_neigh_alloc_block = search_ptr->fwd_expansion_cand;
                if (search_ptr->fwd_expansion_cand != NULL) {
                    search_ptr->fwd_expansion_cand->back_merge_on_free = NULL;
                    search_ptr->fwd_expansion_cand->prev_neigh_alloc_block = new_alloc;
                } else {
                    //
                    // This means the block found is coincidentally the last block in the heap.
                    //
                    last_alloc_block = new_alloc;
                }
                search_ptr->fwd_expansion_cand = new_alloc;
            } else {
                // 
                // Found block not big enough to split in two.
                // In this case we allocate the whole block (even though the caller can't use the extra bytes).
                //
                // Allocate
                new_alloc = (__alloc_block_header *) search_ptr;
                // Detach free block. Update list start if needed
                if (search_ptr->prev_free_block != NULL) {
                    search_ptr->prev_free_block->nxt_free_block = search_ptr->nxt_free_block;
                } else {
                    //
                    // Coincidentally block to be allocated is the first block of list.
                    //
                    free_block_list = search_ptr->nxt_free_block;
                }
                if (search_ptr->nxt_free_block != NULL) {
                    search_ptr->nxt_free_block->prev_free_block = search_ptr->prev_free_block;
                }
                if (search_ptr->back_expansion_cand != NULL) {
                    search_ptr->back_expansion_cand->fwd_merge_on_free = NULL;
                    search_ptr->back_expansion_cand->nxt_neigh_alloc_block = new_alloc;
                }
                if (search_ptr->fwd_expansion_cand != NULL) {
                    search_ptr->fwd_expansion_cand->back_merge_on_free = NULL;
                    search_ptr->fwd_expansion_cand->prev_neigh_alloc_block = new_alloc;
                } else {
                    //
                    // This means the block found is coincidentally the last block in the heap.
                    //
                    last_alloc_block = new_alloc;
                }
               
                // length is already filled due to struct alignment
                new_alloc->back_merge_on_free = NULL;
                new_alloc->fwd_merge_on_free = NULL;
                // prev_neigh_alloc_block and nxt_neigh_alloc_block are already filled due to struct alignment.
            }
        }
    }
    return VOID_PTR(new_alloc) + __ALLOC_BLOCK_HEADER_SZ; // Pointer to body of allocated block
}

void __free(void * memory) {
    if (memory == NULL) {
        return;
    }

    __alloc_block_header * header = memory - __ALLOC_BLOCK_HEADER_SZ;
    if (header == last_alloc_block) {
        //
        // Update last_alloc_block
        //
        if (last_alloc_block->prev_neigh_alloc_block != NULL) {
            last_alloc_block = last_alloc_block->prev_neigh_alloc_block;
        } else if (last_alloc_block->back_merge_on_free != NULL && last_alloc_block->back_merge_on_free->back_expansion_cand != NULL) {
            last_alloc_block = last_alloc_block->back_merge_on_free->back_expansion_cand;
        } else {
            //
            // Last allocated block is also the first allocated block.
            // There are no allocated blocks left.
            //
            last_alloc_block = NULL;
        }
    }

    if (free_block_list == NULL) {
        //
        // There are no free blocks.
        // Start new list.
        //
        free_block_list = (__free_block_header *) header;
        free_block_list->length = (header->length + __ALLOC_BLOCK_HEADER_SZ) - __FREE_BLOCK_HEADER_SZ;
        free_block_list->prev_free_block = NULL;
        free_block_list->nxt_free_block = NULL;
        // prev_neigh_alloc_block and nxt_neigh_alloc_block are already filled due to struct alignment.

        if (header->prev_neigh_alloc_block != NULL) {
            header->prev_neigh_alloc_block->fwd_merge_on_free = free_block_list;
            header->prev_neigh_alloc_block->nxt_neigh_alloc_block = NULL;
        }
        if (header->nxt_neigh_alloc_block != NULL) {
            header->nxt_neigh_alloc_block->back_merge_on_free = free_block_list;
            header->nxt_neigh_alloc_block->prev_neigh_alloc_block = NULL;
        }
    } else if (header->back_merge_on_free != NULL && header->fwd_merge_on_free != NULL) {
        //
        // Double merge.
        // Freed block + forward free block are both merged into the backward free block
        //
        header->back_merge_on_free->length += __ALLOC_BLOCK_HEADER_SZ + header->length + __FREE_BLOCK_HEADER_SZ + header->fwd_merge_on_free->length;
        header->back_merge_on_free->nxt_free_block = header->fwd_merge_on_free->nxt_free_block;
        header->back_merge_on_free->fwd_expansion_cand = header->fwd_merge_on_free->fwd_expansion_cand;
        if (header->fwd_merge_on_free->fwd_expansion_cand != NULL) {
            header->fwd_merge_on_free->fwd_expansion_cand->back_merge_on_free = header->back_merge_on_free;
        }
        if (header->fwd_merge_on_free->nxt_free_block != NULL) {
            header->fwd_merge_on_free->nxt_free_block->prev_free_block = header->back_merge_on_free;
        }
    } else if (header->back_merge_on_free != NULL) {
        //
        // Merge with neighboring free block behind it.
        //
        __free_block_header * merging_free_block = header->back_merge_on_free;
        merging_free_block->length += __ALLOC_BLOCK_HEADER_SZ + header->length;
        merging_free_block->fwd_expansion_cand = header->nxt_neigh_alloc_block;
        if (header->nxt_neigh_alloc_block != NULL) {
            header->nxt_neigh_alloc_block->back_merge_on_free = merging_free_block;
            header->nxt_neigh_alloc_block->prev_neigh_alloc_block = NULL;
        }
    } else if (header->fwd_merge_on_free != NULL) {
        //
        // Merge with neighboring free block ahead of it.
        //
        __free_block_header * merging_free_block = header->fwd_merge_on_free;
        __free_block_header * freed_header = (__free_block_header *) header;
        freed_header->length += __FREE_BLOCK_HEADER_SZ + merging_free_block->length;
        freed_header->prev_free_block = merging_free_block->prev_free_block;
        if (merging_free_block->prev_free_block != NULL) {
            merging_free_block->prev_free_block->nxt_free_block = freed_header;
        } else {
            // 
            // Free block we're merging into is the first block of list.
            // Update list start
            //
            free_block_list = freed_header;
        }
        freed_header->nxt_free_block = merging_free_block->nxt_free_block;
        if (merging_free_block->nxt_free_block != NULL) {
            merging_free_block->nxt_free_block->prev_free_block = freed_header;
        }
        // freed_header->back_expansion_cand is already filled due to struct alignment
        freed_header->fwd_expansion_cand = merging_free_block->fwd_expansion_cand;
        if (merging_free_block->fwd_expansion_cand != NULL) {
            merging_free_block->fwd_expansion_cand->back_merge_on_free = freed_header;
        }
        
        if (freed_header->back_expansion_cand != NULL) {
            freed_header->back_expansion_cand->fwd_merge_on_free = freed_header;
            freed_header->back_expansion_cand->nxt_neigh_alloc_block = NULL;
        }
    } else {
        //
        // No surrounding free blocks to merge with.
        // The allocated block will become a new free block.
        // Find the closest free blocks to it by traversing neighboring allocated blocks.
        //
        __free_block_header * freed_header = (__free_block_header *) header;
        freed_header->length += __ALLOC_BLOCK_HEADER_SZ - __FREE_BLOCK_HEADER_SZ;

        if (header->prev_neigh_alloc_block != NULL) {
            header->prev_neigh_alloc_block->fwd_merge_on_free = freed_header;
            header->prev_neigh_alloc_block->nxt_neigh_alloc_block = NULL;
        }
        if (header->nxt_neigh_alloc_block != NULL) {
            header->nxt_neigh_alloc_block->back_merge_on_free = freed_header;
            header->nxt_neigh_alloc_block->prev_neigh_alloc_block = NULL;
        }

        __free_block_header * prev_free_block_cursor = NULL;
        __free_block_header * nxt_free_block_cursor = NULL;
        //
        // Because we use a first-fit strategy and allocate memory in the end of free blocks (closer to program_break),
        //  if we assume in steady-state that the blocks most likely to be freed are the most recently allocated ones
        //  (which is likely true, for example, in servers made of many simple endpoints that do short-lived heap
        //  allocations besides keeping a few long-lived allocations), so we have more chances to find a free block
        //  if we search first in the direction of heap_start.
        //
        __alloc_block_header * cursor = header->prev_neigh_alloc_block;
        while (cursor != NULL && prev_free_block_cursor == NULL) {
            prev_free_block_cursor = cursor->back_merge_on_free;
            cursor = cursor->prev_neigh_alloc_block;
        }
        if (prev_free_block_cursor != NULL) {
            //
            // Use found free block as shortcut to find the nxt_free_block_cursor
            //
            nxt_free_block_cursor = prev_free_block_cursor->nxt_free_block;
            // Set links
            prev_free_block_cursor->nxt_free_block = freed_header;
            freed_header->prev_free_block = prev_free_block_cursor;
            if (nxt_free_block_cursor != NULL) {
                nxt_free_block_cursor->prev_free_block = freed_header;
                freed_header->nxt_free_block = nxt_free_block_cursor;
            }
        } else {
            //
            // There is no previous free block.
            // This means the current free block list start is the nxt_free_block of the block being freed.
            //
            nxt_free_block_cursor = free_block_list;
            free_block_list = freed_header;
            nxt_free_block_cursor->prev_free_block = freed_header;
            // freed_header->prev_free_block is already filled due to struct alignment
            freed_header->nxt_free_block = nxt_free_block_cursor;
        }
        // freed_header->back_expansion_cand and freed_header->fwd_expansion_cand are already filled due to struct alignment
    }
}

void debug1(void * p1) {
    printf("heap_start=%p\nprogram_break=%p\nfree_block_list=%p\nlast_alloc_block=%p\np1=%p\n", heap_start, program_break, free_block_list, last_alloc_block, p1);
}

void debug2(void *p1, void *p2) {
    debug1(p1);
    printf("p2=%p\n", p2);
}

void debug5(void *p1, void *p2, void *p3, void *p4, void *p5) {
    debug2(p1, p2);
    printf("p3=%p\np4=%p\np5=%p\n", p3, p4, p5);
}

void __attribute__((__noreturn__))
chpt7_q2() {
    //
    // Mixing our own __malloc and __free with the actual malloc and free functions from the GNU C library
    //  might make a mess. Therefore, we're not allowed to use any resources that make use of these two under
    //  the hood nor call them directly. 
    //
    // To avoid undefined behavior, we consider the current program break as a base address and only use 
    //  heap space above this address.
    // We _exit(0) in the end so as not to risk further instructions after this function.
    //
    heap_start = program_break = sbrk(0);
    __free_block_header * f, * ff;
    void * p, * pp, * ppp, * pppp;
    //
    // This should be a NOP
    //
    __free(NULL);
    //
    // 0-byte allocation and freeing
    // Should allocate a header.
    //
    p = __malloc(0);
    assert(program_break - heap_start == __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list == NULL);
    assert(p == VOID_PTR(last_alloc_block) + __ALLOC_BLOCK_HEADER_SZ);
    __free(p);
    assert(free_block_list == heap_start);
    assert(free_block_list->length == 0);
    assert(last_alloc_block == NULL);
    assert(p == VOID_PTR(free_block_list) + __ALLOC_BLOCK_HEADER_SZ);
    //
    // 1-byte allocation and freeing
    // Should extend the heap by a single byte
    //
    p = __malloc(1);
    assert(program_break - heap_start == __ALLOC_BLOCK_HEADER_SZ + 1);
    assert(free_block_list == NULL);
    assert(p == VOID_PTR(last_alloc_block) + __ALLOC_BLOCK_HEADER_SZ);
    __free(p);
    assert(free_block_list == heap_start);
    assert(free_block_list->length == 1);
    assert(last_alloc_block == NULL);
    assert(p == VOID_PTR(free_block_list) + __ALLOC_BLOCK_HEADER_SZ);
    //
    // 1-byte allocation and freeing again
    // Should not extend heap
    //
    p = __malloc(1);
    assert(program_break - heap_start == __ALLOC_BLOCK_HEADER_SZ + 1);
    assert(free_block_list == NULL);
    assert(p == VOID_PTR(last_alloc_block) + __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(p - __ALLOC_BLOCK_HEADER_SZ))->length == 1);
    __free(p);
    assert(free_block_list == heap_start);
    assert(free_block_list->length == 1);
    assert(last_alloc_block == NULL);
    assert(p == VOID_PTR(free_block_list) + __ALLOC_BLOCK_HEADER_SZ);
    //
    // Allocate __MINIMUM_BLOCK_SZ bytes total in the heap and free it.
    // Allocate 2 0-length blocks.
    //
    p = __malloc(__MINIMUM_BLOCK_SZ);
    assert(program_break - heap_start == __ALLOC_BLOCK_HEADER_SZ + __MINIMUM_BLOCK_SZ);
    assert(free_block_list == NULL);
    assert(p == VOID_PTR(last_alloc_block) + __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(p - __ALLOC_BLOCK_HEADER_SZ))->length == 40);
    __free(p);
    assert(free_block_list == heap_start);
    assert(free_block_list->length == __MINIMUM_BLOCK_SZ);
    assert(last_alloc_block == NULL);
    assert(p == VOID_PTR(free_block_list) + __ALLOC_BLOCK_HEADER_SZ);
    p = __malloc(0);
    assert(program_break - heap_start == __ALLOC_BLOCK_HEADER_SZ + __MINIMUM_BLOCK_SZ);
    assert(free_block_list == heap_start);
    assert(free_block_list->length == 0);
    assert(p == VOID_PTR(last_alloc_block) + __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(p - __ALLOC_BLOCK_HEADER_SZ))->length == 0);
    pp = __malloc(0);
    assert(program_break - heap_start == __ALLOC_BLOCK_HEADER_SZ + __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list == NULL);
    assert(pp == heap_start + __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(pp - __ALLOC_BLOCK_HEADER_SZ))->length == 0);
    __free(pp);
    assert(free_block_list == heap_start);
    assert(free_block_list->length == 0);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == NULL);
    assert(free_block_list->back_expansion_cand == NULL);
    assert(free_block_list->fwd_expansion_cand == VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(p == VOID_PTR(last_alloc_block) + __ALLOC_BLOCK_HEADER_SZ);
    assert(last_alloc_block->length == 0);
    assert(last_alloc_block->back_merge_on_free == free_block_list);
    assert(last_alloc_block->fwd_merge_on_free == NULL);
    assert(last_alloc_block->prev_neigh_alloc_block == NULL);
    assert(last_alloc_block->nxt_neigh_alloc_block == NULL);
    __free(p);
    assert(free_block_list == heap_start);
    assert(free_block_list->length == __MINIMUM_BLOCK_SZ);
    assert(last_alloc_block == NULL);
    p = __malloc(0);
    pp = __malloc(0);
    __free(p);
    assert(free_block_list == VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->length == 0);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == NULL);
    assert(free_block_list->back_expansion_cand == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->fwd_expansion_cand == NULL);
    assert(last_alloc_block == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(last_alloc_block->length == 0);
    assert(last_alloc_block->back_merge_on_free == NULL);
    assert(last_alloc_block->fwd_merge_on_free == free_block_list);
    assert(last_alloc_block->prev_neigh_alloc_block == NULL);
    assert(last_alloc_block->nxt_neigh_alloc_block == NULL);
    __free(pp);
    assert(free_block_list == heap_start);
    assert(free_block_list->length == __MINIMUM_BLOCK_SZ);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == NULL);
    assert(free_block_list->back_expansion_cand == NULL);
    assert(free_block_list->fwd_expansion_cand == NULL);
    assert(last_alloc_block == NULL);
    //
    // Allocate the heap to fill it completely and then allocate extra memory that expands the heap.
    // Then free the first allocation, break it into 2 allocations, totalling 3 allocated blocks.
    // Free the middle block.
    //
    p = __malloc(__MINIMUM_BLOCK_SZ);
    ppp = __malloc(2 * __MINIMUM_BLOCK_SZ);
    assert(program_break - heap_start == 5 * __MINIMUM_BLOCK_SZ);
    assert(free_block_list == NULL);
    assert(last_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(last_alloc_block->length == 2*__MINIMUM_BLOCK_SZ);
    assert(last_alloc_block->back_merge_on_free == NULL);
    assert(last_alloc_block->fwd_merge_on_free == NULL);
    assert(last_alloc_block->prev_neigh_alloc_block == VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(last_alloc_block->nxt_neigh_alloc_block == NULL);
    assert(last_alloc_block->prev_neigh_alloc_block->length == __MINIMUM_BLOCK_SZ);
    assert(last_alloc_block->prev_neigh_alloc_block->back_merge_on_free == NULL);
    assert(last_alloc_block->prev_neigh_alloc_block->fwd_merge_on_free == NULL);
    assert(last_alloc_block->prev_neigh_alloc_block->prev_neigh_alloc_block == NULL);
    assert(last_alloc_block->prev_neigh_alloc_block->nxt_neigh_alloc_block ==VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    __free(p);
    assert(free_block_list == heap_start && heap_start == VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == NULL);
    assert(free_block_list->back_expansion_cand == NULL);
    assert(free_block_list->fwd_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    pp = __malloc(0);
    assert(last_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    p = __malloc(0);
    assert(free_block_list == NULL);
    assert(last_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(heap_start == VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    // Free middle block
    __free(pp);
    assert(free_block_list == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == NULL);
    assert(free_block_list->back_expansion_cand == VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->fwd_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(last_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    //
    // 1) Free last big block.
    // 2) Produce a strided pattern of 0-length blocks: alloc - free - alloc - free - alloc.
    // 3) Try to allocate __MINIMUM_BLOCK_SZ (there's no space, so it should expand the heap).
    // 4) Free the space just allocated
    // 5) Try to allocate __MINIMUM_BLOCK_SZ. Should follow the first-fit strategy.
    // 6) Free the block allocated in 5). Reallocate it.
    //
    __free(ppp); // 1)
    assert(free_block_list == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->length == 3*__MINIMUM_BLOCK_SZ);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == NULL);
    assert(free_block_list->back_expansion_cand == VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->fwd_expansion_cand == NULL);
    assert(last_alloc_block == VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    ppp = __malloc(0);
    assert(free_block_list == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->length == 2*__MINIMUM_BLOCK_SZ);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == NULL);
    assert(free_block_list->back_expansion_cand == VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->fwd_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(last_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    pppp = __malloc(0);
    assert(free_block_list == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->length == __MINIMUM_BLOCK_SZ);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == NULL);
    assert(free_block_list->back_expansion_cand == VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->fwd_expansion_cand == VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(last_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    pp = __malloc(0);
    assert(free_block_list == heap_start + __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->length == 0);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == NULL);
    assert(free_block_list->back_expansion_cand == VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->fwd_expansion_cand == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(last_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    __free(pppp); // 2)
    // Ok, finally we have our pattern: alloc (p) - free - alloc (pp) - free - alloc (ppp).
    assert(free_block_list == heap_start + __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->length == 0);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->back_expansion_cand == VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->fwd_expansion_cand == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->length == 0);
    assert(free_block_list->nxt_free_block->prev_free_block == free_block_list);
    assert(free_block_list->nxt_free_block->nxt_free_block == NULL);
    assert(free_block_list->nxt_free_block->back_expansion_cand == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->fwd_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(last_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    // 3)
    f = pppp - __ALLOC_BLOCK_HEADER_SZ;
    pppp = __malloc(__MINIMUM_BLOCK_SZ);
    assert(program_break - heap_start == 7 * __MINIMUM_BLOCK_SZ);
    assert(free_block_list == heap_start + __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->length == 0);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == f);
    assert(free_block_list->back_expansion_cand == VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->fwd_expansion_cand == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->length == 0);
    assert(free_block_list->nxt_free_block->prev_free_block == free_block_list);
    assert(free_block_list->nxt_free_block->nxt_free_block == NULL);
    assert(free_block_list->nxt_free_block->back_expansion_cand == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->fwd_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(last_alloc_block == VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->length == __MINIMUM_BLOCK_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    // 4)
    __free(pppp);
    assert(free_block_list == heap_start + __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->length == 0);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == f);
    assert(free_block_list->back_expansion_cand == VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->fwd_expansion_cand == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->length == 0);
    assert(free_block_list->nxt_free_block->prev_free_block == free_block_list);
    assert(free_block_list->nxt_free_block->nxt_free_block == VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->back_expansion_cand == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->fwd_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->nxt_free_block->length == __MINIMUM_BLOCK_SZ);
    assert(free_block_list->nxt_free_block->nxt_free_block->prev_free_block == free_block_list->nxt_free_block);
    assert(free_block_list->nxt_free_block->nxt_free_block->nxt_free_block == NULL);
    assert(free_block_list->nxt_free_block->nxt_free_block->back_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->nxt_free_block->fwd_expansion_cand == NULL);
    assert(last_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list->nxt_free_block->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    // 5)
    ff = VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ;
    pppp = __malloc(0);
    assert(program_break - heap_start == 7 * __MINIMUM_BLOCK_SZ);
    assert(free_block_list == heap_start + 3 * __MINIMUM_BLOCK_SZ);
    assert(free_block_list->length == 0);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == ff);
    assert(free_block_list->back_expansion_cand == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->fwd_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->length == __MINIMUM_BLOCK_SZ);
    assert(free_block_list->nxt_free_block->prev_free_block == free_block_list);
    assert(free_block_list->nxt_free_block->nxt_free_block == NULL);
    assert(free_block_list->nxt_free_block->back_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->fwd_expansion_cand == NULL);
    assert(last_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    // 6)
    __free(pppp);
    assert(free_block_list == heap_start + __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->length == 0);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == f);
    assert(free_block_list->back_expansion_cand == VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->fwd_expansion_cand == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->length == 0);
    assert(free_block_list->nxt_free_block->prev_free_block == free_block_list);
    assert(free_block_list->nxt_free_block->nxt_free_block == ff);
    assert(free_block_list->nxt_free_block->back_expansion_cand == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->fwd_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->nxt_free_block->length == __MINIMUM_BLOCK_SZ);
    assert(free_block_list->nxt_free_block->nxt_free_block->prev_free_block == free_block_list->nxt_free_block);
    assert(free_block_list->nxt_free_block->nxt_free_block->nxt_free_block == NULL);
    assert(free_block_list->nxt_free_block->nxt_free_block->back_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->nxt_free_block->fwd_expansion_cand == NULL);
    assert(last_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(p) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list->nxt_free_block->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    pppp = __malloc(0); // Undo
    // 
    // Now free 1st, 3rd and 2nd in this order.
    // This causes a double merge when freeing the 2nd block.
    //
    __free(p);
    assert(free_block_list == heap_start);
    assert(free_block_list->length == 0);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == f);
    assert(free_block_list->back_expansion_cand == NULL);
    assert(free_block_list->fwd_expansion_cand == VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->length == 0);
    assert(free_block_list->nxt_free_block->prev_free_block == free_block_list);
    assert(free_block_list->nxt_free_block->nxt_free_block == ff);
    assert(free_block_list->nxt_free_block->back_expansion_cand == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->fwd_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->nxt_free_block->length == __MINIMUM_BLOCK_SZ);
    assert(free_block_list->nxt_free_block->nxt_free_block->prev_free_block == free_block_list->nxt_free_block);
    assert(free_block_list->nxt_free_block->nxt_free_block->nxt_free_block == NULL);
    assert(free_block_list->nxt_free_block->nxt_free_block->back_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->nxt_free_block->fwd_expansion_cand == NULL);
    assert(last_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block == VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list->nxt_free_block->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    __free(pp);
    assert(free_block_list == heap_start);
    assert(free_block_list->length == 0);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == VOID_PTR(pp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->back_expansion_cand == NULL);
    assert(free_block_list->fwd_expansion_cand == VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->length == __MINIMUM_BLOCK_SZ);
    assert(free_block_list->nxt_free_block->prev_free_block == free_block_list);
    assert(free_block_list->nxt_free_block->nxt_free_block == ff);
    assert(free_block_list->nxt_free_block->back_expansion_cand == VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->fwd_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->nxt_free_block->length == __MINIMUM_BLOCK_SZ);
    assert(free_block_list->nxt_free_block->nxt_free_block->prev_free_block == free_block_list->nxt_free_block);
    assert(free_block_list->nxt_free_block->nxt_free_block->nxt_free_block == NULL);
    assert(free_block_list->nxt_free_block->nxt_free_block->back_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->nxt_free_block->fwd_expansion_cand == NULL);
    assert(last_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  NULL);
    assert(((__alloc_block_header *)(VOID_PTR(pppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list->nxt_free_block->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    __free(pppp);
    assert(free_block_list == heap_start);
    assert(free_block_list->length == 3*__MINIMUM_BLOCK_SZ);
    assert(free_block_list->prev_free_block == NULL);
    assert(free_block_list->nxt_free_block == ff);
    assert(free_block_list->back_expansion_cand == NULL);
    assert(free_block_list->fwd_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->length == __MINIMUM_BLOCK_SZ);
    assert(free_block_list->nxt_free_block->prev_free_block == free_block_list);
    assert(free_block_list->nxt_free_block->nxt_free_block == NULL);
    assert(free_block_list->nxt_free_block->back_expansion_cand == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(free_block_list->nxt_free_block->fwd_expansion_cand == NULL);
    assert(last_alloc_block == VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->back_merge_on_free == free_block_list);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->fwd_merge_on_free == free_block_list->nxt_free_block);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->prev_neigh_alloc_block ==  NULL);
    assert(((__alloc_block_header *)(VOID_PTR(ppp) - __ALLOC_BLOCK_HEADER_SZ))->nxt_neigh_alloc_block == NULL);
    __free(ppp);
    _exit(0);
}