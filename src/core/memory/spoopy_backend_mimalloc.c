#include <spoopy.h>
#include <memory/spoopy_memory.h>
#include <memory/spoopy_arena.h>
#include <utils/spoopy_misc_math.h>
#include <spoopy_log.h>
#include <utils/assert.h>

#include <mimalloc.h>


void spoopy_heap_free(void* ptr) {
    mi_free(ptr);
}

void* spoopy_heap_alloc(size_t size) {
    return spoopy_aligned_alloc(SPOOPY_MAX_ALIGN, size, NULL);
}

void* spoopy_aligned_alloc(size_t alignment, size_t size, void* user_data) {
    (void)user_data;
    alignment = spoopy_max(alignment, SPOOPY_MAX_ALIGN);
    return mi_calloc_aligned(1, size, alignment);
}

void* spoopy_heap_realloc(void* ptr, size_t size) {
    assert(size > 0);
    return mi_realloc_aligned(ptr, size, SPOOPY_MAX_ALIGN);
}
