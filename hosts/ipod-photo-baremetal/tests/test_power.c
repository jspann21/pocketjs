#include "power.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(power_battery_mv_from_raw(0u) == 0u);
    assert(power_battery_mv_from_raw(512u) == 3000u);
    assert(power_battery_mv_from_raw(1023u) == 5994u);
    assert(power_battery_mv_from_raw(65535u) == 5994u);

    assert(power_decode_source_flags(0xffffffffu, 0u, 1u) == 0u);
    assert((power_decode_source_flags(0u, 0u, 1u) & PJS_POWER_FIREWIRE) != 0u);
    assert((power_decode_source_flags(0xffffffffu, 0x08u, 1u) & PJS_POWER_USB) != 0u);
    assert((power_decode_source_flags(0xffffffffu, 0u, 0u) & PJS_POWER_CHARGING) != 0u);
    assert(PJS_POWER_TELEMETRY_DISABLED == (1u << 5));
    assert(PJS_POWER_ADC_RANGE_FAULT == (1u << 6));
    puts("power tests: OK");
    return 0;
}
