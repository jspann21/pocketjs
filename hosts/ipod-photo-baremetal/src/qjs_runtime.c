#include "qjs_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "heap.h"
#include "quickjs.h"
#include "timer.h"

#define PJS_QJS_MEMORY_LIMIT (6u * 1024u * 1024u)
#define PJS_QJS_GC_THRESHOLD (512u * 1024u)
#define PJS_QJS_STACK_LIMIT (192u * 1024u)
/* Ordinary compiler bundles are much larger than the hand-written recovery
 * guest.  Preserve a hard boot bound, but allow the 80 MHz PP5020 enough time
 * to parse, mount styles/fonts, and create the first retained tree. */
#define PJS_QJS_BOOT_BUDGET_US 30000000u
/* The first physical A1099 HostOps candidate tripped the 100 ms watchdog on
 * the Hold edge while the guest performed retained-UI mutations. Keep frames
 * bounded, but leave enough PP5020 headroom for an interactive mutation burst.
 * Boot has a separate, deliberately larger bound because it parses the whole
 * generated framework bundle and installs baked assets. */
#define PJS_QJS_FRAME_BUDGET_US 250000u
#define PJS_QJS_MAX_PENDING_JOBS 64u
#define PJS_QJS_MALLOC_OVERHEAD 8u
#define PJS_QJS_ROOT_ID 1
#define PJS_QJS_HOST_ABI 1
#define PJS_QJS_MAX_LAUNCHER_APPS 6u
#define PJS_QJS_LAUNCHER_LABEL_BYTES 9u

/* The A1099 input decoder intentionally uses a compact, device-local bit
 * layout (input.h) for the native diagnostic screen.  The guest never sees
 * those bits: generated PocketJS apps consume the portable BTN mask from
 * contracts/spec/spec.ts.  Keep the values here in lock-step with that table
 * without making the freestanding target depend on generated TypeScript. */
#define PJS_FRAME_BTN_SELECT   0x0001u
#define PJS_FRAME_BTN_START    0x0008u
#define PJS_FRAME_BTN_RIGHT    0x0020u
#define PJS_FRAME_BTN_LEFT     0x0080u
#define PJS_FRAME_BTN_TRIANGLE 0x1000u
#define PJS_FRAME_BTN_CIRCLE   0x2000u
#define PJS_FRAME_ANALOG_CENTER 0x8080u

/* These are deliberately the first PocketJS HostOps required by the embedded
 * recovery guest. The ABI surface grows append-only toward the complete host
 * contract; none of these calls know about the LCD or JavaScript runtime. */
typedef enum {
    HOST_CREATE_NODE = 1,
    HOST_DESTROY_NODE,
    HOST_INSERT_BEFORE,
    HOST_REMOVE_CHILD,
    HOST_SET_STYLE,
    HOST_SET_PROP,
    HOST_SET_PROP_BATCH,
    HOST_SET_TEXT,
    HOST_REPLACE_TEXT,
    HOST_UPLOAD_TEXTURE,
    HOST_SET_IMAGE,
    HOST_SET_SPRITE,
    HOST_ANIMATE,
    HOST_CANCEL_ANIM,
    HOST_SET_FOCUS,
    HOST_SET_ACTIVE,
    HOST_LOAD_STYLES,
    HOST_LOAD_FONT_ATLAS,
    HOST_MEASURE_TEXT,
    HOST_FREE_TEXTURE,
    HOST_UPLOAD_IMG_ENTRY,
} HostOperation;

static JSRuntime *runtime;
static JSContext *context;
static JSValue global_value;
static JSValue frame_function;
/* Host-owned, non-portable input facts used only by the embedded recovery
 * guest.  Standard generated apps receive input through frame() and should
 * never need this object.  Keeping it under a target-prefixed name prevents
 * accidental collision with the framework namespace. */
static JSValue ipod_input_value;
static JSValue launcher_value;
static uint8_t launcher_labels[PJS_QJS_MAX_LAUNCHER_APPS]
                              [PJS_QJS_LAUNCHER_LABEL_BYTES];
static uint32_t launcher_count;
static uint32_t error_code;
static uint32_t execution_deadline;
static bool execution_timed_out;
/* The first A1099 package used a seven-argument, device-local frame callback.
 * Keep that package runnable while all new/generated guests use the standard
 * frame(buttons, analog?, touches?, hits?, touchSurfaces?) shape. */
static bool legacy_frame_abi;

