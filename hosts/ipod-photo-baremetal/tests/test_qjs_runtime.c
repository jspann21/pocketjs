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
    int32_t texture;
    double properties[160];
} MockNode;

static MockNode nodes[64];
static int32_t next_node = 2;
static int texture_uploads;
static int animations_started;

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
    assert(parent == 1 || (parent >= 2 && parent < next_node));
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

void pjs_ui_set_style(int32_t id, int32_t style_id)
{
    (void)id;
    (void)style_id;
}

void pjs_ui_set_prop(int32_t id, uint32_t prop, double value)
{
    assert(id >= 2 && id < next_node);
    assert(prop < 160u);
    nodes[id].properties[prop] = value;
}

void pjs_ui_set_prop_batch(const uint8_t *bytes, size_t length)
{
    assert(bytes != NULL);
    assert(length % (3u * sizeof(double)) == 0u);
    for (size_t offset = 0; offset < length; offset += 3u * sizeof(double)) {
        double record[3];
        memcpy(record, bytes + offset, sizeof(record));
        int32_t id = (int32_t)record[0];
        uint32_t prop = (uint32_t)record[1];
        assert(id >= 2 && id < next_node);
        assert(prop < 160u);
        nodes[id].properties[prop] = record[2];
    }
}

void pjs_ui_set_text(int32_t id, const uint8_t *text, size_t length)
{
    (void)id;
    (void)text;
    (void)length;
}

void pjs_ui_replace_text(int32_t id, const uint8_t *text, size_t length)
{
    (void)id;
    (void)text;
    (void)length;
}

int32_t pjs_ui_upload_texture(const uint8_t *bytes, size_t length,
                              uint32_t width, uint32_t height,
                              uint32_t pixel_storage)
{
    assert(bytes != NULL);
    assert(length == 8u * 8u * 4u);
    assert(width == 8u && height == 8u && pixel_storage == 3u);
    ++texture_uploads;
    return 7;
}

int32_t pjs_ui_upload_img_entry(const uint8_t *bytes, size_t length)
{
    (void)bytes;
    (void)length;
    return -1;
}

void pjs_ui_free_texture(int32_t handle)
{
    (void)handle;
}

void pjs_ui_set_image(int32_t id, int32_t texture)
{
    assert(id >= 2 && id < next_node);
    nodes[id].texture = texture;
}

void pjs_ui_set_sprite(int32_t id, int32_t atlas, uint32_t frames,
                       uint32_t columns, uint32_t step)
{
    (void)id;
    (void)atlas;
    (void)frames;
    (void)columns;
    (void)step;
}

int32_t pjs_ui_animate(int32_t id, uint32_t prop, double to,
                       uint32_t duration_ms, uint32_t easing,
                       uint32_t delay_ms)
{
    assert(id >= 2 && id < next_node);
    assert(prop == 128u);
    assert(to == 0.0 || to == 160.0);
    assert(duration_ms == 1200u && easing == 3u && delay_ms == 0u);
    ++animations_started;
    return animations_started;
}

void pjs_ui_cancel_anim(int32_t animation_id)
{
    (void)animation_id;
}

void pjs_ui_set_focus(int32_t id)
{
    (void)id;
}

void pjs_ui_set_active(int32_t id, int32_t active)
{
    (void)id;
    (void)active;
}

int32_t pjs_ui_load_styles(const uint8_t *bytes, size_t length)
{
    (void)bytes;
    (void)length;
    return 1;
}

int32_t pjs_ui_load_font_atlas(const uint8_t *bytes, size_t length)
{
    (void)bytes;
    (void)length;
    return 1;
}

float pjs_ui_measure_text(const uint8_t *text, size_t length,
                          uint32_t font_slot)
{
    (void)text;
    (void)length;
    (void)font_slot;
    return 0.0f;
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
    if (guest.pak_length != 0u) {
        assert(next_node > 2);
        PjsCoreInput input = {0};
        assert(qjs_runtime_frame(&input));
        input.buttons = 1u;
        assert(qjs_runtime_frame(&input));
        input.buttons = 0u;
        assert(qjs_runtime_frame(&input));
        input.wheel_delta = 1;
        assert(qjs_runtime_frame(&input));
        qjs_runtime_shutdown();
        puts("QuickJS generated guest smoke test: OK");
        return 0;
    }
    assert(next_node == 5);
    for (int32_t node = 2; node < next_node; ++node) assert(nodes[node].parent == 1);
    assert(texture_uploads == 0);
    assert((int)nodes[3].properties[28] == 8);
    assert((int)nodes[3].properties[25] == 36);
    assert((int)nodes[3].properties[1] == 10);
    assert((int)nodes[3].properties[2] == 12);
    assert(animations_started == 0);

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

    input.hold = 0;
    input.buttons = 1;
    assert(qjs_runtime_frame(&input));
    assert((uint32_t)nodes[3].properties[64] == abgr(45, 235, 105));
    assert(animations_started == 0);

    qjs_runtime_shutdown();
    puts("QuickJS embedded guest smoke test: OK");
    return 0;
}
