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

uint32_t qjs_runtime_error_code(void)
{
    return stub_error;
}

void qjs_runtime_shutdown(void)
{
}