bool qjs_runtime_set_launcher_catalog(const uint8_t *labels,
                                      uint32_t count, uint32_t stride)
{
    if (count > PJS_QJS_MAX_LAUNCHER_APPS ||
        (count != 0u && (labels == 0 || stride < PJS_QJS_LAUNCHER_LABEL_BYTES))) {
        return false;
    }
    launcher_count = count;
    for (uint32_t app = 0u; app < PJS_QJS_MAX_LAUNCHER_APPS; ++app) {
        for (uint32_t index = 0u; index < PJS_QJS_LAUNCHER_LABEL_BYTES; ++index) {
            launcher_labels[app][index] = 0u;
        }
        if (app >= count) continue;
        const uint8_t *source = labels + app * stride;
        for (uint32_t index = 0u; index < PJS_QJS_LAUNCHER_LABEL_BYTES; ++index) {
            launcher_labels[app][index] = source[index];
        }
        launcher_labels[app][PJS_QJS_LAUNCHER_LABEL_BYTES - 1u] = 0u;
    }
    return true;
}

static size_t qjs_malloc_usable_size(const void *pointer)
{
    return pjs_heap_usable_size(pointer);
}

static void *qjs_malloc(JSMallocState *state, size_t size)
{
    if (size == 0u || size > state->malloc_limit ||
        state->malloc_size > state->malloc_limit - size) {
        return 0;
    }
    void *pointer = pjs_heap_alloc(size, 16u);
    if (pointer == 0) return 0;
    size_t usable = pjs_heap_usable_size(pointer);
    if (usable > SIZE_MAX - PJS_QJS_MALLOC_OVERHEAD ||
        state->malloc_size > SIZE_MAX - usable - PJS_QJS_MALLOC_OVERHEAD) {
        pjs_heap_free(pointer);
        return 0;
    }
    ++state->malloc_count;
    state->malloc_size += usable + PJS_QJS_MALLOC_OVERHEAD;
    return pointer;
}

static void qjs_free(JSMallocState *state, void *pointer)
{
    if (pointer == 0) return;
    size_t usable = pjs_heap_usable_size(pointer);
    size_t accounted = usable + PJS_QJS_MALLOC_OVERHEAD;
    if (state->malloc_count != 0u) --state->malloc_count;
    if (state->malloc_size >= accounted) state->malloc_size -= accounted;
    else state->malloc_size = 0u;
    pjs_heap_free(pointer);
}

static void *qjs_realloc(JSMallocState *state, void *pointer, size_t size)
{
    if (pointer == 0) return size == 0u ? 0 : qjs_malloc(state, size);
    size_t old_size = pjs_heap_usable_size(pointer);
    if (size == 0u) {
        qjs_free(state, pointer);
        return 0;
    }
    if (size > old_size &&
        (size - old_size > state->malloc_limit ||
         state->malloc_size > state->malloc_limit - (size - old_size))) {
        return 0;
    }
    void *replacement = pjs_heap_realloc(pointer, size, 16u);
    if (replacement == 0) return 0;
    size_t new_size = pjs_heap_usable_size(replacement);
    if (new_size >= old_size) state->malloc_size += new_size - old_size;
    else state->malloc_size -= old_size - new_size;
    return replacement;
}

static const JSMallocFunctions qjs_allocators = {
    .js_malloc = qjs_malloc,
    .js_free = qjs_free,
    .js_realloc = qjs_realloc,
    .js_malloc_usable_size = qjs_malloc_usable_size,
};

static void execution_budget_start(uint32_t budget_us)
{
    execution_timed_out = false;
    execution_deadline = timer_now_us() + budget_us;
}

static void execution_budget_stop(void)
{
    execution_deadline = 0u;
}

static int interrupt_handler(JSRuntime *rt, void *opaque)
{
    (void)rt;
    (void)opaque;
    bool expired = execution_deadline != 0u &&
                   (int32_t)(timer_now_us() - execution_deadline) >= 0;
    if (expired) execution_timed_out = true;
    return expired ? 1 : 0;
}

static int argument_i32(JSContext *ctx, int argc, JSValueConst *argv,
                        int index, int32_t *value)
{
    if (index >= argc) {
        *value = 0;
        return 0;
    }
    return JS_ToInt32(ctx, value, argv[index]);
}

static int argument_u32(JSContext *ctx, int argc, JSValueConst *argv,
                        int index, uint32_t *value)
{
    if (index >= argc) {
        *value = 0u;
        return 0;
    }
    return JS_ToUint32(ctx, value, argv[index]);
}

static int argument_f64(JSContext *ctx, int argc, JSValueConst *argv,
                        int index, double *value)
{
    if (index >= argc) {
        *value = 0.0;
        return 0;
    }
    return JS_ToFloat64(ctx, value, argv[index]);
}

