#define _POSIX_C_SOURCE 200809L

#include "core_bridge.h"
#include "qjs_runtime.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern const uint8_t pjs_embedded_package[];
extern const uint32_t pjs_embedded_package_length;

typedef struct {
    void *raw;
    size_t size;
} AllocationHeader;

typedef struct {
    bool alive;
    int32_t parent;
    double properties[128];
} MockNode;

static MockNode nodes[16];
static int32_t next_node = 2;

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static size_t align16(size_t value)
{
    return (value + 15u) & ~(size_t)15u;
}

static PjsGuestPackage guest_from_embedded(void)
{
    const uint8_t *bytes = pjs_embedded_package;
    size_t length = pjs_embedded_package_length;
    assert(length >= 64u);
    assert(read_u32(bytes) == 0x544b4350u);
    size_t manifest_length = read_u32(bytes + 8u);
    size_t variant = align16(16u + manifest_length);
    assert(variant + 40u <= length);
    assert(memcmp(bytes + variant, "ipod-photo", 10u) == 0);
    assert(read_u32(bytes + variant + 16u) == 1u);
    size_t section_count = read_u32(bytes + variant + 20u);
    size_t section_table = read_u32(bytes + variant + 24u);

    PjsGuestPackage guest = {0};
    for (size_t index = 0; index < section_count; ++index) {
        const uint8_t *entry = bytes + section_table + index * 16u;
        uint32_t kind = read_u32(entry);
        size_t offset = read_u32(entry + 8u);
        size_t section_length = read_u32(entry + 12u);
        assert(offset + section_length <= length);
        if (kind == 2u) {
            guest.plan = bytes + offset;
            guest.plan_length = (uint32_t)section_length;
        } else if (kind == 3u) {
            guest.javascript = bytes + offset;
            guest.javascript_length = (uint32_t)section_length;
        } else if (kind == 4u) {
            guest.pak = bytes + offset;
            guest.pak_length = (uint32_t)section_length;
        }
    }
    assert(guest.javascript != NULL);
    assert(guest.javascript_length > 1u);
    assert(guest.javascript[guest.javascript_length - 1u] == 0u);
    return guest;
}

void *pjs_heap_alloc(size_t size, size_t alignment)
{
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    size_t total = size + alignment - 1u + sizeof(AllocationHeader);
    void *raw = malloc(total);
    if (raw == NULL) return NULL;
    uintptr_t candidate = (uintptr_t)raw + sizeof(AllocationHeader);
    uintptr_t aligned = (candidate + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
    AllocationHeader *header = (AllocationHeader *)aligned - 1;
    header->raw = raw;
    header->size = size;
    return (void *)aligned;
}

void pjs_heap_free(void *pointer)
{
    if (pointer == NULL) return;
    AllocationHeader *header = (AllocationHeader *)pointer - 1;
    free(header->raw);
}

void *pjs_heap_realloc(void *pointer, size_t size, size_t alignment)
{
    if (pointer == NULL) return pjs_heap_alloc(size, alignment);
    if (size == 0u) {
        pjs_heap_free(pointer);
        return NULL;
    }
    AllocationHeader *header = (AllocationHeader *)pointer - 1;
    void *replacement = pjs_heap_alloc(size, alignment);
    if (replacement == NULL) return NULL;
    memcpy(replacement, pointer, header->size < size ? header->size : size);
    pjs_heap_free(pointer);
    return replacement;
}

size_t pjs_heap_usable_size(const void *pointer)
{
    if (pointer == NULL) return 0u;
    return ((const AllocationHeader *)pointer - 1)->size;
}

uint32_t timer_now_us(void)
{
    struct timespec value;
    assert(clock_gettime(CLOCK_MONOTONIC, &value) == 0);
    uint64_t micros = (uint64_t)value.tv_sec * 1000000u +
                      (uint64_t)value.tv_nsec / 1000u;
    return (uint32_t)micros;
}

int32_t pjs_ui_create_node(uint32_t node_type)
{
    (void)node_type;
    assert(next_node < (int32_t)(sizeof(nodes) / sizeof(nodes[0])));
    int32_t id = next_node++;
    nodes[id].alive = true;
    return id;
}

void pjs_ui_destroy_node(int32_t id)
{
    if (id >= 0 && id < (int32_t)(sizeof(nodes) / sizeof(nodes[0]))) {
        nodes[id].alive = false;
    }
}

void pjs_ui_insert_before(int32_t parent, int32_t child, int32_t anchor)
{
    (void)anchor;
    assert(parent == 1);
    assert(child >= 2 && child < next_node);
    nodes[child].parent = parent;
}

void pjs_ui_remove_child(int32_t parent, int32_t child)
{
    if (child >= 0 && child < (int32_t)(sizeof(nodes) / sizeof(nodes[0])) &&
        nodes[child].parent == parent) {
        nodes[child].parent = 0;
    }
}

void pjs_ui_set_prop(int32_t id, uint32_t prop, double value)
{
    assert(id >= 2 && id < next_node);
    assert(prop < 128u);
    nodes[id].properties[prop] = value;
}

static uint32_t abgr(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint32_t)red | ((uint32_t)green << 8) |
           ((uint32_t)blue << 16) | 0xff000000u;
}

int main(void)
{
    PjsGuestPackage guest = guest_from_embedded();
    assert(qjs_runtime_boot(&guest));
    assert(qjs_runtime_error_code() == PJS_QJS_ERROR_NONE);
    assert(next_node == 5);
    assert(nodes[2].parent == 1 && nodes[3].parent == 1 && nodes[4].parent == 1);

    PjsCoreInput input = {
        .wheel_delta = 3,
        .wheel_position = 95,
        .wheel_touched = 1,
        .battery_mv = 4010,
        .power_flags = 2,
        .runtime_status = PJS_RUNTIME_READY,
    };
    assert(qjs_runtime_frame(&input));
    assert((int)nodes[3].properties[28] == 202);
    assert((uint32_t)nodes[3].properties[64] == abgr(0, 222, 255));
    assert(nodes[3].properties[69] == 1.0);

    input.wheel_touched = 0;
    input.hold = 1;
    assert(qjs_runtime_frame(&input));
    assert((uint32_t)nodes[3].properties[64] == abgr(255, 78, 90));
    assert(nodes[3].properties[69] == 0.45);

    qjs_runtime_shutdown();
    puts("QuickJS embedded guest smoke test: OK");
    return 0;
}
