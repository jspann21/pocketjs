#include "core_bridge.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    PjsCoreDamagePlan plan = {0};
    assert(sizeof(PjsCoreInput) == 44u);
    assert(sizeof(PjsCoreDamageRect) == 16u);
    assert(sizeof(PjsCoreDamagePlan) == 144u);
#if UINTPTR_MAX == UINT32_MAX
    assert(sizeof(PjsGuestPackage) == 40u);
#else
    assert(sizeof(PjsGuestPackage) == 64u);
#endif
    assert(plan.count == 0u);
    assert(PJS_CORE_MAX_DAMAGE_REGIONS == 8u);
    puts("damage ABI tests: OK");
    return 0;
}