/* Return 1 for an ArrayBuffer/typed-array view, 0 for an invalid value, and
 * -1 when the argument was omitted. Invalid values are deliberately treated
 * as a no-op/failed upload by this non-strict native host; they must not leave
 * a stale QuickJS exception behind. The borrowed pointer is consumed before
 * this operation returns. */
static int argument_bytes(JSContext *ctx, int argc, JSValueConst *argv,
                          int index, const uint8_t **bytes, size_t *length)
{
    if (index >= argc) return -1;

    size_t direct_length = 0u;
    uint8_t *direct = JS_GetArrayBuffer(ctx, &direct_length, argv[index]);
    if (!JS_HasException(ctx)) {
        *bytes = direct;
        *length = direct_length;
        return 1;
    }
    JSValue direct_error = JS_GetException(ctx);
    JS_FreeValue(ctx, direct_error);

    size_t offset = 0u;
    size_t view_length = 0u;
    size_t bytes_per_element = 0u;
    JSValue buffer = JS_GetTypedArrayBuffer(
        ctx, argv[index], &offset, &view_length, &bytes_per_element);
    if (JS_IsException(buffer)) {
        JSValue error = JS_GetException(ctx);
        JS_FreeValue(ctx, error);
        return 0;
    }
    size_t buffer_length = 0u;
    uint8_t *base = JS_GetArrayBuffer(ctx, &buffer_length, buffer);
    if (JS_HasException(ctx)) {
        JSValue error = JS_GetException(ctx);
        JS_FreeValue(ctx, error);
        JS_FreeValue(ctx, buffer);
        return 0;
    }
    if (offset > buffer_length || view_length > buffer_length - offset) {
        JS_FreeValue(ctx, buffer);
        return 0;
    }
    (void)bytes_per_element;
    *bytes = base == 0 ? 0 : base + offset;
    *length = view_length;
    JS_FreeValue(ctx, buffer);
    return 1;
}

/* QuickJS exposes strings as temporary UTF-8 allocations. The core copies
 * text into its retained tree before the string is released. */
static int argument_string(JSContext *ctx, int argc, JSValueConst *argv,
                           int index, const char **text, size_t *length)
{
    if (index >= argc) return -1;
    *text = JS_ToCStringLen2(ctx, length, argv[index], 0);
    if (*text != 0) return 1;
    JSValue error = JS_GetException(ctx);
    JS_FreeValue(ctx, error);
    return 0;
}

/* Translate the local A1099 button packet into the portable PocketJS BTN
 * mask.  The centre/select key is the ordinary confirm action (CIRCLE), the
 * menu key is the ordinary cancel/back action (TRIANGLE), and play/pause is
 * START.  Wheel motion is a one-frame directional pulse; its absolute
 * position remains a target-local recovery fact in ui.__ipodInput. */
static uint32_t guest_buttons(const PjsCoreInput *input)
{
    uint32_t buttons = 0u;
    if ((input->buttons & PJS_BUTTON_SELECT) != 0u) {
        buttons |= PJS_FRAME_BTN_CIRCLE;
    }
    if ((input->buttons & PJS_BUTTON_RIGHT) != 0u) {
        buttons |= PJS_FRAME_BTN_RIGHT;
    }
    if ((input->buttons & PJS_BUTTON_LEFT) != 0u) {
        buttons |= PJS_FRAME_BTN_LEFT;
    }
    if ((input->buttons & PJS_BUTTON_PLAY) != 0u) {
        buttons |= PJS_FRAME_BTN_START;
    }
    if ((input->buttons & PJS_BUTTON_MENU) != 0u) {
        buttons |= PJS_FRAME_BTN_TRIANGLE;
    }
    if (input->wheel_delta > 0) buttons |= PJS_FRAME_BTN_RIGHT;
    if (input->wheel_delta < 0) buttons |= PJS_FRAME_BTN_LEFT;
    return buttons;
}

/* Keep the old recovery screen's wheel/Hold/power proof available without
 * making those device-specific words part of the generated-app frame ABI.
 * Values are replaced before every guest turn; the object itself is stable so
 * a recovery guest can retain a reference to it safely. */
