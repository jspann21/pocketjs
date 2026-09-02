#ifndef POCKETJS_IPOD_PHOTO_POWER_H
#define POCKETJS_IPOD_PHOTO_POWER_H

#include <stdbool.h>
#include <stdint.h>

enum {
    PJS_POWER_FIREWIRE = 1u << 0,
    PJS_POWER_USB = 1u << 1,
    PJS_POWER_CHARGING = 1u << 2,
    PJS_POWER_ADC_VALID = 1u << 3,
    PJS_POWER_I2C_FAULT = 1u << 4,
    PJS_POWER_TELEMETRY_DISABLED = 1u << 5,
    PJS_POWER_ADC_RANGE_FAULT = 1u << 6,
    /* Source pins have not produced the same value for the filter's
     * qualification window, or one of the source pins is not observable. */
    PJS_POWER_SOURCE_UNSTABLE = 1u << 7,
};

#define PJS_POWER_SOURCE_MASK \
    (PJS_POWER_FIREWIRE | PJS_POWER_USB | PJS_POWER_CHARGING)
#define PJS_POWER_SOURCE_FILTER_SAMPLES 3u

typedef struct {
    uint8_t flags;
    uint8_t valid_mask;
} PjsPowerSourceSample;

typedef struct {
    uint8_t stable_flags;
    uint8_t candidate_flags;
    uint8_t candidate_samples;
    uint8_t valid_mask;
    uint8_t qualified;
    uint32_t samples;
    uint32_t transitions;
} PjsPowerSourceFilter;

typedef struct {
    uint16_t battery_raw;
    uint16_t battery_mv;
    uint8_t flags;
    uint8_t consecutive_failures;
    uint32_t samples;
} PjsPowerTelemetry;

uint8_t power_source_flags_read(void);
void power_telemetry_init(void);
void power_telemetry_sample(PjsPowerTelemetry *telemetry);
uint16_t power_battery_mv_from_raw(uint16_t raw);
uint8_t power_decode_source_flags(uint32_t gpioc, uint32_t gpiod,
                                  uint32_t gpo32_input);

/* Read source pins without configuring a peripheral or writing a PMU,
 * charger, or power-rail register. The valid mask is deliberately separate
 * from flags: an inherited loader may not have enabled the LTC4066 input. */
void power_source_sample_read_only(PjsPowerSourceSample *sample);
uint8_t power_source_flags_read_only(void);

/* Debounce only source bits. The returned value contains the last qualified
 * source state and PJS_POWER_SOURCE_UNSTABLE until every source pin is both
 * observable and stable for PJS_POWER_SOURCE_FILTER_SAMPLES samples. */
void power_source_filter_init(PjsPowerSourceFilter *filter);
uint8_t power_source_filter_update(PjsPowerSourceFilter *filter,
                                   uint8_t raw_flags, uint8_t valid_mask);

#endif
