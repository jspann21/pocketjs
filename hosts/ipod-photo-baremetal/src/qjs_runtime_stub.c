#include "qjs_runtime.h"

static uint32_t stub_error = PJS_QJS_ERROR_RUNTIME;

bool qjs_runtime_boot(const PjsGuestPackage *guest)
{
    (void)guest;
    stub_error = PJS_QJS_ERROR_RUNTIME;
    return false;
}

bool qjs_runtime_frame(const PjsCoreInput *input)
{
    (void)input;
    return false;
}

bool qjs_runtime_set_launcher_catalog(const uint8_t *labels,
                                      uint32_t count, uint32_t stride)
{
    (void)labels;
    (void)count;
    (void)stride;
    return false;
}

int32_t qjs_runtime_launcher_selection(void)
{
    return -1;
}

uint32_t qjs_runtime_error_code(void)
{
    return stub_error;
}

void qjs_runtime_shutdown(void)
{
}