static int update_ipod_input(const PjsCoreInput *input, uint32_t buttons)
{
    if (JS_SetPropertyStr(context, ipod_input_value, "buttons",
                          JS_NewUint32(context, buttons)) < 0 ||
        JS_SetPropertyStr(context, ipod_input_value, "nativeButtons",
                          JS_NewUint32(context, input->buttons)) < 0 ||
        JS_SetPropertyStr(context, ipod_input_value, "analog",
                          JS_NewUint32(context, PJS_FRAME_ANALOG_CENTER)) < 0 ||
        JS_SetPropertyStr(context, ipod_input_value, "wheelDelta",
                          JS_NewInt32(context, input->wheel_delta)) < 0 ||
        JS_SetPropertyStr(context, ipod_input_value, "wheelPosition",
                          JS_NewUint32(context, input->wheel_position)) < 0 ||
        JS_SetPropertyStr(context, ipod_input_value, "wheelTouched",
                          JS_NewBool(context, input->wheel_touched != 0u)) < 0 ||
        JS_SetPropertyStr(context, ipod_input_value, "hold",
                          JS_NewBool(context, input->hold != 0u)) < 0 ||
        JS_SetPropertyStr(context, ipod_input_value, "batteryMv",
                          JS_NewUint32(context, input->battery_mv)) < 0 ||
        JS_SetPropertyStr(context, ipod_input_value, "powerFlags",
                          JS_NewUint32(context, input->power_flags)) < 0) {
        return -1;
    }
    return 0;
}

