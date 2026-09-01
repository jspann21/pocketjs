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
};

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

#endif
