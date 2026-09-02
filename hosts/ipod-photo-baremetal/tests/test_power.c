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

    PjsPowerSourceFilter filter;
    power_source_filter_init(&filter);
    uint8_t stable = power_source_filter_update(
        &filter, PJS_POWER_USB, PJS_POWER_SOURCE_MASK);
    assert((stable & PJS_POWER_USB) == 0u);
    assert((stable & PJS_POWER_SOURCE_UNSTABLE) != 0u);
    stable = power_source_filter_update(
        &filter, PJS_POWER_USB, PJS_POWER_SOURCE_MASK);
    assert((stable & PJS_POWER_USB) == 0u);
    stable = power_source_filter_update(
        &filter, PJS_POWER_USB, PJS_POWER_SOURCE_MASK);
    assert((stable & PJS_POWER_USB) != 0u);
    assert((stable & PJS_POWER_SOURCE_UNSTABLE) == 0u);
    assert(filter.transitions == 1u);

    stable = power_source_filter_update(
        &filter, 0u, PJS_POWER_SOURCE_MASK);
    assert((stable & PJS_POWER_USB) != 0u);
    assert((stable & PJS_POWER_SOURCE_UNSTABLE) != 0u);
    stable = power_source_filter_update(
        &filter, 0u, PJS_POWER_SOURCE_MASK);
    stable = power_source_filter_update(
        &filter, 0u, PJS_POWER_SOURCE_MASK);
    assert((stable & PJS_POWER_USB) == 0u);
    assert((stable & PJS_POWER_SOURCE_UNSTABLE) == 0u);

    /* An initial all-clear sample still requires the full qualification
     * window; zero is a real source state, not an initialized sentinel. */
    power_source_filter_init(&filter);
    for (uint32_t sample = 1u; sample < PJS_POWER_SOURCE_FILTER_SAMPLES;
         ++sample) {
        stable = power_source_filter_update(
            &filter, 0u, PJS_POWER_SOURCE_MASK);
        assert((stable & PJS_POWER_SOURCE_UNSTABLE) != 0u);
    }
    stable = power_source_filter_update(
        &filter, 0u, PJS_POWER_SOURCE_MASK);
    assert((stable & PJS_POWER_SOURCE_UNSTABLE) == 0u);

    /* An unavailable charging input never fabricates a charging state. */
    stable = power_source_filter_update(
        &filter, PJS_POWER_USB, PJS_POWER_FIREWIRE | PJS_POWER_USB);
    assert((stable & PJS_POWER_CHARGING) == 0u);
    assert((stable & PJS_POWER_SOURCE_UNSTABLE) != 0u);
    puts("power tests: OK");
    return 0;
}