static JSValue host_operation(JSContext *ctx, JSValueConst this_value,
                              int argc, JSValueConst *argv, int magic)
{
    (void)this_value;
    int32_t a = 0;
    int32_t b = 0;
    int32_t c = 0;
    uint32_t ua = 0u;
    uint32_t ub = 0u;
    uint32_t uc = 0u;
    int bytes_result = 0;
    int text_result = 0;
    const uint8_t *bytes = 0;
    size_t bytes_length = 0u;
    const char *text = 0;
    size_t text_length = 0u;
    double value = 0.0;

    switch ((HostOperation)magic) {
    case HOST_CREATE_NODE:
        if (argument_i32(ctx, argc, argv, 0, &a) < 0) return JS_EXCEPTION;
        return JS_NewInt32(ctx, pjs_ui_create_node((uint32_t)a));
    case HOST_DESTROY_NODE:
        if (argument_i32(ctx, argc, argv, 0, &a) < 0) return JS_EXCEPTION;
        pjs_ui_destroy_node(a);
        return JS_UNDEFINED;
    case HOST_INSERT_BEFORE:
        if (argument_i32(ctx, argc, argv, 0, &a) < 0 ||
            argument_i32(ctx, argc, argv, 1, &b) < 0 ||
            argument_i32(ctx, argc, argv, 2, &c) < 0) {
            return JS_EXCEPTION;
        }
        pjs_ui_insert_before(a, b, c);
        return JS_UNDEFINED;
    case HOST_REMOVE_CHILD:
        if (argument_i32(ctx, argc, argv, 0, &a) < 0 ||
            argument_i32(ctx, argc, argv, 1, &b) < 0) {
            return JS_EXCEPTION;
        }
        pjs_ui_remove_child(a, b);
        return JS_UNDEFINED;
    case HOST_SET_STYLE:
        if (argument_i32(ctx, argc, argv, 0, &a) < 0 ||
            argument_i32(ctx, argc, argv, 1, &b) < 0) {
            return JS_EXCEPTION;
        }
        pjs_ui_set_style(a, b);
        return JS_UNDEFINED;
    case HOST_SET_PROP:
        if (argument_i32(ctx, argc, argv, 0, &a) < 0 ||
            argument_i32(ctx, argc, argv, 1, &b) < 0 ||
            argument_f64(ctx, argc, argv, 2, &value) < 0) {
            return JS_EXCEPTION;
        }
        pjs_ui_set_prop(a, (uint32_t)b, value);
        return JS_UNDEFINED;
    case HOST_SET_PROP_BATCH:
        bytes_result = argument_bytes(ctx, argc, argv, 0, &bytes, &bytes_length);
        if (bytes_result == 1) pjs_ui_set_prop_batch(bytes, bytes_length);
        return JS_UNDEFINED;
    case HOST_SET_TEXT:
    case HOST_REPLACE_TEXT:
        text_result = argument_string(ctx, argc, argv, 1, &text, &text_length);
        if (text_result == 1) {
            if (argument_i32(ctx, argc, argv, 0, &a) < 0) {
                JS_FreeCString(ctx, text);
                return JS_EXCEPTION;
            }
            if (magic == HOST_SET_TEXT) {
                pjs_ui_set_text(a, (const uint8_t *)text, text_length);
            } else {
                pjs_ui_replace_text(a, (const uint8_t *)text, text_length);
            }
            JS_FreeCString(ctx, text);
        }
        return JS_UNDEFINED;
    case HOST_UPLOAD_TEXTURE:
        bytes_result = argument_bytes(ctx, argc, argv, 0, &bytes, &bytes_length);
        if (bytes_result != 1) return JS_NewInt32(ctx, -1);
        if (argument_u32(ctx, argc, argv, 1, &ua) < 0 ||
            argument_u32(ctx, argc, argv, 2, &ub) < 0 ||
            argument_u32(ctx, argc, argv, 3, &uc) < 0) {
            return JS_EXCEPTION;
        }
        return JS_NewInt32(ctx, pjs_ui_upload_texture(bytes, bytes_length, ua, ub, uc));
    case HOST_SET_IMAGE:
        if (argument_i32(ctx, argc, argv, 0, &a) < 0 ||
            argument_i32(ctx, argc, argv, 1, &b) < 0) {
            return JS_EXCEPTION;
        }
        pjs_ui_set_image(a, b);
        return JS_UNDEFINED;
    case HOST_SET_SPRITE:
        if (argument_i32(ctx, argc, argv, 0, &a) < 0 ||
            argument_i32(ctx, argc, argv, 1, &b) < 0 ||
            argument_u32(ctx, argc, argv, 2, &ua) < 0 ||
            argument_u32(ctx, argc, argv, 3, &ub) < 0 ||
            argument_u32(ctx, argc, argv, 4, &uc) < 0) {
            return JS_EXCEPTION;
        }
        pjs_ui_set_sprite(a, b, ua, ub, uc);
        return JS_UNDEFINED;
    case HOST_ANIMATE:
        if (argument_i32(ctx, argc, argv, 0, &a) < 0 ||
            argument_u32(ctx, argc, argv, 1, &ua) < 0 ||
            argument_f64(ctx, argc, argv, 2, &value) < 0 ||
            argument_u32(ctx, argc, argv, 4, &ub) < 0) {
            return JS_EXCEPTION;
        }
        if (argument_i32(ctx, argc, argv, 3, &b) < 0 ||
            argument_i32(ctx, argc, argv, 5, &c) < 0) {
            return JS_EXCEPTION;
        }
        return JS_NewInt32(ctx, pjs_ui_animate(
            a, ua, value, b < 0 ? 0u : (uint32_t)b, ub,
            c < 0 ? 0u : (uint32_t)c));
    case HOST_CANCEL_ANIM:
        if (argument_i32(ctx, argc, argv, 0, &a) < 0) return JS_EXCEPTION;
        pjs_ui_cancel_anim(a);
        return JS_UNDEFINED;
    case HOST_SET_FOCUS:
        if (argument_i32(ctx, argc, argv, 0, &a) < 0) return JS_EXCEPTION;
        pjs_ui_set_focus(a);
        return JS_UNDEFINED;
    case HOST_SET_ACTIVE:
        if (argument_i32(ctx, argc, argv, 0, &a) < 0 ||
            argument_i32(ctx, argc, argv, 1, &b) < 0) {
            return JS_EXCEPTION;
        }
        pjs_ui_set_active(a, b);
        return JS_UNDEFINED;
    case HOST_LOAD_STYLES:
    case HOST_LOAD_FONT_ATLAS:
        bytes_result = argument_bytes(ctx, argc, argv, 0, &bytes, &bytes_length);
        if (bytes_result != 1) return JS_NewBool(ctx, 0);
        return JS_NewBool(ctx, magic == HOST_LOAD_STYLES ?
            pjs_ui_load_styles(bytes, bytes_length) != 0 :
            pjs_ui_load_font_atlas(bytes, bytes_length) != 0);
    case HOST_MEASURE_TEXT:
        text_result = argument_string(ctx, argc, argv, 0, &text, &text_length);
        if (text_result != 1) return JS_NewFloat64(ctx, 0.0);
        if (argument_u32(ctx, argc, argv, 1, &ua) < 0) {
            JS_FreeCString(ctx, text);
            return JS_EXCEPTION;
        }
        value = (double)pjs_ui_measure_text(
            (const uint8_t *)text, text_length, ua);
        JS_FreeCString(ctx, text);
        return JS_NewFloat64(ctx, value);
    case HOST_FREE_TEXTURE:
        if (argument_i32(ctx, argc, argv, 0, &a) < 0) return JS_EXCEPTION;
        pjs_ui_free_texture(a);
        return JS_UNDEFINED;
    case HOST_UPLOAD_IMG_ENTRY:
        bytes_result = argument_bytes(ctx, argc, argv, 0, &bytes, &bytes_length);
        if (bytes_result != 1) return JS_NewInt32(ctx, -1);
        return JS_NewInt32(ctx, pjs_ui_upload_img_entry(bytes, bytes_length));
    default:
        return JS_ThrowInternalError(ctx, "unknown PocketJS HostOp");
    }
}

