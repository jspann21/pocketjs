#include "core_bridge.h"

const volatile uint32_t PJS_CORE_BACKEND_STUB = PJS_CORE_BACKEND_STUB_MAGIC;

uint32_t pjs_core_backend_marker(void)
{
    return PJS_CORE_BACKEND_STUB;
}

int32_t pjs_core_init(void) { return -1; }
int32_t pjs_core_step(const PjsCoreInput *input) { (void)input; return -1; }
int32_t pjs_core_render(uint16_t *pixels, uint32_t pixel_count)
{
    (void)pixels;
    (void)pixel_count;
    return -1;
}
int32_t pjs_core_render_damage(uint16_t *pixels, uint32_t pixel_count,
                               PjsCoreDamagePlan *damage)
{
    (void)pixels;
    (void)pixel_count;
    if (damage != 0) *damage = (PjsCoreDamagePlan){0};
    return -1;
}
uint32_t pjs_core_needs_render(void) { return 0u; }
uint32_t pjs_core_frame(void) { return 0u; }
uint32_t pjs_core_draw_words(void) { return 0u; }
void pjs_core_shutdown(void) {}

int32_t pjs_package_open_ipod_photo(const uint8_t *bytes, uint32_t length,
                                    PjsGuestPackage *guest)
{
    (void)bytes;
    (void)length;
    if (guest != 0) *guest = (PjsGuestPackage){0};
    return -1;
}

static int32_t stub_next_node = 2;
int32_t pjs_ui_create_node(uint32_t node_type)
{
    return node_type <= 3u ? stub_next_node++ : 0;
}
void pjs_ui_destroy_node(int32_t id) { (void)id; }
void pjs_ui_insert_before(int32_t parent, int32_t child, int32_t anchor)
{
    (void)parent; (void)child; (void)anchor;
}
void pjs_ui_remove_child(int32_t parent, int32_t child)
{
    (void)parent; (void)child;
}
void pjs_ui_set_prop(int32_t id, uint32_t prop, double value)
{
    (void)id; (void)prop; (void)value;
}
