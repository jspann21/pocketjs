#include "qjs_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "heap.h"
#include "quickjs.h"
#include "timer.h"

#define PJS_QJS_MEMORY_LIMIT (6u * 1024u * 1024u)
#define PJS_QJS_GC_THRESHOLD (512u * 1024u)
#define PJS_QJS_STACK_LIMIT (192u * 1024u)
#define PJS_QJS_BOOT_BUDGET_US 500000u
#define PJS_QJS_FRAME_BUDGET_US 100000u
#define PJS_QJS_MAX_PENDING_JOBS 64u
#define PJS_QJS_MALLOC_OVERHEAD 8u
#define PJS_QJS_ROOT_ID 1
#define PJS_QJS_HOST_ABI 1

/* These are deliberately the first PocketJS HostOps required by the embedded
 * recovery guest. The ABI surface grows append-only toward the complete host
 * contract; none of these calls know about the LCD or JavaScript runtime. */
typedef enum {
    HOST_CREATE_NODE = 1,
    HOST_DESTROY_NODE,
    HOST_INSERT_BEFORE,
    HOST_REMOVE_CHILD,
    HOST_SET_PROP,
} HostOperation;

static JSRuntime *runtime;
static JSContext *context;
static JSValue global_value;
static JSValue frame_function;
static uint32_t error_code;
static uint32_t execution_deadline;
static bool execution_timed_out;

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

static int argument_f64(JSContext *ctx, int argc, JSValueConst *argv,
                        int index, double *value)
{
    if (index >= argc) {
        *value = 0.0;
        return 0;
    }
    return JS_ToFloat64(ctx, value, argv[index]);
}

static JSValue host_operation(JSContext *ctx, JSValueConst this_value,
                              int argc, JSValueConst *argv, int magic)
{
    (void)this_value;
    int32_t a = 0;
    int32_t b = 0;
    int32_t c = 0;
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
    case HOST_SET_PROP:
        if (argument_i32(ctx, argc, argv, 0, &a) < 0 ||
            argument_i32(ctx, argc, argv, 1, &b) < 0 ||
            argument_f64(ctx, argc, argv, 2, &value) < 0) {
            return JS_EXCEPTION;
        }
        pjs_ui_set_prop(a, (uint32_t)b, value);
        return JS_UNDEFINED;
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
        add_operation(ui, "setProp", 3, HOST_SET_PROP) < 0 ||
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
        error_code = PJS_QJS_ERROR_EVAL;
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
    execution_budget_start(PJS_QJS_BOOT_BUDGET_US);
    bool jobs_ok = drain_jobs(PJS_QJS_ERROR_PENDING_JOB);
    execution_budget_stop();
    if (!jobs_ok) {
        qjs_runtime_shutdown();
        return false;
    }
    return true;
}

bool qjs_runtime_frame(const PjsCoreInput *input)
{
    if (context == 0 || runtime == 0 || input == 0) return false;
    JSValue arguments[7] = {
        JS_NewUint32(context, input->buttons),
        JS_NewInt32(context, input->wheel_delta),
        JS_NewUint32(context, input->wheel_position),
        JS_NewBool(context, input->wheel_touched != 0u),
        JS_NewBool(context, input->hold != 0u),
        JS_NewUint32(context, input->battery_mv),
        JS_NewUint32(context, input->power_flags),
    };

    execution_budget_start(PJS_QJS_FRAME_BUDGET_US);
    JSValue result = JS_Call(context, frame_function, global_value, 7, arguments);
    for (size_t index = 0u; index < 7u; ++index) JS_FreeValue(context, arguments[index]);
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

void qjs_runtime_shutdown(void)
{
    execution_budget_stop();
    if (context != 0) {
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
}