static int add_operation(JSValueConst object, const char *name, int arity,
                         HostOperation operation)
{
    JSValue function = JS_NewCFunctionMagic(
        context, host_operation, name, arity, JS_CFUNC_generic_magic, (int)operation);
    if (JS_IsException(function)) return -1;
    return JS_SetPropertyStr(context, object, name, function);
}

static int install_host(void)
{
    JSValue ui = JS_NewObject(context);
    if (JS_IsException(ui)) return -1;
    if (add_operation(ui, "createNode", 1, HOST_CREATE_NODE) < 0 ||
        add_operation(ui, "destroyNode", 1, HOST_DESTROY_NODE) < 0 ||
        add_operation(ui, "insertBefore", 3, HOST_INSERT_BEFORE) < 0 ||
        add_operation(ui, "removeChild", 2, HOST_REMOVE_CHILD) < 0 ||
        add_operation(ui, "setStyle", 2, HOST_SET_STYLE) < 0 ||
        add_operation(ui, "setProp", 3, HOST_SET_PROP) < 0 ||
        add_operation(ui, "setPropBatch", 1, HOST_SET_PROP_BATCH) < 0 ||
        add_operation(ui, "setText", 2, HOST_SET_TEXT) < 0 ||
        add_operation(ui, "replaceText", 2, HOST_REPLACE_TEXT) < 0 ||
        add_operation(ui, "uploadTexture", 4, HOST_UPLOAD_TEXTURE) < 0 ||
        add_operation(ui, "setImage", 2, HOST_SET_IMAGE) < 0 ||
        add_operation(ui, "setSprite", 5, HOST_SET_SPRITE) < 0 ||
        add_operation(ui, "animate", 6, HOST_ANIMATE) < 0 ||
        add_operation(ui, "cancelAnim", 1, HOST_CANCEL_ANIM) < 0 ||
        add_operation(ui, "setFocus", 1, HOST_SET_FOCUS) < 0 ||
        add_operation(ui, "setActive", 2, HOST_SET_ACTIVE) < 0 ||
        add_operation(ui, "loadStyles", 1, HOST_LOAD_STYLES) < 0 ||
        add_operation(ui, "loadFontAtlas", 1, HOST_LOAD_FONT_ATLAS) < 0 ||
        add_operation(ui, "measureText", 2, HOST_MEASURE_TEXT) < 0 ||
        add_operation(ui, "freeTexture", 1, HOST_FREE_TEXTURE) < 0 ||
        add_operation(ui, "uploadImgEntry", 1, HOST_UPLOAD_IMG_ENTRY) < 0 ||
        JS_SetPropertyStr(context, ui, "__host", JS_NewString(context, "ipod-photo")) < 0 ||
        JS_SetPropertyStr(context, ui, "__hostAbi", JS_NewInt32(context, PJS_QJS_HOST_ABI)) < 0 ||
        JS_SetPropertyStr(context, ui, "__root", JS_NewInt32(context, PJS_QJS_ROOT_ID)) < 0 ||
        JS_SetPropertyStr(context, ui, "__tickHz", JS_NewInt32(context, 60)) < 0) {
        JS_FreeValue(context, ui);
        return -1;
    }

    JSValue viewport = JS_NewObject(context);
    if (JS_IsException(viewport)) {
        JS_FreeValue(context, ui);
        return -1;
    }
    if (JS_SetPropertyStr(context, viewport, "w", JS_NewInt32(context, 220)) < 0 ||
        JS_SetPropertyStr(context, viewport, "h", JS_NewInt32(context, 176)) < 0) {
        JS_FreeValue(context, viewport);
        JS_FreeValue(context, ui);
        return -1;
    }
    /* JS_SetPropertyStr consumes viewport whether it succeeds or fails. */
    if (JS_SetPropertyStr(context, ui, "__viewport", viewport) < 0) {
        JS_FreeValue(context, ui);
        return -1;
    }

    ipod_input_value = JS_NewObject(context);
    if (JS_IsException(ipod_input_value) ||
        JS_SetPropertyStr(context, ui, "__ipodInput",
                          JS_DupValue(context, ipod_input_value)) < 0) {
        JS_FreeValue(context, ui);
        return -1;
    }

    launcher_value = JS_UNDEFINED;
    if (launcher_count != 0u) {
        JSValue apps = JS_NewArray(context);
        launcher_value = JS_NewObject(context);
        if (JS_IsException(apps) || JS_IsException(launcher_value)) {
            JS_FreeValue(context, apps);
            JS_FreeValue(context, ui);
            return -1;
        }
        for (uint32_t index = 0u; index < launcher_count; ++index) {
            JSValue label = JS_NewString(
                context, (const char *)launcher_labels[index]);
            if (JS_IsException(label) ||
                JS_SetPropertyUint32(context, apps, index, label) < 0) {
                JS_FreeValue(context, apps);
                JS_FreeValue(context, ui);
                return -1;
            }
        }
        if (JS_SetPropertyStr(context, launcher_value, "selected",
                              JS_NewInt32(context, -1)) < 0 ||
            JS_SetPropertyStr(context, ui, "__ipodApps", apps) < 0 ||
            JS_SetPropertyStr(context, ui, "__ipodLauncher",
                              JS_DupValue(context, launcher_value)) < 0) {
            JS_FreeValue(context, ui);
            return -1;
        }
    }

    /* JS_SetPropertyStr consumes ui whether it succeeds or fails. */
    if (JS_SetPropertyStr(context, global_value, "ui", ui) < 0 ||
        JS_SetPropertyStr(context, global_value, "__simHz", JS_NewInt32(context, 60)) < 0) {
        return -1;
    }
    return 0;
}

