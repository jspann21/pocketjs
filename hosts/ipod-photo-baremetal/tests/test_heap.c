#include "heap.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned char arena[512 * 1024 + 31];

int main(void)
{
    assert(pjs_heap_init(arena + 3, sizeof(arena) - 3));
    assert(pjs_heap_validate());

    void *a = pjs_heap_alloc(1, 1);
    void *b = pjs_heap_alloc(4096, 64);
    void *c = pjs_heap_alloc(8193, 4096);
    assert(a && b && c);
    assert(((uintptr_t)a & 15u) == 0u);
    assert(((uintptr_t)b & 63u) == 0u);
    assert(((uintptr_t)c & 4095u) == 0u);
    memset(b, 0x5a, 4096);

    b = pjs_heap_realloc(b, 16384, 64);
    assert(b != 0);
    for (unsigned i = 0; i < 4096; ++i) assert(((unsigned char *)b)[i] == 0x5a);
    assert(pjs_heap_validate());

    pjs_heap_free(c);
    pjs_heap_free(a);
    pjs_heap_free(b);
    assert(pjs_heap_validate());

    PjsHeapStats stats;
    pjs_heap_stats(&stats);
    assert(stats.allocation_count == 0u);
    assert(stats.free_block_count == 1u);
    assert(stats.largest_free == stats.free_bytes);

    for (unsigned round = 0; round < 64; ++round) {
        void *slots[32] = {0};
        for (unsigned i = 0; i < 32; ++i) {
            slots[i] = pjs_heap_alloc(37u + i * 113u, 1u << (4u + (i & 3u)));
            assert(slots[i] != 0);
        }
        for (unsigned i = 0; i < 32; i += 2) pjs_heap_free(slots[i]);
        for (unsigned i = 1; i < 32; i += 2) pjs_heap_free(slots[i]);
        assert(pjs_heap_validate());
    }
    puts("heap tests: OK");
    return 0;
}
