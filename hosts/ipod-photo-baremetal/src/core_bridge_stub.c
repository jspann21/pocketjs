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
void pjs_core_set_boot_diagnostic(uint32_t source, uint32_t failure_stage,
                                  uint32_t failure_code,
                                  uint32_t sector_reads)
{
    (void)source;
    (void)failure_stage;
    (void)failure_code;
    (void)sector_reads;
}
void pjs_core_set_app_diagnostic(uint32_t selected, uint32_t count,
                                 uint32_t sector_reads)
{
    (void)selected;
    (void)count;
    (void)sector_reads;
}
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
void pjs_ui_set_style(int32_t id, int32_t style_id)
{
    (void)id; (void)style_id;
}
void pjs_ui_set_prop(int32_t id, uint32_t prop, double value)
{
    (void)id; (void)prop; (void)value;
}
void pjs_ui_set_prop_batch(const uint8_t *bytes, size_t length)
{
    (void)bytes; (void)length;
}
void pjs_ui_set_text(int32_t id, const uint8_t *text, size_t length)
{
    (void)id; (void)text; (void)length;
}
void pjs_ui_replace_text(int32_t id, const uint8_t *text, size_t length)
{
    (void)id; (void)text; (void)length;
}
int32_t pjs_ui_upload_texture(const uint8_t *bytes, size_t length,
                              uint32_t width, uint32_t height,
                              uint32_t pixel_storage)
{
    (void)bytes; (void)length; (void)width; (void)height; (void)pixel_storage;
    return -1;
}
int32_t pjs_ui_upload_img_entry(const uint8_t *bytes, size_t length)
{
    (void)bytes; (void)length;
    return -1;
}
void pjs_ui_free_texture(int32_t handle) { (void)handle; }
void pjs_ui_set_image(int32_t id, int32_t texture)
{
    (void)id; (void)texture;
}
void pjs_ui_set_sprite(int32_t id, int32_t atlas, uint32_t frames,
                       uint32_t columns, uint32_t step)
{
    (void)id; (void)atlas; (void)frames; (void)columns; (void)step;
}
int32_t pjs_ui_animate(int32_t id, uint32_t prop, double to,
                       uint32_t duration_ms, uint32_t easing,
                       uint32_t delay_ms)
{
    (void)id; (void)prop; (void)to; (void)duration_ms;
    (void)easing; (void)delay_ms;
    return -1;
}
void pjs_ui_cancel_anim(int32_t animation_id) { (void)animation_id; }
void pjs_ui_set_focus(int32_t id) { (void)id; }
void pjs_ui_set_active(int32_t id, int32_t active)
{
    (void)id; (void)active;
}
int32_t pjs_ui_load_styles(const uint8_t *bytes, size_t length)
{
    (void)bytes; (void)length;
    return 0;
}
int32_t pjs_ui_load_font_atlas(const uint8_t *bytes, size_t length)
{
    (void)bytes; (void)length;
    return 0;
}
float pjs_ui_measure_text(const uint8_t *text, size_t length,
                          uint32_t font_slot)
{
    (void)text; (void)length; (void)font_slot;
    return 0.0f;
}