static void discard_exception(void)
{
    if (context == 0) return;
    JSValue exception = JS_GetException(context);
    JS_FreeValue(context, exception);
}

static bool drain_jobs(uint32_t error)
{
    for (uint32_t count = 0u; count < PJS_QJS_MAX_PENDING_JOBS; ++count) {
        JSContext *pending = 0;
        int result = JS_ExecutePendingJob(runtime, &pending);
        if (result > 0) continue;
        if (result < 0) {
            error_code = error;
            discard_exception();
            return false;
        }
        return true;
    }
    error_code = error;
    return false;
}

bool qjs_runtime_boot(const PjsGuestPackage *guest)
{
    error_code = PJS_QJS_ERROR_NONE;
    runtime = 0;
    context = 0;
    global_value = JS_UNDEFINED;
    frame_function = JS_UNDEFINED;
    ipod_input_value = JS_UNDEFINED;
    launcher_value = JS_UNDEFINED;
    legacy_frame_abi = false;
    if (guest == 0 || guest->javascript == 0 || guest->javascript_length < 2u ||
        guest->javascript[guest->javascript_length - 1u] != 0u) {
        error_code = PJS_QJS_ERROR_EVAL;
        return false;
    }

    runtime = JS_NewRuntime2(&qjs_allocators, 0);
    if (runtime == 0) {
        error_code = PJS_QJS_ERROR_RUNTIME;
        return false;
    }
    JS_SetMemoryLimit(runtime, PJS_QJS_MEMORY_LIMIT);
    JS_SetGCThreshold(runtime, PJS_QJS_GC_THRESHOLD);
    JS_SetMaxStackSize(runtime, PJS_QJS_STACK_LIMIT);
    JS_SetCanBlock(runtime, 0);
    JS_SetInterruptHandler(runtime, interrupt_handler, 0);

    context = JS_NewContext(runtime);
    if (context == 0) {
        error_code = PJS_QJS_ERROR_CONTEXT;
        qjs_runtime_shutdown();
        return false;
    }
    global_value = JS_GetGlobalObject(context);
    if (JS_IsException(global_value) || install_host() < 0) {
        error_code = PJS_QJS_ERROR_HOST;
        discard_exception();
        qjs_runtime_shutdown();
        return false;
    }

    if (guest->pak != 0 && guest->pak_length != 0u) {
        JSValue pak = JS_NewArrayBuffer(
            context, (uint8_t *)guest->pak, guest->pak_length, 0, 0, 0);
        if (JS_IsException(pak) ||
            JS_SetPropertyStr(context, global_value, "__pak", pak) < 0) {
            error_code = PJS_QJS_ERROR_HOST;
            discard_exception();
            qjs_runtime_shutdown();
            return false;
        }
    }

    execution_budget_start(PJS_QJS_BOOT_BUDGET_US);
    JSValue result = JS_Eval(
        context,
        (const char *)guest->javascript,
        guest->javascript_length - 1u,
        "embedded-recovery.js",
        JS_EVAL_TYPE_GLOBAL
    );
    execution_budget_stop();
    if (JS_IsException(result)) {
        error_code = execution_timed_out ?
            PJS_QJS_ERROR_BOOT_BUDGET : PJS_QJS_ERROR_EVAL;
        discard_exception();
        qjs_runtime_shutdown();
        return false;
    }
    JS_FreeValue(context, result);

    frame_function = JS_GetPropertyStr(context, global_value, "frame");
    if (!JS_IsFunction(context, frame_function)) {
        error_code = PJS_QJS_ERROR_FRAME_EXPORT;
        qjs_runtime_shutdown();
        return false;
    }
    /* The historical A1099 recovery package used seven device-local words.
     * Function.length is stable for both that package and the generated
     * framework wrapper (whose standard input surface is at most five words),
     * so this compatibility decision needs no app-specific marker. */
    JSValue frame_length = JS_GetPropertyStr(context, frame_function, "length");
    if (!JS_IsException(frame_length)) {
        int32_t arity = 0;
        if (JS_ToInt32(context, &arity, frame_length) == 0) {
            legacy_frame_abi = arity >= 7;
        } else {
            discard_exception();
        }
    } else {
        discard_exception();
    }
    JS_FreeValue(context, frame_length);
    execution_budget_start(PJS_QJS_BOOT_BUDGET_US);
    bool jobs_ok = drain_jobs(PJS_QJS_ERROR_PENDING_JOB);
    execution_budget_stop();
    if (!jobs_ok) {
        if (execution_timed_out) error_code = PJS_QJS_ERROR_BOOT_BUDGET;
        qjs_runtime_shutdown();
        return false;
    }
    return true;
}

bool qjs_runtime_frame(const PjsCoreInput *input)
{
    if (context == 0 || runtime == 0 || input == 0) return false;
    uint32_t buttons = guest_buttons(input);
    JSValue arguments[7];
    size_t argument_count;

    if (legacy_frame_abi) {
        /* Compatibility for the already-qualified disk package. */
        arguments[0] = JS_NewUint32(context, input->buttons);
        arguments[1] = JS_NewInt32(context, input->wheel_delta);
        arguments[2] = JS_NewUint32(context, input->wheel_position);
        arguments[3] = JS_NewBool(context, input->wheel_touched != 0u);
        arguments[4] = JS_NewBool(context, input->hold != 0u);
        arguments[5] = JS_NewUint32(context, input->battery_mv);
        arguments[6] = JS_NewUint32(context, input->power_flags);
        argument_count = 7u;
    } else {
        /* Standard generated-app ABI.  A1099 has no analog nub or touch
         * surface, so the centered analog word is the complete input frame;
         * optional touch arguments are intentionally omitted. */
        arguments[0] = JS_NewUint32(context, buttons);
        arguments[1] = JS_NewUint32(context, PJS_FRAME_ANALOG_CENTER);
        argument_count = 2u;
    }

    execution_budget_start(PJS_QJS_FRAME_BUDGET_US);
    if (update_ipod_input(input, buttons) < 0) {
        execution_budget_stop();
        error_code = PJS_QJS_ERROR_HOST;
        discard_exception();
        for (size_t index = 0u; index < argument_count; ++index) {
            JS_FreeValue(context, arguments[index]);
        }
        return false;
    }
    JSValue result = JS_Call(context, frame_function, global_value,
                             (int)argument_count, arguments);
    for (size_t index = 0u; index < argument_count; ++index) {
        JS_FreeValue(context, arguments[index]);
    }
    if (JS_IsException(result)) {
        execution_budget_stop();
        error_code = execution_timed_out ?
            PJS_QJS_ERROR_FRAME_BUDGET : PJS_QJS_ERROR_FRAME_CALL;
        discard_exception();
        return false;
    }
    JS_FreeValue(context, result);
    bool jobs_ok = drain_jobs(PJS_QJS_ERROR_PENDING_JOB);
    execution_budget_stop();
    return jobs_ok;
}

uint32_t qjs_runtime_error_code(void)
{
    return error_code;
}

int32_t qjs_runtime_launcher_selection(void)
{
    if (context == 0 || launcher_count == 0u ||
        JS_IsUndefined(launcher_value)) return -1;
    JSValue selected = JS_GetPropertyStr(context, launcher_value, "selected");
    if (JS_IsException(selected)) {
        discard_exception();
        return -1;
    }
    int32_t index = -1;
    if (JS_ToInt32(context, &index, selected) < 0) {
        discard_exception();
        index = -1;
    }
    JS_FreeValue(context, selected);
    return index >= 0 && (uint32_t)index < launcher_count ? index : -1;
}

void qjs_runtime_shutdown(void)
{
    execution_budget_stop();
    if (context != 0) {
        JS_FreeValue(context, launcher_value);
        JS_FreeValue(context, ipod_input_value);
        JS_FreeValue(context, frame_function);
        JS_FreeValue(context, global_value);
        JS_FreeContext(context);
        context = 0;
    }
    if (runtime != 0) {
        JS_FreeRuntime(runtime);
        runtime = 0;
    }
    global_value = JS_UNDEFINED;
    frame_function = JS_UNDEFINED;
    ipod_input_value = JS_UNDEFINED;
    launcher_value = JS_UNDEFINED;
    legacy_frame_abi = false;
}
